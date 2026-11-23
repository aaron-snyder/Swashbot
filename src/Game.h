// File: Game.h
// The Swashbot game engine. It knows nothing about Discord: the bot layer (MyBot.cpp) turns
// slash commands into Game::command() calls and posts whatever Reply/Announcement comes back.
//
// How time-based events work
// --------------------------
// Every server's ship stores *timestamps*, not running timers: when the next random event is due,
// when the current event ends, and when the enemy fires next. The bot calls Game::tick(now) every
// few seconds and the engine simply checks "has that time passed yet?". Because all state is plain
// data saved to disk, a restart (or crash) loses nothing - on load() deadlines are pushed back by a
// short grace period so the crew isn't punished for the bot being offline.

#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>
#include <vector>
#include "Random.h"
#include "Ship.h"

// All the balance knobs in one place (seconds unless noted)
namespace Tuning {
    constexpr int64_t EVENT_GAP_MIN = 30 * 60;      // calm time between random events
    constexpr int64_t EVENT_GAP_MAX = 75 * 60;
    constexpr int64_t FIRST_EVENT_MIN = 5 * 60;     // first event after /sail
    constexpr int64_t FIRST_EVENT_MAX = 10 * 60;
    constexpr int64_t COMBAT_DURATION = 8 * 60;     // enemy boards/escapes after this
    constexpr int64_t COMBAT_FIRST_VOLLEY = 45;
    constexpr int64_t COMBAT_VOLLEY_EVERY = 75;
    constexpr int BOARD_BELOW_PERCENT = 30;         // pirates board (instead of sinking us) below this hull %
    constexpr int MIN_SAIL_HP_PERCENT = 30;         // below this the ship heads for a cove and won't /sail
    constexpr int64_t HAZARD_DURATION = 6 * 60;     // storms and fires
    constexpr int64_t HAZARD_PULSE_EVERY = 60;
    constexpr int64_t BOUNTY_DURATION = 5 * 60;     // floating wreckage
    constexpr int64_t RAID_COOLDOWN = 15 * 60;      // per ship
    constexpr int64_t RESUME_GRACE = 3 * 60;        // added to deadlines after a restart
    constexpr int AUTO_ANCHOR_AFTER = 3;            // ignored events in a row before anchoring
    constexpr int SINK_GOLD_LOSS_PERCENT = 40;
    constexpr int SINK_WOOD_LOSS_PERCENT = 50;
    constexpr int64_t DOCK_REPAIR_EVERY = 10 * 60;  // anchored ships slowly patch themselves up...
    constexpr int DOCK_REPAIR_PERCENT = 5;          // ...by this % of max HP each time

    // Per-crew-member cooldowns, so a fight needs several people rather than one fast typist
    constexpr int64_t CD_FIRE = 12;
    constexpr int64_t CD_REPAIR = 15;
    constexpr int64_t CD_HAZARD = 8;                // bail / douse
    constexpr int64_t CD_SALVAGE = 10;
    constexpr int64_t CD_FLEE = 20;

    constexpr int WOOD_PRICE = 8;
    constexpr int STEEL_HULL_PRICE = 600;
    constexpr int CRYSTAL_HULL_PRICE = 1500;
    constexpr int SILVER_CANNON_PRICE = 500;
    constexpr int GOLD_CANNON_PRICE = 1400;
    constexpr int GUN_PRICE_PER_GUN = 150;          // next gun costs this * guns you have
}

enum class Tone { Info, Good, Bad, Alert };

// A message for Discord. The bot layer turns it into an embed.
struct Reply {
    std::string title;
    std::string body;
    Tone tone = Tone::Info;
    bool ephemeral = false;   // only the person who ran the command sees it
    bool pingCrew = false;    // mention the crew role (something needs attention)
};

// A message the engine wants posted on its own (from tick), not as a command reply
struct Announcement {
    uint64_t guildId = 0;
    uint64_t channelId = 0;
    uint64_t roleId = 0;
    Reply message;
};

enum class EventKind { None, EnemyShip, Merchant, Kraken, Storm, Fire, Wreckage };

// Anything the crew can shoot at
struct Foe {
    std::string name;
    int hp = 0, maxHp = 0, ac = 10;
    int hitBonus = 0, damageDie = 1, guns = 1;
    int goldReward = 0, woodReward = 0;
};

struct Event {
    EventKind kind = EventKind::None;
    int64_t startedAt = 0;
    int64_t deadline = 0;
    int64_t nextPulseAt = 0;       // next enemy volley / storm surge / fire spread
    Foe foe;                       // EnemyShip, Merchant, Kraken
    int severity = 0;              // Storm/Fire: water or flames left. Wreckage: wood left to grab
    int startingSeverity = 0;
    int goldPool = 0;              // Wreckage: gold left to grab
    bool crewResponded = false;    // did anybody act during this event?

    bool active() const { return kind != EventKind::None; }
    bool isCombat() const { return kind == EventKind::EnemyShip || kind == EventKind::Merchant || kind == EventKind::Kraken; }
};

