// File: Game.cpp
// Swashbot game rules: commands, random events, the economy and the leaderboards.

#include "Game.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>

using namespace ShipSpecs;

// ============================================================================
// Formatting helpers
// ============================================================================
namespace {

// Discord renders <t:UNIX:R> as a live countdown ("in 3 minutes") in each user's own timezone
std::string when(int64_t unixTime) { return "<t:" + std::to_string(unixTime) + ":R>"; }

std::string channelLink(uint64_t channelId) { return "<#" + std::to_string(channelId) + ">"; }

std::string withCommas(long long n) {
    std::string digits = std::to_string(n < 0 ? -n : n);
    std::string out;
    for (size_t i = 0; i < digits.size(); i++) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return (n < 0 ? "-" : "") + out;
}

std::string bar(int value, int max, int width = 12) {
    if (max <= 0) max = 1;
    int filled = static_cast<int>(std::lround(static_cast<double>(std::clamp(value, 0, max)) / max * width));
    std::string out;
    for (int i = 0; i < width; i++) out += (i < filled ? "\xE2\x96\x88" : "\xE2\x96\x91");  // █ / ░
    return "`" + out + "` " + std::to_string(std::max(value, 0)) + "/" + std::to_string(max);
}

std::string oneDecimal(double v) {
    std::ostringstream ss;
    ss.setf(std::ios::fixed);
    ss.precision(1);
    ss << v;
    return ss.str();
}

std::string bold(const std::string& s) { return "**" + s + "**"; }

Reply reply(const std::string& title, const std::string& body, Tone tone = Tone::Info) {
    Reply r;
    r.title = title;
    r.body = body;
    r.tone = tone;
    return r;
}

// Errors and "you can't do that right now" messages are only shown to the person who asked
Reply privateNote(const std::string& body) {
    Reply r;
    r.body = body;
    r.tone = Tone::Info;
    r.ephemeral = true;
    return r;
}

const std::array<const char*, 12> ENEMY_NAMES = {
    "the Rusty Gull", "the Black Wake", "the Widow's Lantern", "the Salt Ghost",
    "the Barnacle Queen", "the Drowned Fiddle", "the Gilded Eel", "the Squall Hound",
    "the Iron Petrel", "the Moray's Grin", "the Tattered Saint", "the Cutthroat Clam",
};

const char* NOT_SET_UP =
    "Swashbot couldn't build its ship channel in this server yet. It needs the **Manage Channels** and "
    "**Manage Roles** permissions - once it has them, an admin can run `/setup`.";

const std::array<const char*, 6> MERCHANT_NAMES = {
    "the Fat Abbot", "the Spice Countess", "the Velvet Ledger",
    "the Golden Ewe", "the Lazy Tortoise", "the Silk Hen",
};

std::string eventName(EventKind k) {
    switch (k) {
    case EventKind::EnemyShip: return "Enemy attack";
    case EventKind::Merchant: return "Merchant sighted";
    case EventKind::Kraken: return "Kraken";
    case EventKind::Storm: return "Storm";
    case EventKind::Fire: return "Fire aboard";
    case EventKind::Wreckage: return "Floating wreckage";
    case EventKind::None: break;
    }
    return "Calm seas";
}

// Cleans user-supplied ship names so they can't ping people or break formatting
std::string cleanName(const std::string& raw) {
    std::string out;
    for (char ch : raw) {
        if (std::string("*_`~|<>@#\\\n\r").find(ch) == std::string::npos) out += ch;
    }
    size_t start = out.find_first_not_of(' ');
    size_t end = out.find_last_not_of(' ');
    if (start == std::string::npos) return "";
    out = out.substr(start, end - start + 1);
    return out.size() > 40 ? out.substr(0, 40) : out;
}

}  // namespace

// ============================================================================
// GuildState
// ============================================================================

int GuildState::crewCount() const {
    return static_cast<int>(std::count_if(crew.begin(), crew.end(), [](const auto& kv) { return kv.second.aboard; }));
}

// Ship score: everything the crew has earned, plus bonuses for glory, minus a penalty for sinking.
long long GuildState::score() const {
    long long s = stats.lifetimeGold
                + 150LL * stats.victories
                + 300LL * stats.krakens
                + 50LL * stats.hazardsWeathered
                + 25LL * stats.raids
                - 200LL * stats.timesSunk;
    return std::max(0LL, s);
}

// ============================================================================
// Game: setup, crew, dispatch
// ============================================================================

Game::Game(std::string path, std::optional<uint32_t> seed)
    : dataPath(std::move(path)), rng(seed ? Rng(*seed) : Rng()) {}

GuildState& Game::guild(uint64_t guildId) {
    GuildState& g = guilds[guildId];
    g.guildId = guildId;
    return g;
}

Game::SetupInfo Game::setupInfo(uint64_t guildId) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = guilds.find(guildId);
    if (it == guilds.end()) return {};
    return { it->second.channelId, it->second.roleId };
}

Reply Game::configure(uint64_t guildId, const std::string& guildName, uint64_t channelId, uint64_t roleId, int64_t now) {
    std::lock_guard<std::mutex> lock(mutex);
    GuildState& g = guild(guildId);
    if (!guildName.empty()) g.guildName = guildName;
    g.channelId = channelId;
    g.roleId = roleId;
    (void)now;  // a new ship waits at anchor; join() sets sail when the first crew member signs on
    saveLocked();

    Reply r = reply("\xE2\x9A\x93 " + g.ship.getName() + " is ready to sail!",
        "This channel is the deck of your server's ship. Everyone who `/join`s the crew can see it and help run her. "
        "She weighs anchor as soon as the first sailor signs on.\n\n"
        "**How it works**\n"
        "\xE2\x80\xA2 Every so often something happens at sea - an enemy attack, a storm, a fire, a fat merchant ship. "
        "The crew gets pinged and has a few minutes to respond.\n"
        "\xE2\x80\xA2 Fights need many hands: each command has a short per-person cooldown, so the more crew show up, the less we lose.\n"
        "\xE2\x80\xA2 Spend plunder in the `/shop` to upgrade the hull, cannons and guns, and climb the `/leaderboard`.\n"
        "\xE2\x80\xA2 Nobody around? If three alarms go unanswered the ship drops anchor by itself. `/sail` sets out again.\n\n"
        "Type `/help` for every command.", Tone::Good);
    return r;
}

void Game::setGuildName(uint64_t guildId, const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = guilds.find(guildId);
    if (it != guilds.end() && it->second.guildName != name) {
        it->second.guildName = name;
        saveLocked();
    }
}

std::vector<uint64_t> Game::crewIds(uint64_t guildId) {
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<uint64_t> ids;
    auto it = guilds.find(guildId);
    if (it == guilds.end()) return ids;
    for (const auto& [id, m] : it->second.crew) {
        if (m.aboard) ids.push_back(id);
    }
    return ids;
}

std::optional<Reply> Game::checkJoin(const Ctx& c) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = guilds.find(c.guildId);
    if (it == guilds.end() || !it->second.configured()) {
        return privateNote(NOT_SET_UP);
    }
    auto m = it->second.crew.find(c.userId);
    if (m != it->second.crew.end() && m->second.aboard) {
        return privateNote("You're already part of the crew! Head to " + channelLink(it->second.channelId) + ".");
    }
    return std::nullopt;
}

Reply Game::join(const Ctx& c) {
    std::lock_guard<std::mutex> lock(mutex);
    GuildState& g = guild(c.guildId);
    CrewMember& m = g.crew[c.userId];
    bool returning = m.joinedAt != 0;
    m.name = c.userName;
    m.aboard = true;
    if (!returning) m.joinedAt = c.now;

    // A brand-new ship sets sail when its first sailor arrives
    if (!g.sailing && !g.event.active() && g.nextEventAt == 0 && g.crewCount() == 1) {
        g.sailing = true;
        g.nextEventAt = c.now + rng.range(Tuning::FIRST_EVENT_MIN, Tuning::FIRST_EVENT_MAX);
    }
    saveLocked();
    Reply r = privateNote(std::string(returning ? "Welcome back aboard, " : "Welcome aboard, ") + c.userName +
                          "! The ship is in " + channelLink(g.channelId) + ". Try `/status` there.");
    r.tone = Tone::Good;
    return r;
}

std::optional<Reply> Game::checkLeave(const Ctx& c) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = guilds.find(c.guildId);
    if (it == guilds.end() || !it->second.configured()) {
        return privateNote(NOT_SET_UP);
    }
    auto m = it->second.crew.find(c.userId);
    if (m == it->second.crew.end() || !m->second.aboard) {
        return privateNote("You're not on the crew.");
    }
    return std::nullopt;
}

Reply Game::leave(const Ctx& c) {
    std::lock_guard<std::mutex> lock(mutex);
    GuildState& g = guild(c.guildId);
    g.crew[c.userId].aboard = false;
    saveLocked();
    return privateNote("You've left the crew. Your record is kept for if you ever `/join` again.");
}

CrewMember& Game::member(GuildState& g, const Ctx& c) {
    CrewMember& m = g.crew[c.userId];
    if (!c.userName.empty()) m.name = c.userName;  // pick up nickname changes
    return m;
}

Reply Game::command(const std::string& name, const Ctx& c, const std::string& arg, int64_t amount) {
    std::lock_guard<std::mutex> lock(mutex);

    // Commands that work anywhere, even before setup
    if (name == "help") return help();
    if (name == "leaderboard") {
        Reply r = leaderboard(arg, c.guildId);
        auto it = guilds.find(c.guildId);
        r.ephemeral = it == guilds.end() || c.channelId != it->second.channelId;  // don't spam other channels
        return r;
    }

    auto it = guilds.find(c.guildId);
    if (it == guilds.end() || !it->second.configured()) {
        return privateNote(NOT_SET_UP);
    }
    GuildState& g = it->second;

    // Read-only commands: anyone can look, but outside the ship channel only they see the answer
    if (name == "status" || name == "crew" || name == "shop" || name == "rename") {
        Reply r = name == "status" ? status(g, c)
                : name == "crew"   ? crewBoard(g)
                : name == "shop"   ? shop(g)
                                   : rename(g, arg);
        if (c.channelId != g.channelId) r.ephemeral = true;
        saveLocked();
        return r;
    }

    // Everything else is running the ship: crew only, and only on deck
    auto m = g.crew.find(c.userId);
    if (m == g.crew.end() || !m->second.aboard) {
        return privateNote("You're not part of the crew! Use `/join` to sign on.");
    }
    if (c.channelId != g.channelId) {
        return privateNote("Ship business happens on deck: " + channelLink(g.channelId));
    }

    Reply r;
    if (name == "sail") r = sail(g, c);
    else if (name == "anchor") r = anchor(g, c);
    else if (name == "hunt") r = hunt(g, c);
    else if (name == "raid") r = raid(g, c);
    else if (name == "fire") r = fire(g, c);
    else if (name == "flee") r = flee(g, c);
    else if (name == "repair") r = repair(g, c);
    else if (name == "bail") r = fightHazard(g, c, EventKind::Storm);
    else if (name == "douse") r = fightHazard(g, c, EventKind::Fire);
    else if (name == "salvage") r = salvage(g, c);
    else if (name == "buy") r = buy(g, c, arg, amount);
    else return privateNote("Unknown command.");

    saveLocked();
    return r;
}

std::optional<Reply> Game::onCooldown(const Ctx& c, Action a, int64_t seconds) {
    auto key = std::make_tuple(c.guildId, c.userId, static_cast<int>(a));
    auto it = cooldowns.find(key);
    if (it != cooldowns.end() && it->second > c.now) {
        return privateNote("Catch your breath! You can do that again " + when(it->second) + ".");
    }
    cooldowns[key] = c.now + seconds;
    return std::nullopt;
}

// ============================================================================
// Read-only commands
// ============================================================================

Reply Game::help() {
    Reply r = reply("\xF0\x9F\x8F\xB4\xE2\x80\x8D\xE2\x98\xA0\xEF\xB8\x8F Swashbot commands",
        "**Crew**\n"
        "`/join` `/leave` - sign on or off the crew (gives access to the ship channel)\n"
        "`/status` - the ship, cargo and current situation\n"
        "`/crew` - who's pulling their weight\n"
        "`/leaderboard` - top ships across every server\n\n"
        "**Sailing**\n"
        "`/sail` / `/anchor` - set out (random events happen) or rest at anchor (nothing happens)\n"
        "`/hunt` - look for a fight\n"
        "`/raid` - raid an island for loot (might be guarded!)\n\n"
        "**When things go wrong**\n"
        "`/fire` - fire a broadside at the enemy\n"
        "`/flee` - try to escape a fight\n"
        "`/repair` - patch the hull with 1 wood\n"
        "`/bail` - bail water during a storm\n"
        "`/douse` - fight a fire aboard\n"
        "`/salvage` - grab floating wreckage\n\n"
        "**Port**\n"
        "`/shop` - prices  \xE2\x80\xA2  `/buy` - buy wood or upgrades\n\n"
        "**Admins**\n"
        "`/setup` - repair the ship channel and crew role  \xE2\x80\xA2  `/rename` - name the ship");
    r.ephemeral = true;
    return r;
}