struct ShipStats {
    long long lifetimeGold = 0;    // all gold ever earned
    int victories = 0;             // ships sunk or captured
    int krakens = 0;
    int hazardsWeathered = 0;      // storms/fires beaten before they ran their course
    int raids = 0;
    int timesSunk = 0;
};

struct CrewMember {
    std::string name;
    int64_t joinedAt = 0;
    bool aboard = true;            // false after /leave (stats are kept if they come back)
    int points = 0;
    int shots = 0, hits = 0, damage = 0;
    int repairs = 0, hazardWork = 0, plunder = 0;
};

// Everything about one Discord server's ship
struct GuildState {
    uint64_t guildId = 0;
    std::string guildName;
    uint64_t channelId = 0;        // the crew-only ship channel
    uint64_t roleId = 0;           // the crew role (grants access to the channel)
    Ship ship;
    ShipStats stats;
    std::map<uint64_t, CrewMember> crew;
    bool sailing = false;          // anchored ships get no random events
    int64_t nextEventAt = 0;
    int64_t nextRaidAt = 0;
    int64_t nextDockRepairAt = 0;
    int ignoredEvents = 0;
    Event event;

    bool configured() const { return channelId != 0; }
    int crewCount() const;
    long long score() const;
};

// Who/where/when a command came from
struct Ctx {
    uint64_t guildId = 0;
    uint64_t channelId = 0;
    uint64_t userId = 0;
    std::string userName;
    int64_t now = 0;
};

class Game {
public:
    // dataPath == "" disables saving (used by tests). seed makes dice rolls repeatable.
    explicit Game(std::string dataPath = "", std::optional<uint32_t> seed = std::nullopt);

    // Persistence (Storage.cpp). load() also gives active events a grace period after downtime.
    bool load(int64_t now);
    bool save();

    // --- Server setup (the bot creates the channel + role, then hands the ids over) ---
    struct SetupInfo { uint64_t channelId = 0; uint64_t roleId = 0; };
    SetupInfo setupInfo(uint64_t guildId);
    Reply configure(uint64_t guildId, const std::string& guildName, uint64_t channelId, uint64_t roleId, int64_t now);
    void setGuildName(uint64_t guildId, const std::string& name);
    std::vector<uint64_t> crewIds(uint64_t guildId);   // everyone currently aboard (to re-grant a recreated role)

    // --- Joining the crew. check*() runs before the bot touches roles, join()/leave() after ---
    std::optional<Reply> checkJoin(const Ctx& ctx);
    Reply join(const Ctx& ctx);
    std::optional<Reply> checkLeave(const Ctx& ctx);
    Reply leave(const Ctx& ctx);

    // --- Every other slash command. `arg` is the command's text option, `amount` its number option ---
    Reply command(const std::string& name, const Ctx& ctx, const std::string& arg = "", int64_t amount = 0);

    // --- The heartbeat. Call every few seconds; returns messages to post ---
    std::vector<Announcement> tick(int64_t now);

    // --- Test helpers ---
    GuildState snapshot(uint64_t guildId);
    void edit(uint64_t guildId, const std::function<void(GuildState&)>& fn);
    Announcement startEvent(uint64_t guildId, EventKind kind, int64_t now);

private:
    enum class Action { Fire, Repair, Hazard, Salvage, Flee };

    // Command handlers (lock already held)
    Reply status(GuildState& g, const Ctx& c);
    Reply crewBoard(GuildState& g);
    Reply leaderboard(const std::string& category, uint64_t guildId);
    Reply shop(GuildState& g);
    Reply help();
    Reply rename(GuildState& g, const std::string& name);
    Reply sail(GuildState& g, const Ctx& c);
    Reply anchor(GuildState& g, const Ctx& c);
    Reply hunt(GuildState& g, const Ctx& c);
    Reply raid(GuildState& g, const Ctx& c);
    Reply fire(GuildState& g, const Ctx& c);
    Reply flee(GuildState& g, const Ctx& c);
    Reply repair(GuildState& g, const Ctx& c);
    Reply fightHazard(GuildState& g, const Ctx& c, EventKind kind);
    Reply salvage(GuildState& g, const Ctx& c);
    Reply buy(GuildState& g, const Ctx& c, const std::string& item, int64_t amount);

    // Event engine
    Reply beginEvent(GuildState& g, EventKind kind, int64_t now);
    std::optional<Reply> pulse(GuildState& g, int64_t now);
    Reply timeout(GuildState& g, int64_t now);
    std::string foeVolley(GuildState& g, int64_t now);
    std::string winFight(GuildState& g, int64_t now);
    std::string endEvent(GuildState& g, int64_t now);
    std::string sink(GuildState& g, int64_t now);
    EventKind rollEventKind();

    // Helpers
    std::optional<Reply> onCooldown(const Ctx& c, Action a, int64_t seconds);
    CrewMember& member(GuildState& g, const Ctx& c);
    GuildState& guild(uint64_t guildId);
    bool saveLocked();

    std::string dataPath;
    Rng rng;
    std::mutex mutex;
    std::map<uint64_t, GuildState> guilds;
    std::map<std::tuple<uint64_t, uint64_t, int>, int64_t> cooldowns;  // (guild, user, action) -> ready at
};