Reply Game::status(GuildState& g, const Ctx& c) {
    const Ship& s = g.ship;
    std::string body =
        bold(s.info()) + "\n" +
        "Hull: " + bar(s.getHp(), s.getMaxHp()) + "  \xE2\x80\xA2  AC " + std::to_string(s.getAc()) + "\n" +
        "Gold: " + bold(withCommas(s.getGold())) + "  \xE2\x80\xA2  Wood: " + bold(std::to_string(s.getWood())) +
        "  \xE2\x80\xA2  Firepower: " + bold(oneDecimal(s.firepower())) + "\n" +
        "Score: " + bold(withCommas(g.score())) + "  \xE2\x80\xA2  Victories: " + std::to_string(g.stats.victories) +
        "  \xE2\x80\xA2  Krakens: " + std::to_string(g.stats.krakens) + "  \xE2\x80\xA2  Sunk: " + std::to_string(g.stats.timesSunk) + "\n" +
        "Crew: " + std::to_string(g.crewCount()) + "\n\n";

    const Event& e = g.event;
    if (e.isCombat()) {
        body += "\xE2\x9A\x94\xEF\xB8\x8F **" + eventName(e.kind) + "!** " + e.foe.name + ": " + bar(e.foe.hp, e.foe.maxHp) +
                "\nIt ends " + when(e.deadline) + ". `/fire` `/repair` `/flee`";
    } else if (e.kind == EventKind::Storm || e.kind == EventKind::Fire) {
        body += std::string(e.kind == EventKind::Storm ? "\xF0\x9F\x8C\x8A **Storm!** Water in the hold: " : "\xF0\x9F\x94\xA5 **Fire aboard!** Flames: ") +
                bar(e.severity, e.startingSeverity) + "\nIt ends " + when(e.deadline) + ". " +
                (e.kind == EventKind::Storm ? "`/bail`" : "`/douse`");
    } else if (e.kind == EventKind::Wreckage) {
        body += "\xF0\x9F\xAA\xB5 **Wreckage!** " + std::to_string(e.severity) + " wood and " + std::to_string(e.goldPool) +
                " gold still floating. Drifts away " + when(e.deadline) + ". `/salvage`";
    } else if (g.sailing) {
        body += "\xE2\x9B\xB5 Sailing calm seas. Keep a weather eye out...";
    } else {
        body += "\xE2\x9A\x93 At anchor. Nothing will happen until someone uses `/sail`.";
        if (s.getHp() < s.getMaxHp()) body += " The carpenter is slowly patching the hull meanwhile.";
    }

    if (g.nextRaidAt > c.now) body += "\nNext island raid possible " + when(g.nextRaidAt) + ".";

    return reply(s.getName(), body, e.active() ? Tone::Alert : Tone::Info);
}

Reply Game::crewBoard(GuildState& g) {
    std::vector<std::pair<uint64_t, const CrewMember*>> rows;
    for (const auto& [id, m] : g.crew) {
        if (m.aboard) rows.push_back({ id, &m });
    }
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second->points > b.second->points; });

    if (rows.empty()) return reply("Crew of " + g.ship.getName(), "Nobody's aboard yet! `/join` to sign on.");

    std::string body;
    int rank = 0;
    for (const auto& [id, m] : rows) {
        if (++rank > 15) break;
        body += std::to_string(rank) + ". " + bold(m->name) + " - " + withCommas(m->points) + " pts  \xC2\xB7  " +
                std::to_string(m->hits) + "/" + std::to_string(m->shots) + " hits, " + withCommas(m->damage) + " dmg, " +
                std::to_string(m->repairs) + " repairs, " + std::to_string(m->hazardWork) + " hazard, " +
                withCommas(m->plunder) + " gold looted\n";
    }
    if (rows.size() > 15) body += "...and " + std::to_string(rows.size() - 15) + " more hands.";
    return reply("Crew of " + g.ship.getName() + " (" + std::to_string(rows.size()) + ")", body);
}

Reply Game::leaderboard(const std::string& categoryArg, uint64_t guildId) {
    std::string category = categoryArg.empty() ? "score" : categoryArg;

    struct Row { uint64_t guildId; std::string ship; std::string server; double value; };
    std::vector<Row> rows;
    for (const auto& [id, g] : guilds) {
        if (!g.configured()) continue;
        double v = category == "gold"        ? g.ship.getGold()
                 : category == "firepower"   ? g.ship.firepower()
                 : category == "victories"   ? g.stats.victories
                 : category == "crew"        ? g.crewCount()
                                             : static_cast<double>(g.score());
        rows.push_back({ id, g.ship.getName(), g.guildName.empty() ? "Unknown waters" : g.guildName, v });
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.value > b.value; });

    auto show = [&](double v) { return category == "firepower" ? oneDecimal(v) : withCommas(static_cast<long long>(v)); };
    static const char* medals[] = { "\xF0\x9F\xA5\x87", "\xF0\x9F\xA5\x88", "\xF0\x9F\xA5\x89" };

    std::string body;
    for (size_t i = 0; i < rows.size() && i < 10; i++) {
        std::string place = i < 3 ? medals[i] : "`#" + std::to_string(i + 1) + "`";
        body += place + " " + bold(rows[i].ship) + " of *" + rows[i].server + "* - " + show(rows[i].value) +
                (rows[i].guildId == guildId ? "  \xE2\x86\x90 you" : "") + "\n";
    }
    if (rows.empty()) body = "No ships on the seas yet!";
    for (size_t i = 10; i < rows.size(); i++) {
        if (rows[i].guildId == guildId) body += "...\n`#" + std::to_string(i + 1) + "` " + bold(rows[i].ship) + " - " + show(rows[i].value) + "  \xE2\x86\x90 you\n";
    }

    std::string title = category == "gold" ? "Richest ships (gold in the hold)"
                      : category == "firepower" ? "Most firepower (damage per broadside)"
                      : category == "victories" ? "Most victories"
                      : category == "crew" ? "Biggest crews"
                      : "Top ships by score";
    body += "\n*Score = lifetime gold + 150/victory + 300/kraken + 50/hazard weathered + 25/raid - 200/time sunk*";
    return reply("\xF0\x9F\x8F\x86 " + title, body, Tone::Info);
}

Reply Game::shop(GuildState& g) {
    const Ship& s = g.ship;
    std::string body = "Gold in the hold: " + bold(withCommas(s.getGold())) + "\n\n";
    body += "`/buy wood <amount>` - " + std::to_string(Tuning::WOOD_PRICE) + " gold each (you have " + std::to_string(s.getWood()) + ")\n";

    switch (s.getHull()) {
    case Hull::Wood: body += "`/buy hull` - Steel hull (200 HP, AC 15): " + withCommas(Tuning::STEEL_HULL_PRICE) + " gold\n"; break;
    case Hull::Steel: body += "`/buy hull` - Crystal hull (300 HP, AC 18): " + withCommas(Tuning::CRYSTAL_HULL_PRICE) + " gold\n"; break;
    case Hull::Crystal: body += "Hull: Crystal - the finest afloat\n"; break;
    }
    switch (s.getCannons()) {
    case Cannons::Iron: body += "`/buy cannons` - Silver cannons (+4 to hit, d40): " + withCommas(Tuning::SILVER_CANNON_PRICE) + " gold\n"; break;
    case Cannons::Silver: body += "`/buy cannons` - Gold cannons (+6 to hit, d60): " + withCommas(Tuning::GOLD_CANNON_PRICE) + " gold\n"; break;
    case Cannons::Gold: body += "Cannons: Gold - nothing better\n"; break;
    }
    if (s.getGuns() < MAX_GUNS) {
        body += "`/buy gun` - gun #" + std::to_string(s.getGuns() + 1) + ": " + withCommas(Tuning::GUN_PRICE_PER_GUN * s.getGuns()) + " gold\n";
    } else {
        body += "Guns: " + std::to_string(MAX_GUNS) + " - every port is full\n";
    }
    return reply("\xF0\x9F\x8F\xAA Port shop", body);
}

Reply Game::rename(GuildState& g, const std::string& raw) {
    std::string name = cleanName(raw);
    if (name.empty()) return privateNote("That name won't fit on the stern. Use letters, numbers and spaces.");
    std::string old = g.ship.getName();
    g.ship.setName(name);
    return reply("\xF0\x9F\x8E\xA8 A new name!", bold(old) + " shall henceforth be known as " + bold(name) + ".", Tone::Good);
}

// ============================================================================
// Sailing commands
// ============================================================================

Reply Game::sail(GuildState& g, const Ctx& c) {
    if (g.sailing) return privateNote("We're already under sail!");
    const Ship& s = g.ship;
    if (s.getHp() * 100 < s.getMaxHp() * Tuning::MIN_SAIL_HP_PERCENT) {
        return privateNote("She's in no state to sail (" + std::to_string(s.getHp()) + "/" + std::to_string(s.getMaxHp()) +
                           " HP). `/repair` her first, or wait - the carpenter patches her up while we're at anchor.");
    }
    g.sailing = true;
    g.ignoredEvents = 0;
    g.nextDockRepairAt = 0;
    g.nextEventAt = c.now + rng.range(Tuning::FIRST_EVENT_MIN, Tuning::FIRST_EVENT_MAX);
    return reply("\xE2\x9B\xB5 Anchors aweigh!", bold(c.userName) + " raises the anchor. " + g.ship.getName() +
                 " heads for open water - stay sharp, crew.", Tone::Good);
}

Reply Game::anchor(GuildState& g, const Ctx& c) {
    if (!g.sailing) return privateNote("We're already at anchor.");
    if (g.event.active()) return privateNote("Can't drop anchor now - deal with the " + eventName(g.event.kind) + " first!");
    g.sailing = false;
    return reply("\xE2\x9A\x93 Anchor dropped", bold(c.userName) + " drops anchor in a quiet cove. Nothing will happen until someone uses `/sail`.");
}

Reply Game::hunt(GuildState& g, const Ctx& c) {
    if (g.event.active()) return privateNote("We've got our hands full already! (" + eventName(g.event.kind) + ")");
    if (!g.sailing) return privateNote("We're at anchor. `/sail` first.");
    member(g, c).points += 1;
    Reply r = beginEvent(g, EventKind::EnemyShip, c.now);
    r.body = bold(c.userName) + " spots sails on the horizon and gives chase!\n\n" + r.body;
    return r;
}

Reply Game::raid(GuildState& g, const Ctx& c) {
    if (g.event.active()) return privateNote("We've got our hands full already! (" + eventName(g.event.kind) + ")");
    if (!g.sailing) return privateNote("We're at anchor. `/sail` first.");
    if (g.nextRaidAt > c.now) return privateNote("The nearby islands are picked clean. Next raid possible " + when(g.nextRaidAt) + ".");

    g.nextRaidAt = c.now + Tuning::RAID_COOLDOWN;
    CrewMember& m = member(g, c);

    // Same idea as the original /loot: sometimes the island is guarded
    if (rng.chance(35)) {
        Reply r = beginEvent(g, EventKind::EnemyShip, c.now);
        r.title = "\xE2\x9A\x94\xEF\xB8\x8F Ambush!";
        r.body = bold(c.userName) + " leads a landing party - but the cove was a trap!\n\n" + r.body;
        return r;
    }

    int gold = rng.range(25, 80) + 20 * tier(g.ship.getHull());
    int wood = rng.range(0, 4);
    g.ship.addGold(gold);
    g.ship.addWood(wood);
    g.stats.lifetimeGold += gold;
    g.stats.raids++;
    m.plunder += gold;
    m.points += 3;
    return reply("\xF0\x9F\x8F\x9D\xEF\xB8\x8F Island raided!", bold(c.userName) + " leads a landing party and comes back with " +
                 bold(std::to_string(gold) + " gold") + (wood > 0 ? " and " + bold(std::to_string(wood) + " wood") : "") + ".", Tone::Good);
}

// ============================================================================
// Event-response commands
// ============================================================================

Reply Game::fire(GuildState& g, const Ctx& c) {
    Event& e = g.event;
    if (!e.isCombat()) return privateNote("Nobody to shoot at! Use `/hunt` to go looking for a fight.");
    if (auto cd = onCooldown(c, Action::Fire, Tuning::CD_FIRE)) return *cd;

    e.crewResponded = true;
    Ship::Broadside b = g.ship.fireBroadside(rng, e.foe.ac);
    e.foe.hp -= b.damage;

    CrewMember& m = member(g, c);
    m.shots += b.shots;
    m.hits += b.hits;
    m.damage += b.damage;
    m.points += 1 + b.damage / 10;

    std::string body = bold(c.userName) + " fires a broadside: " + std::to_string(b.hits) + "/" + std::to_string(b.shots) +
                       " shots hit" + (b.damage > 0 ? " for " + bold(std::to_string(b.damage) + " damage") : "") + "!\n";
    if (e.foe.hp <= 0) {
        m.points += 10;
        body += "\n" + winFight(g, c.now);
        return reply("\xF0\x9F\x92\xA5 Direct hit!", body, Tone::Good);
    }
    body += e.foe.name + ": " + bar(e.foe.hp, e.foe.maxHp);
    return reply(b.hits > 0 ? "\xF0\x9F\x92\xA5 Broadside!" : "\xF0\x9F\x92\xA8 Missed!", body, b.hits > 0 ? Tone::Good : Tone::Info);
}

Reply Game::flee(GuildState& g, const Ctx& c) {
    Event& e = g.event;
    if (!e.isCombat()) return privateNote("Not in a fight - nothing to run from.");
    if (auto cd = onCooldown(c, Action::Flee, Tuning::CD_FLEE)) return *cd;
    e.crewResponded = true;

    if (e.kind == EventKind::Merchant) {
        std::string text = bold(c.userName) + " lets " + e.foe.name + " sail on unmolested.\n" + endEvent(g, c.now);
        return reply("\xF0\x9F\x91\x8B Let them go", text);
    }

    int odds = e.kind == EventKind::Kraken ? 40 : 50;
    if (rng.chance(odds)) {
        std::string text = bold(c.userName) + " catches the wind and we slip away from " + e.foe.name + "!\n" + endEvent(g, c.now);
        return reply("\xF0\x9F\x92\xA8 Got away!", text, Tone::Good);
    }
    std::string text = bold(c.userName) + " tries to run, but " + e.foe.name + " cuts us off!\n" + foeVolley(g, c.now);
    return reply("\xE2\x9D\x8C Couldn't get away!", text, Tone::Bad);
}

Reply Game::repair(GuildState& g, const Ctx& c) {
    Ship& s = g.ship;
    if (s.getHp() >= s.getMaxHp()) return privateNote("The hull's already in top shape!");
    if (s.getWood() <= 0) return privateNote("We're out of wood! `/buy wood` in port or `/salvage` some.");
    if (auto cd = onCooldown(c, Action::Repair, Tuning::CD_REPAIR)) return *cd;

    int healed = s.repair();
    CrewMember& m = member(g, c);
    m.repairs++;
    m.points += 2;
    if (g.event.active()) g.event.crewResponded = true;
    return reply("\xF0\x9F\x94\xA8 Repairs", bold(c.userName) + " patches the hull (+" + std::to_string(healed) + " HP).\nHull: " +
                 bar(s.getHp(), s.getMaxHp()) + "  \xE2\x80\xA2  Wood left: " + std::to_string(s.getWood()), Tone::Good);
}

Reply Game::fightHazard(GuildState& g, const Ctx& c, EventKind kind) {
    Event& e = g.event;
    if (e.kind != kind) {
        return privateNote(kind == EventKind::Storm ? "The hold is dry - no water to bail." : "Nothing's on fire. Yet.");
    }
    if (auto cd = onCooldown(c, Action::Hazard, Tuning::CD_HAZARD)) return *cd;

    e.crewResponded = true;
    int amount = rng.range(4, 8);
    e.severity = std::max(0, e.severity - amount);
    CrewMember& m = member(g, c);
    m.hazardWork++;
    m.points += 2;

    bool storm = kind == EventKind::Storm;
    std::string body = bold(c.userName) + (storm ? " bails furiously" : " hauls buckets onto the flames") +
                       " (-" + std::to_string(amount) + ").\n";
    if (e.severity <= 0) {
        int bonus = rng.range(15, 40);
        g.ship.addGold(bonus);
        g.stats.lifetimeGold += bonus;
        g.stats.hazardsWeathered++;
        body += storm ? "\nThe storm breaks and the sun comes out! " : "\nThe last ember hisses out! ";
        body += "Tidying up, the crew finds " + std::to_string(bonus) + " gold that rolled under the barrels.\n" + endEvent(g, c.now);
        return reply(storm ? "\xE2\x98\x80\xEF\xB8\x8F Storm weathered!" : "\xF0\x9F\x9A\x92 Fire's out!", body, Tone::Good);
    }
    body += (storm ? "Water in the hold: " : "Flames: ") + bar(e.severity, e.startingSeverity);
    return reply(storm ? "\xF0\x9F\xAA\xA3 Bailing!" : "\xF0\x9F\x92\xA7 Dousing!", body);
}

Reply Game::salvage(GuildState& g, const Ctx& c) {
    Event& e = g.event;
    if (e.kind != EventKind::Wreckage) return privateNote("Nothing floating nearby worth grabbing.");
    if (auto cd = onCooldown(c, Action::Salvage, Tuning::CD_SALVAGE)) return *cd;

    e.crewResponded = true;
    int wood = std::min(e.severity, rng.range(1, 3));
    int gold = std::min(e.goldPool, rng.range(5, 20));
    e.severity -= wood;
    e.goldPool -= gold;
    g.ship.addWood(wood);
    g.ship.addGold(gold);
    g.stats.lifetimeGold += gold;

    CrewMember& m = member(g, c);
    m.plunder += gold;
    m.points += 1 + (gold + wood * 5) / 10;

    std::string body = bold(c.userName) + " fishes out " + std::to_string(wood) + " wood" +
                       (gold > 0 ? " and " + std::to_string(gold) + " gold" : "") + ".\n";
    if (e.severity <= 0 && e.goldPool <= 0) {
        body += "That's the last of it - picked clean!\n" + endEvent(g, c.now);
        return reply("\xF0\x9F\xAA\xB5 Salvage complete", body, Tone::Good);
    }
    body += "Still floating: " + std::to_string(e.severity) + " wood, " + std::to_string(e.goldPool) + " gold.";
    return reply("\xF0\x9F\xAA\xB5 Salvaging", body, Tone::Good);
}

Reply Game::buy(GuildState& g, const Ctx& c, const std::string& item, int64_t amount) {
    if (g.event.active()) return privateNote("No shopping in the middle of a " + eventName(g.event.kind) + "!");
    Ship& s = g.ship;

    auto afford = [&](int price) -> std::optional<Reply> {
        if (s.getGold() < price) {
            return privateNote("That costs " + withCommas(price) + " gold, but we only have " + withCommas(s.getGold()) + ".");
        }
        return std::nullopt;
    };
    auto bought = [&](int price, const std::string& what) {
        s.addGold(-price);
        member(g, c).points += 1;
        return reply("\xF0\x9F\x9B\x92 Purchase", bold(c.userName) + " buys " + what + " for " + withCommas(price) +
                     " gold. " + withCommas(s.getGold()) + " gold left.", Tone::Good);
    };

    if (item == "wood") {
        int qty = static_cast<int>(std::clamp<int64_t>(amount <= 0 ? 5 : amount, 1, 100));
        int price = qty * Tuning::WOOD_PRICE;
        if (auto no = afford(price)) return *no;
        s.addWood(qty);
        return bought(price, std::to_string(qty) + " wood");
    }
    if (item == "hull") {
        if (s.getHull() == Hull::Crystal) return privateNote("We already have the best hull there is.");
        Hull next = hullFromTier(tier(s.getHull()) + 1);
        int price = next == Hull::Steel ? Tuning::STEEL_HULL_PRICE : Tuning::CRYSTAL_HULL_PRICE;
        if (auto no = afford(price)) return *no;
        s.setHull(next);
        return bought(price, "a " + toString(next) + " hull");
    }
    if (item == "cannons") {
        if (s.getCannons() == Cannons::Gold) return privateNote("Our cannons are already solid gold.");
        Cannons next = cannonsFromTier(tier(s.getCannons()) + 1);
        int price = next == Cannons::Silver ? Tuning::SILVER_CANNON_PRICE : Tuning::GOLD_CANNON_PRICE;
        if (auto no = afford(price)) return *no;
        s.setCannons(next);
        return bought(price, toString(next) + " cannons");
    }
    if (item == "gun") {
        if (s.getGuns() >= MAX_GUNS) return privateNote("Every gun port is already full.");
        int price = Tuning::GUN_PRICE_PER_GUN * s.getGuns();
        if (auto no = afford(price)) return *no;
        s.setGuns(s.getGuns() + 1);
        return bought(price, "gun #" + std::to_string(s.getGuns()));
    }
    return privateNote("The shopkeeper squints. Choose wood, hull, cannons or gun.");
}

// ============================================================================
// The event engine
// ============================================================================

EventKind Game::rollEventKind() {
    // Weighted table: fights are most common, krakens rare
    struct Weight { EventKind kind; int weight; };
    static const Weight table[] = {
        { EventKind::EnemyShip, 30 }, { EventKind::Merchant, 18 }, { EventKind::Storm, 16 },
        { EventKind::Wreckage, 16 },  { EventKind::Fire, 12 },     { EventKind::Kraken, 8 },
    };
    int total = 0;
    for (const auto& w : table) total += w.weight;
    int roll = rng.range(1, total);
    for (const auto& w : table) {
        roll -= w.weight;
        if (roll <= 0) return w.kind;
    }
    return EventKind::EnemyShip;
}

Reply Game::beginEvent(GuildState& g, EventKind kind, int64_t now) {
    Event e;
    e.kind = kind;
    e.startedAt = now;
    const Ship& s = g.ship;
    int hullTier = tier(s.getHull());

    Reply r;
    r.pingCrew = true;
    r.tone = Tone::Alert;

    switch (kind) {
    case EventKind::EnemyShip: {
        Ship enemy = Ship::randomEnemy(rng, s);
        e.foe.name = rng.pick(ENEMY_NAMES);
        e.foe.maxHp = e.foe.hp = enemy.getMaxHp();
        e.foe.ac = enemy.getAc();
        e.foe.hitBonus = hitBonus(enemy.getCannons());
        e.foe.damageDie = damageDie(enemy.getCannons());
        e.foe.guns = enemy.getGuns();
        e.foe.goldReward = 60 + 50 * (tier(enemy.getHull()) + tier(enemy.getCannons())) + 15 * enemy.getGuns() + rng.range(0, 40);
        e.foe.woodReward = rng.range(2, 6);
        e.deadline = now + Tuning::COMBAT_DURATION;
        e.nextPulseAt = now + Tuning::COMBAT_FIRST_VOLLEY;
        r.title = "\xE2\x9A\x94\xEF\xB8\x8F Enemy attack!";
        r.body = "Pirates! **" + e.foe.name + "**, a " + enemy.info() + ", is bearing down on us!\n\n"
                 "`/fire` to fight back  \xE2\x80\xA2  `/repair` to patch holes  \xE2\x80\xA2  `/flee` to run\n"
                 "If they're still afloat " + when(e.deadline) + " they'll board us and plunder the hold.";
        break;
    }
    case EventKind::Merchant:
        e.foe.name = rng.pick(MERCHANT_NAMES);
        e.foe.maxHp = e.foe.hp = 60 + 40 * hullTier;
        e.foe.ac = 11;
        e.foe.hitBonus = 0;
        e.foe.damageDie = 8;
        e.foe.guns = 1;
        e.foe.goldReward = 150 + 75 * hullTier + rng.range(0, 100);
        e.foe.woodReward = rng.range(3, 8);
        e.deadline = now + Tuning::COMBAT_DURATION;
        e.nextPulseAt = now + Tuning::COMBAT_FIRST_VOLLEY;
        r.title = "\xF0\x9F\x92\xB0 Merchant sighted!";
        r.body = "A fat merchant ship, **" + e.foe.name + "**, wallows past loaded with cargo. Barely armed!\n\n"
                 "`/fire` to take her as a prize  \xE2\x80\xA2  `/flee` to let her go\n"
                 "She'll be over the horizon " + when(e.deadline) + ".";
        r.tone = Tone::Good;
        break;
    case EventKind::Kraken:
        e.foe.name = "the Kraken";
        e.foe.maxHp = e.foe.hp = 180 + 80 * hullTier;
        e.foe.ac = 13;
        e.foe.hitBonus = 3;
        e.foe.damageDie = 10 + 5 * hullTier;
        e.foe.guns = 3;
        e.foe.goldReward = 120 + rng.range(0, 80);
        e.foe.woodReward = 0;
        e.deadline = now + Tuning::COMBAT_DURATION;
        e.nextPulseAt = now + Tuning::COMBAT_FIRST_VOLLEY;
        r.title = "\xF0\x9F\x90\x99 KRAKEN!";
        r.body = "The sea boils and great tentacles wrap around the hull!\n\n"
                 "`/fire` point-blank  \xE2\x80\xA2  `/repair` what it crushes  \xE2\x80\xA2  `/flee` (it's hard to outrun)\n"
                 "If it's still clinging on " + when(e.deadline) + " it'll drag our cargo into the deep as it lets go.";
        break;
    case EventKind::Storm:
        e.startingSeverity = e.severity = rng.range(35, 55) + 10 * hullTier;
        e.deadline = now + Tuning::HAZARD_DURATION;
        e.nextPulseAt = now + Tuning::HAZARD_PULSE_EVERY;
        r.title = "\xF0\x9F\x8C\x8A Storm!";
        r.body = "Black clouds and towering waves - water is pouring into the hold!\n\n"
                 "`/bail` to pump it out. Every minute the water does damage, and whatever's left " + when(e.deadline) +
                 " hits us all at once.\nWater in the hold: " + bar(e.severity, e.startingSeverity);
        break;
    case EventKind::Fire:
        e.startingSeverity = e.severity = rng.range(30, 50) + 10 * hullTier;
        e.deadline = now + Tuning::HAZARD_DURATION;
        e.nextPulseAt = now + Tuning::HAZARD_PULSE_EVERY;
        r.title = "\xF0\x9F\x94\xA5 Fire aboard!";
        r.body = "A lantern tipped over in the hold and the fire is spreading!\n\n"
                 "`/douse` to put it out. It burns the hull and our wood supply every minute until it's out or burns itself out " +
                 when(e.deadline) + ".\nFlames: " + bar(e.severity, e.startingSeverity);
        break;
    case EventKind::Wreckage:
        e.startingSeverity = e.severity = rng.range(8, 16);
        e.goldPool = rng.range(30, 90);
        e.deadline = now + Tuning::BOUNTY_DURATION;
        e.nextPulseAt = e.deadline;
        r.title = "\xF0\x9F\xAA\xB5 Wreckage ahoy!";
        r.body = "Some poor ship's wreckage is floating past - " + std::to_string(e.severity) + " planks and a few strongboxes!\n\n"
                 "`/salvage` to haul it aboard before it drifts off " + when(e.deadline) + ".";
        r.tone = Tone::Good;
        break;
    case EventKind::None:
        break;
    }

    g.event = e;
    return r;
}

// Enemy shoots at us. Returns a line describing it (and the sinking, if that happens).
std::string Game::foeVolley(GuildState& g, int64_t now) {
    Foe& f = g.event.foe;
    int hits = 0, damage = 0;
    for (int i = 0; i < f.guns; i++) {
        if (rng.die(20) + f.hitBonus >= g.ship.getAc()) {
            hits++;
            damage += rng.die(f.damageDie);
        }
    }
    damage = g.ship.takeDamage(damage);
    bool kraken = g.event.kind == EventKind::Kraken;

    std::string text = f.name + (kraken ? " lashes out: " : " fires: ") + std::to_string(hits) + "/" + std::to_string(f.guns) +
                       (kraken ? " tentacles connect" : " shots hit") + (damage > 0 ? " for " + bold(std::to_string(damage) + " damage") : "") + ".\n" +
                       "Our hull: " + bar(g.ship.getHp(), g.ship.getMaxHp()) + "\n";
    if (g.ship.sunk()) text += "\n" + sink(g, now);
    return text;
}

std::string Game::winFight(GuildState& g, int64_t now) {
    Event& e = g.event;
    g.ship.addGold(e.foe.goldReward);
    g.ship.addWood(e.foe.woodReward);
    g.stats.lifetimeGold += e.foe.goldReward;
    if (e.kind == EventKind::Kraken) g.stats.krakens++;
    else g.stats.victories++;

    std::string text = e.kind == EventKind::Kraken ? "\xF0\x9F\x90\x99 The Kraken shrieks and sinks back into the deep!"
                     : e.kind == EventKind::Merchant ? "\xF0\x9F\x8F\xB3\xEF\xB8\x8F " + e.foe.name + " strikes her colours - she's ours!"
                                                     : "\xE2\x98\xA0\xEF\xB8\x8F " + e.foe.name + " goes down in flames!";
    text += "\nPlunder: " + bold(std::to_string(e.foe.goldReward) + " gold") +
            (e.foe.woodReward > 0 ? " and " + bold(std::to_string(e.foe.woodReward) + " wood") : "") + ".\n";
    return text + endEvent(g, now);
}

// Our ship goes down. Lose a chunk of the cargo, limp back to port at anchor.
std::string Game::sink(GuildState& g, int64_t now) {
    Ship& s = g.ship;
    int goldLost = s.getGold() * Tuning::SINK_GOLD_LOSS_PERCENT / 100;
    int woodLost = s.getWood() * Tuning::SINK_WOOD_LOSS_PERCENT / 100;
    s.addGold(-goldLost);
    s.addWood(-woodLost);
    s.setHp(s.getMaxHp() * 2 / 5);
    g.stats.timesSunk++;
    g.event = Event{};
    g.sailing = false;
    g.ignoredEvents = 0;
    g.nextEventAt = 0;
    g.nextDockRepairAt = now + Tuning::DOCK_REPAIR_EVERY;
    return "\xF0\x9F\x92\x80 **We're going down!** The crew scrambles into the boats. " + std::to_string(goldLost) +
           " gold and " + std::to_string(woodLost) + " wood sink with the hold.\n"
           "The ship is towed into port, patched to " + std::to_string(s.getHp()) + " HP and left at anchor. `/sail` when ready.";
}

// Clears the current event and schedules the next one. Anchors automatically if nobody's around.
std::string Game::endEvent(GuildState& g, int64_t now) {
    bool responded = g.event.crewResponded;
    g.event = Event{};
    g.nextEventAt = now + rng.range(Tuning::EVENT_GAP_MIN, Tuning::EVENT_GAP_MAX);

    g.ignoredEvents = responded ? 0 : g.ignoredEvents + 1;
    const Ship& s = g.ship;
    if (g.sailing && s.getHp() * 100 < s.getMaxHp() * Tuning::MIN_SAIL_HP_PERCENT) {
        g.sailing = false;
        return "\n\xE2\x9A\x93 Badly damaged, the ship limps into a quiet cove to patch up. `/repair` her (or let the carpenter work) and `/sail` again.";
    }
    if (g.sailing && g.ignoredEvents >= Tuning::AUTO_ANCHOR_AFTER) {
        g.sailing = false;
        g.ignoredEvents = 0;
        return "\n\xE2\x9A\x93 Nobody answered the last " + std::to_string(Tuning::AUTO_ANCHOR_AFTER) +
               " alarms, so the ship drops anchor to stay safe. Use `/sail` when the crew is back.";
    }
    return "";
}

// Something happens mid-event: enemy volley, storm surge, fire spreading
std::optional<Reply> Game::pulse(GuildState& g, int64_t now) {
    Event& e = g.event;
    if (e.kind == EventKind::Wreckage) return std::nullopt;

    if (e.isCombat()) {
        bool kraken = e.kind == EventKind::Kraken;
        e.nextPulseAt = now + Tuning::COMBAT_VOLLEY_EVERY;
        std::string text = foeVolley(g, now);  // may sink us, which clears the event

        // Pirates (and the Kraken) want our cargo, not a wreck: once we're badly holed they stop
        // attacking and take what they can. That's a loss, but it means one missed alarm can't sink us.
        const Ship& s = g.ship;
        if (e.kind != EventKind::Merchant && g.event.active() && s.getHp() * 100 <= s.getMaxHp() * Tuning::BOARD_BELOW_PERCENT) {
            Reply taken = timeout(g, now);
            taken.body = text + "\nWith our hull in splinters, they close in...\n\n" + taken.body;
            return taken;
        }
        if (g.event.active()) text += "Enemy: " + bar(e.foe.hp, e.foe.maxHp) + "  \xE2\x80\xA2  ends " + when(e.deadline);
        return reply(kraken ? "\xF0\x9F\x90\x99 The Kraken strikes!" : "\xF0\x9F\x92\xA3 Incoming fire!", text, Tone::Bad);
    }

    // Storm / fire
    e.nextPulseAt = now + Tuning::HAZARD_PULSE_EVERY;
    bool storm = e.kind == EventKind::Storm;
    int damage = g.ship.takeDamage(std::max(1, e.severity / 8));
    int woodBurned = 0;
    if (!storm && g.ship.getWood() > 0) {
        woodBurned = 1;
        g.ship.addWood(-1);
    }

    std::string text = std::string(storm ? "A wave crashes over the deck" : "The fire spreads") + " (-" + std::to_string(damage) + " HP" +
                       (woodBurned ? ", 1 wood burned" : "") + ").\nOur hull: " + bar(g.ship.getHp(), g.ship.getMaxHp()) + "\n";
    if (g.ship.sunk()) {
        text += "\n" + sink(g, now);
    } else {
        text += (storm ? "Water: " : "Flames: ") + bar(e.severity, e.startingSeverity) + "  \xE2\x80\xA2  ends " + when(e.deadline) +
                "  \xE2\x80\xA2  " + (storm ? "`/bail`" : "`/douse`");
    }
    return reply(storm ? "\xF0\x9F\x8C\x8A The storm rages" : "\xF0\x9F\x94\xA5 The fire spreads", text, Tone::Bad);
}

// The event's time is up. The more the crew did, the less it hurts.
Reply Game::timeout(GuildState& g, int64_t now) {
    Event e = g.event;  // copy: sink()/endEvent() clear g.event
    Ship& s = g.ship;
    std::string text;
    Tone tone = Tone::Bad;
    std::string title;

    switch (e.kind) {
    case EventKind::EnemyShip: {
        // Plunder scales with how healthy the enemy still is
        double strength = static_cast<double>(e.foe.hp) / e.foe.maxHp;
        int goldLost = static_cast<int>(std::lround(s.getGold() * 0.30 * strength));
        int woodLost = static_cast<int>(std::lround(s.getWood() * 0.30 * strength));
        s.addGold(-goldLost);
        s.addWood(-woodLost);
        title = "\xF0\x9F\x8F\xB4\xE2\x80\x8D\xE2\x98\xA0\xEF\xB8\x8F Boarded!";
        text = e.foe.name + " grapples alongside and their crew swarms aboard. They make off with " + bold(std::to_string(goldLost) + " gold") +
               " and " + bold(std::to_string(woodLost) + " wood") + ".";
        if (strength < 0.5) text += "\nWe battered them badly, so they couldn't carry much.";
        text += endEvent(g, now);
        break;
    }
    case EventKind::Merchant:
        title = "\xF0\x9F\x8C\x85 She got away";
        text = e.foe.name + " disappears over the horizon with her cargo." + endEvent(g, now);
        tone = Tone::Info;
        break;
    case EventKind::Kraken: {
        double strength = static_cast<double>(e.foe.hp) / e.foe.maxHp;
        int goldLost = static_cast<int>(std::lround(s.getGold() * 0.25 * strength));
        int woodLost = static_cast<int>(std::lround(s.getWood() * 0.40 * strength));
        s.addGold(-goldLost);
        s.addWood(-woodLost);
        title = "\xF0\x9F\x90\x99 The Kraken lets go";
        text = "The Kraken loses interest - but its tentacles drag " + bold(std::to_string(goldLost) + " gold") + " and " +
               bold(std::to_string(woodLost) + " wood") + " off the deck and into the deep.\nOur hull: " +
               bar(s.getHp(), s.getMaxHp()) + endEvent(g, now);
        break;
    }
    case EventKind::Storm:
    case EventKind::Fire: {
        bool storm = e.kind == EventKind::Storm;
        int damage = s.takeDamage(e.severity / 4);
        int woodLost = storm ? 0 : std::min(s.getWood(), e.severity / 10);
        s.addWood(-woodLost);
        title = storm ? "\xF0\x9F\x8C\xA5\xEF\xB8\x8F The storm passes" : "\xF0\x9F\x94\xA5 The fire burns out";
        text = std::string(storm ? "The last of the storm" : "The fire") + " does " + std::to_string(damage) + " damage on its way out" +
               (woodLost ? " and burns " + std::to_string(woodLost) + " wood" : "") + ".\n";
        text += s.sunk() ? sink(g, now) : "Our hull: " + bar(s.getHp(), s.getMaxHp()) + endEvent(g, now);
        break;
    }
    case EventKind::Wreckage:
        title = "\xF0\x9F\x8C\x8A Drifted away";
        text = "The rest of the wreckage drifts out of reach." + endEvent(g, now);
        tone = Tone::Info;
        break;
    case EventKind::None:
        break;
    }
    return reply(title, text, tone);
}

std::vector<Announcement> Game::tick(int64_t now) {
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<Announcement> out;
    bool changed = false;

    for (auto& [id, g] : guilds) {
        if (!g.configured()) continue;
        std::optional<Reply> msg;

        // Resting at anchor: the carpenter slowly patches the hull (quietly, no message)
        Ship& s = g.ship;
        if (!g.sailing && !g.event.active() && s.getHp() < s.getMaxHp()) {
            if (g.nextDockRepairAt == 0) {
                g.nextDockRepairAt = now + Tuning::DOCK_REPAIR_EVERY;
            } else if (now >= g.nextDockRepairAt) {
                s.setHp(s.getHp() + std::max(1, s.getMaxHp() * Tuning::DOCK_REPAIR_PERCENT / 100));
                g.nextDockRepairAt = now + Tuning::DOCK_REPAIR_EVERY;
                changed = true;
            }
        }

        if (g.event.active()) {
            if (now >= g.event.deadline) msg = timeout(g, now);
            else if (now >= g.event.nextPulseAt) msg = pulse(g, now);
        } else if (g.sailing && g.nextEventAt > 0 && now >= g.nextEventAt) {
            msg = beginEvent(g, rollEventKind(), now);
        }

        if (msg) {
            changed = true;
            out.push_back({ id, g.channelId, g.roleId, *msg });
        }
    }

    if (changed) saveLocked();
    return out;
}

// ============================================================================
// Test helpers
// ============================================================================

GuildState Game::snapshot(uint64_t guildId) {
    std::lock_guard<std::mutex> lock(mutex);
    return guild(guildId);
}

void Game::edit(uint64_t guildId, const std::function<void(GuildState&)>& fn) {
    std::lock_guard<std::mutex> lock(mutex);
    fn(guild(guildId));
}

Announcement Game::startEvent(uint64_t guildId, EventKind kind, int64_t now) {
    std::lock_guard<std::mutex> lock(mutex);
    GuildState& g = guild(guildId);
    Reply r = beginEvent(g, kind, now);
    return { guildId, g.channelId, g.roleId, r };
}
