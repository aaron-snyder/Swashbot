// File: Game.cpp
// Swashbot game rules: commands, random events, the economy, ship naming and the leaderboards.
//
// Message style: every message is a short title, one line saying what happened, and (if needed)
// one line with a status bar or the commands to use next. Errors are a single private sentence.

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

const std::string DOT = " · ";

// Discord renders <t:UNIX:R> as a live countdown ("in 3 minutes") in each user's own timezone
std::string when(int64_t unixTime) { return "<t:" + std::to_string(unixTime) + ":R>"; }

std::string channelLink(uint64_t channelId) { return "<#" + std::to_string(channelId) + ">"; }

std::string num(long long n) {
    std::string digits = std::to_string(n < 0 ? -n : n);
    std::string out;
    for (size_t i = 0; i < digits.size(); i++) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return (n < 0 ? "-" : "") + out;
}

// `██████░░░░` 60/100
std::string bar(int value, int max) {
    const int width = 10;
    if (max <= 0) max = 1;
    int filled = static_cast<int>(std::lround(static_cast<double>(std::clamp(value, 0, max)) / max * width));
    std::string out;
    for (int i = 0; i < width; i++) out += (i < filled ? "█" : "░");
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

// "+12 gold · +3 wood", skipping zeros
std::string loot(int gold, int wood, const char* sign = "+") {
    std::string out;
    if (gold > 0) out += sign + num(gold) + " gold";
    if (wood > 0) out += (out.empty() ? "" : DOT) + sign + num(wood) + " wood";
    return out.empty() ? "nothing" : out;
}

Reply reply(const std::string& title, const std::string& body, Tone tone = Tone::Info) {
    Reply r;
    r.title = title;
    r.body = body;
    while (!r.body.empty() && r.body.back() == '\n') r.body.pop_back();
    r.tone = tone;
    return r;
}

// Errors and "not right now" messages: one sentence, only the person who asked sees it
Reply note(const std::string& text) {
    Reply r;
    r.body = text;
    r.ephemeral = true;
    return r;
}

const char* NOT_SET_UP = "Not set up yet. Swashbot needs **Manage Channels** and **Manage Roles**, then an admin runs `/setup`.";
const char* NOT_CREW = "Join the crew first: `/join`";
const char* BUSY = "Busy with an event right now.";

const std::array<const char*, 12> ENEMY_NAMES = {
    "the Rusty Gull", "the Black Wake", "the Widow's Lantern", "the Salt Ghost",
    "the Barnacle Queen", "the Drowned Fiddle", "the Gilded Eel", "the Squall Hound",
    "the Iron Petrel", "the Moray's Grin", "the Tattered Saint", "the Cutthroat Clam",
};

const std::array<const char*, 6> MERCHANT_NAMES = {
    "the Fat Abbot", "the Spice Countess", "the Velvet Ledger",
    "the Golden Ewe", "the Lazy Tortoise", "the Silk Hen",
};

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

int GuildState::votesNeeded() const { return std::max(1, (crewCount() + 1) / 2); }

int GuildState::ayesFromCrew() const {
    return static_cast<int>(std::count_if(nameVote.ayes.begin(), nameVote.ayes.end(), [this](uint64_t id) {
        auto it = crew.find(id);
        return it != crew.end() && it->second.aboard;
    }));
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

    return reply("🏴‍☠️ All aboard",
                 "This is the ship's deck. When an alert pops up, answer it with commands before time runs out.\n"
                 "`/help` for commands.", Tone::Good);
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
    if (it == guilds.end() || !it->second.configured()) return note(NOT_SET_UP);
    auto m = it->second.crew.find(c.userId);
    if (m != it->second.crew.end() && m->second.aboard) {
        return note("You're already crew. Head to " + channelLink(it->second.channelId) + ".");
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
    Reply r = note(std::string(returning ? "Welcome back! " : "Welcome aboard! ") + "Head to " + channelLink(g.channelId) + ".");
    r.tone = Tone::Good;
    return r;
}

std::optional<Reply> Game::checkLeave(const Ctx& c) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = guilds.find(c.guildId);
    if (it == guilds.end() || !it->second.configured()) return note(NOT_SET_UP);
    auto m = it->second.crew.find(c.userId);
    if (m == it->second.crew.end() || !m->second.aboard) return note("You're not in the crew.");
    return std::nullopt;
}

Reply Game::leave(const Ctx& c) {
    std::lock_guard<std::mutex> lock(mutex);
    GuildState& g = guild(c.guildId);
    g.crew[c.userId].aboard = false;
    saveLocked();
    return note("You left the crew. Your stats are saved if you come back.");
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
    if (it == guilds.end() || !it->second.configured()) return note(NOT_SET_UP);
    GuildState& g = it->second;

    // Read-only commands: anyone can look, but outside the ship channel only they see the answer
    if (name == "status" || name == "crew" || name == "shop") {
        Reply r = name == "status" ? status(g, c) : name == "crew" ? crewBoard(g) : shop(g);
        if (c.channelId != g.channelId) r.ephemeral = true;
        return r;
    }

    // Everything else is running the ship: crew only, and only on deck
    auto m = g.crew.find(c.userId);
    if (m == g.crew.end() || !m->second.aboard) return note(NOT_CREW);
    if (c.channelId != g.channelId) return note("Use this in " + channelLink(g.channelId) + ".");

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
    else return note("Unknown command.");

    saveLocked();
    return r;
}

std::optional<Reply> Game::onCooldown(const Ctx& c, Action a, int64_t seconds) {
    auto key = std::make_tuple(c.guildId, c.userId, static_cast<int>(a));
    auto it = cooldowns.find(key);
    if (it != cooldowns.end() && it->second > c.now) return note("Ready again " + when(it->second) + ".");
    cooldowns[key] = c.now + seconds;
    return std::nullopt;
}

// ============================================================================
// Read-only commands
// ============================================================================

Reply Game::help() {
    Reply r = reply("🏴‍☠️ Commands",
        "**Crew** `/join` `/leave` `/status` `/crew` `/leaderboard`\n"
        "**Sail** `/sail` `/anchor` `/hunt` `/raid`\n"
        "**Act** `/fire` `/flee` `/repair` `/bail` `/douse` `/salvage`\n"
        "**Ship** `/shop` `/buy` `/name`");
    r.footer = "Admins: /setup repairs the channel · /name renames instantly";
    r.ephemeral = true;
    return r;
}

Reply Game::status(GuildState& g, const Ctx& c) {
    const Ship& s = g.ship;
    const Event& e = g.event;

    std::string state;
    if (e.isCombat()) {
        state = "⚔️ " + bold(e.foe.name) + " " + bar(e.foe.hp, e.foe.maxHp) + DOT + "ends " + when(e.deadline);
    } else if (e.kind == EventKind::Storm) {
        state = "🌊 Water " + bar(e.severity, e.startingSeverity) + DOT + "`/bail`";
    } else if (e.kind == EventKind::Fire) {
        state = "🔥 Flames " + bar(e.severity, e.startingSeverity) + DOT + "`/douse`";
    } else if (e.kind == EventKind::Wreckage) {
        state = "🪵 " + loot(e.goldPool, e.severity, "") + " floating" + DOT + "`/salvage`";
    } else {
        state = g.sailing ? "⛵ Sailing" : "⚓ Anchored";
    }
    if (g.nameVote.open(c.now)) state += "\n🏷️ Name vote: " + bold(g.nameVote.name);

    Reply r = reply("🚢 " + s.getName(),
                    toString(s.getHull()) + " hull" + DOT + toString(s.getCannons()) + " cannons" + DOT +
                    std::to_string(s.getGuns()) + " guns\n" + bar(s.getHp(), s.getMaxHp()) + " HP\n" + state,
                    e.active() ? Tone::Alert : Tone::Info);
    r.fields = {
        { "Gold", num(s.getGold()) },
        { "Wood", num(s.getWood()) },
        { "Score", num(g.score()) },
        { "Firepower", oneDecimal(s.firepower()) },
        { "Victories", num(g.stats.victories) },
        { "Crew", num(g.crewCount()) },
    };
    return r;
}

Reply Game::crewBoard(GuildState& g) {
    std::vector<const CrewMember*> rows;
    for (const auto& [id, m] : g.crew) {
        if (m.aboard) rows.push_back(&m);
    }
    std::sort(rows.begin(), rows.end(), [](const CrewMember* a, const CrewMember* b) { return a->points > b->points; });
    if (rows.empty()) return reply("👥 Crew", "Nobody yet. `/join` to sign on.");

    std::string body;
    for (size_t i = 0; i < rows.size() && i < 15; i++) {
        body += std::to_string(i + 1) + ". " + bold(rows[i]->name) + DOT + num(rows[i]->points) + "\n";
    }
    if (rows.size() > 15) body += "+" + std::to_string(rows.size() - 15) + " more";
    Reply r = reply("👥 Crew" + DOT + std::to_string(rows.size()), body);
    r.footer = "Points for every action";
    return r;
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
        rows.push_back({ id, g.ship.getName(), g.guildName.empty() ? "?" : g.guildName, v });
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.value > b.value; });

    auto show = [&](double v) { return category == "firepower" ? oneDecimal(v) : num(static_cast<long long>(v)); };
    static const char* medals[] = { "🥇", "🥈", "🥉" };
    auto line = [&](size_t i) {
        std::string place = i < 3 ? medals[i] : "`" + std::to_string(i + 1) + ".`";
        return place + " " + bold(rows[i].ship) + DOT + show(rows[i].value) + DOT + "*" + rows[i].server + "*" +
               (rows[i].guildId == guildId ? " ◀" : "") + "\n";
    };

    std::string body;
    for (size_t i = 0; i < rows.size() && i < 10; i++) body += line(i);
    for (size_t i = 10; i < rows.size(); i++) {
        if (rows[i].guildId == guildId) body += "…\n" + line(i);
    }
    if (rows.empty()) body = "No ships yet.";

    std::string title = category == "gold" ? "Gold"
                      : category == "firepower" ? "Firepower"
                      : category == "victories" ? "Victories"
                      : category == "crew" ? "Crew size"
                      : "Score";
    Reply r = reply("🏆 Leaderboard" + DOT + title, body);
    if (category == "score") r.footer = "Score = gold earned + bonuses for wins − sinkings";
    if (category == "firepower") r.footer = "Average damage per broadside";
    return r;
}

Reply Game::shop(GuildState& g) {
    const Ship& s = g.ship;
    std::string body = "`wood` " + std::to_string(Tuning::WOOD_PRICE) + "g each\n";

    switch (s.getHull()) {
    case Hull::Wood: body += "`hull` Steel" + DOT + num(Tuning::STEEL_HULL_PRICE) + "g\n"; break;
    case Hull::Steel: body += "`hull` Crystal" + DOT + num(Tuning::CRYSTAL_HULL_PRICE) + "g\n"; break;
    case Hull::Crystal: body += "`hull` maxed\n"; break;
    }
    switch (s.getCannons()) {
    case Cannons::Iron: body += "`cannons` Silver" + DOT + num(Tuning::SILVER_CANNON_PRICE) + "g\n"; break;
    case Cannons::Silver: body += "`cannons` Gold" + DOT + num(Tuning::GOLD_CANNON_PRICE) + "g\n"; break;
    case Cannons::Gold: body += "`cannons` maxed\n"; break;
    }
    body += s.getGuns() < MAX_GUNS
          ? "`gun` #" + std::to_string(s.getGuns() + 1) + DOT + num(Tuning::GUN_PRICE_PER_GUN * s.getGuns()) + "g"
          : "`gun` maxed";

    Reply r = reply("🏪 Shop", body);
    r.footer = num(s.getGold()) + " gold · /buy <item>";
    return r;
}

// ============================================================================
// Naming the ship
// ============================================================================

std::string Game::voteButtonId(int voteId) { return "vote:" + std::to_string(voteId); }

int Game::parseVoteButtonId(const std::string& id) {
    if (id.rfind("vote:", 0) != 0) return 0;
    try {
        return std::stoi(id.substr(5));
    } catch (...) {
        return 0;
    }
}

void Game::applyName(GuildState& g, const std::string& name) {
    g.ship.setName(name);
    g.nameVote = NameVote{};
}

Reply Game::voteMessage(const GuildState& g, int64_t now) {
    const NameVote& v = g.nameVote;
    (void)now;
    Reply r = reply("🏷️ Rename the ship?",
                    bold(v.proposerName) + " proposes " + bold(v.name) + "\n" +
                    "Aye " + std::to_string(g.ayesFromCrew()) + "/" + std::to_string(g.votesNeeded()) + DOT + "closes " + when(v.closesAt));
    r.button = Reply::Button{ voteButtonId(v.id), "Aye" };
    return r;
}

Reply Game::nameShip(const Ctx& c, const std::string& raw, bool isAdmin) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = guilds.find(c.guildId);
    if (it == guilds.end() || !it->second.configured()) return note(NOT_SET_UP);
    GuildState& g = it->second;

    std::string name = cleanName(raw);
    if (name.empty()) return note("Use letters, numbers and spaces (40 max).");

    // Admins skip the vote
    if (isAdmin) {
        applyName(g, name);
        saveLocked();
        Reply r = reply("🏷️ Renamed", bold(c.userName) + " named the ship " + bold(name) + ".", Tone::Good);
        if (c.channelId != g.channelId) r.ephemeral = true;
        return r;
    }

    auto m = g.crew.find(c.userId);
    if (m == g.crew.end() || !m->second.aboard) return note(NOT_CREW);
    if (c.channelId != g.channelId) return note("Use this in " + channelLink(g.channelId) + ".");
    if (g.nameVote.open(c.now)) return note("A vote for " + bold(g.nameVote.name) + " is already open.");

    g.nameVote = NameVote{};
    g.nameVote.id = g.nextVoteId++;
    g.nameVote.name = name;
    g.nameVote.proposerName = c.userName;
    g.nameVote.ayes.insert(c.userId);  // proposing counts as voting aye
    g.nameVote.closesAt = c.now + Tuning::NAME_VOTE_DURATION;

    // A crew of one or two: the proposer alone is already half
    if (g.ayesFromCrew() >= g.votesNeeded()) {
        applyName(g, name);
        saveLocked();
        return reply("🏷️ Renamed", "The ship is now " + bold(name) + ".", Tone::Good);
    }
    saveLocked();
    return voteMessage(g, c.now);
}

Reply Game::voteOnName(const Ctx& c, int voteId) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = guilds.find(c.guildId);
    if (it == guilds.end() || !it->second.configured()) return note(NOT_SET_UP);
    GuildState& g = it->second;

    auto m = g.crew.find(c.userId);
    if (m == g.crew.end() || !m->second.aboard) return note("Only crew can vote. `/join` first.");

    NameVote& v = g.nameVote;
    if (v.id != voteId || !v.open(c.now)) {
        Reply closed = reply("🏷️ Vote closed", v.id == voteId ? bold(v.name) + " didn't get enough votes." : "This vote is over.");
        closed.editsOriginal = true;
        if (v.id == voteId) {
            v = NameVote{};
            saveLocked();
        }
        return closed;
    }
    if (v.ayes.count(c.userId) != 0) return note("You already voted.");

    v.ayes.insert(c.userId);
    if (g.ayesFromCrew() >= g.votesNeeded()) {
        std::string name = v.name;
        applyName(g, name);
        saveLocked();
        Reply done = reply("🏷️ Renamed", "The ship is now " + bold(name) + ".", Tone::Good);
        done.editsOriginal = true;
        return done;
    }
    saveLocked();
    Reply update = voteMessage(g, c.now);
    update.editsOriginal = true;
    return update;
}

// ============================================================================
// Sailing commands
// ============================================================================

Reply Game::sail(GuildState& g, const Ctx& c) {
    if (g.sailing) return note("Already sailing.");
    const Ship& s = g.ship;
    if (s.getHp() * 100 < s.getMaxHp() * Tuning::MIN_SAIL_HP_PERCENT) {
        return note("Too damaged to sail (" + std::to_string(s.getHp()) + "/" + std::to_string(s.getMaxHp()) + " HP). `/repair` first.");
    }
    g.sailing = true;
    g.ignoredEvents = 0;
    g.nextDockRepairAt = 0;
    g.nextEventAt = c.now + rng.range(Tuning::FIRST_EVENT_MIN, Tuning::FIRST_EVENT_MAX);
    return reply("⛵ Sailing", bold(c.userName) + " raised anchor.", Tone::Good);
}

Reply Game::anchor(GuildState& g, const Ctx& c) {
    if (!g.sailing) return note("Already anchored.");
    if (g.event.active()) return note("Can't anchor during an event.");
    g.sailing = false;
    return reply("⚓ Anchored", bold(c.userName) + " dropped anchor. No events until `/sail`.");
}

Reply Game::hunt(GuildState& g, const Ctx& c) {
    if (g.event.active()) return note(BUSY);
    if (!g.sailing) return note("Anchored. `/sail` first.");
    member(g, c).points += 1;
    Reply r = beginEvent(g, EventKind::EnemyShip, c.now);
    r.footer = c.userName + " went hunting";
    return r;
}

Reply Game::raid(GuildState& g, const Ctx& c) {
    if (g.event.active()) return note(BUSY);
    if (!g.sailing) return note("Anchored. `/sail` first.");
    if (g.nextRaidAt > c.now) return note("Islands are picked clean. Next raid " + when(g.nextRaidAt) + ".");

    g.nextRaidAt = c.now + Tuning::RAID_COOLDOWN;
    CrewMember& m = member(g, c);

    // Same idea as the original /loot: sometimes the island is guarded
    if (rng.chance(35)) {
        Reply r = beginEvent(g, EventKind::EnemyShip, c.now);
        r.title = "⚔️ Ambush!";
        r.footer = c.userName + "'s raid was a trap";
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
    return reply("🏝️ Raid", bold(c.userName) + DOT + loot(gold, wood), Tone::Good);
}

// ============================================================================
// Event-response commands
// ============================================================================

Reply Game::fire(GuildState& g, const Ctx& c) {
    Event& e = g.event;
    if (!e.isCombat()) return note("Nothing to shoot. `/hunt` to find a fight.");
    if (auto cd = onCooldown(c, Action::Fire, Tuning::CD_FIRE)) return *cd;

    e.crewResponded = true;
    Ship::Broadside b = g.ship.fireBroadside(rng, e.foe.ac);
    e.foe.hp -= b.damage;

    CrewMember& m = member(g, c);
    m.shots += b.shots;
    m.hits += b.hits;
    m.damage += b.damage;
    m.points += 1 + b.damage / 10;

    std::string shot = bold(c.userName) + DOT + std::to_string(b.hits) + "/" + std::to_string(b.shots) + " hits" +
                       (b.damage > 0 ? DOT + std::to_string(b.damage) + " dmg" : "");
    if (e.foe.hp <= 0) {
        m.points += 10;
        EventKind kind = e.kind;
        std::string foe = e.foe.name;
        std::string spoils = winFight(g, c.now);
        std::string title = kind == EventKind::Kraken ? "🏆 Kraken slain" : kind == EventKind::Merchant ? "🏆 Prize taken" : "🏆 Victory";
        std::string verb = kind == EventKind::Kraken ? " drove off " : kind == EventKind::Merchant ? " captured " : " sank ";
        return reply(title, bold(c.userName) + verb + bold(foe) + ".\n" + spoils, Tone::Good);
    }
    return reply(b.hits > 0 ? "💥 Hit" : "💨 Miss", shot + "\nEnemy " + bar(e.foe.hp, e.foe.maxHp), b.hits > 0 ? Tone::Good : Tone::Info);
}

Reply Game::flee(GuildState& g, const Ctx& c) {
    Event& e = g.event;
    if (!e.isCombat()) return note("Not in a fight.");
    if (auto cd = onCooldown(c, Action::Flee, Tuning::CD_FLEE)) return *cd;
    e.crewResponded = true;

    if (e.kind == EventKind::Merchant) {
        std::string foe = e.foe.name;
        return reply("👋 Let go", bold(c.userName) + " let " + bold(foe) + " pass." + endEvent(g, c.now));
    }

    int odds = e.kind == EventKind::Kraken ? 40 : 50;
    if (rng.chance(odds)) {
        return reply("💨 Escaped", bold(c.userName) + " got us away." + endEvent(g, c.now), Tone::Good);
    }
    int sunkBefore = g.stats.timesSunk;
    std::string volley = foeVolley(g, c.now);
    if (g.stats.timesSunk > sunkBefore) return reply("💀 Sunk", volley, Tone::Bad);
    return reply("❌ Can't escape", volley + "\nHull " + bar(g.ship.getHp(), g.ship.getMaxHp()), Tone::Bad);
}

Reply Game::repair(GuildState& g, const Ctx& c) {
    Ship& s = g.ship;
    if (s.getHp() >= s.getMaxHp()) return note("Hull is already full.");
    if (s.getWood() <= 0) return note("No wood. `/buy wood` or `/salvage`.");
    if (auto cd = onCooldown(c, Action::Repair, Tuning::CD_REPAIR)) return *cd;

    int healed = s.repair();
    CrewMember& m = member(g, c);
    m.repairs++;
    m.points += 2;
    if (g.event.active()) g.event.crewResponded = true;
    return reply("🔨 Repaired", bold(c.userName) + DOT + "+" + std::to_string(healed) + " HP" + DOT + std::to_string(s.getWood()) +
                 " wood left\nHull " + bar(s.getHp(), s.getMaxHp()), Tone::Good);
}

Reply Game::fightHazard(GuildState& g, const Ctx& c, EventKind kind) {
    Event& e = g.event;
    bool storm = kind == EventKind::Storm;
    if (e.kind != kind) return note(storm ? "No storm right now." : "Nothing's on fire.");
    if (auto cd = onCooldown(c, Action::Hazard, Tuning::CD_HAZARD)) return *cd;

    e.crewResponded = true;
    int amount = rng.range(4, 8);
    e.severity = std::max(0, e.severity - amount);
    CrewMember& m = member(g, c);
    m.hazardWork++;
    m.points += 2;

    if (e.severity <= 0) {
        int bonus = rng.range(15, 40);
        g.ship.addGold(bonus);
        g.stats.lifetimeGold += bonus;
        g.stats.hazardsWeathered++;
        return reply(storm ? "☀️ Storm beaten" : "🚒 Fire out",
                     bold(c.userName) + " finished it" + DOT + "+" + std::to_string(bonus) + " gold" + endEvent(g, c.now), Tone::Good);
    }
    return reply(storm ? "🪣 Bailing" : "💧 Dousing",
                 bold(c.userName) + DOT + "−" + std::to_string(amount) + "\n" + (storm ? "Water " : "Flames ") +
                 bar(e.severity, e.startingSeverity));
}

Reply Game::salvage(GuildState& g, const Ctx& c) {
    Event& e = g.event;
    if (e.kind != EventKind::Wreckage) return note("Nothing to salvage.");
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

    std::string got = bold(c.userName) + DOT + loot(gold, wood);
    if (e.severity <= 0 && e.goldPool <= 0) {
        return reply("🪵 All salvaged", got + endEvent(g, c.now), Tone::Good);
    }
    return reply("🪵 Salvage", got + "\nLeft: " + loot(e.goldPool, e.severity, ""), Tone::Good);
}

Reply Game::buy(GuildState& g, const Ctx& c, const std::string& item, int64_t amount) {
    if (g.event.active()) return note("Can't shop during an event.");
    Ship& s = g.ship;

    auto afford = [&](int price) -> std::optional<Reply> {
        if (s.getGold() < price) return note("Costs " + num(price) + " gold. You have " + num(s.getGold()) + ".");
        return std::nullopt;
    };
    auto bought = [&](int price, const std::string& what) {
        s.addGold(-price);
        member(g, c).points += 1;
        return reply("🛒 Bought", bold(c.userName) + DOT + what + DOT + "−" + num(price) + " gold\n" + num(s.getGold()) + " gold left", Tone::Good);
    };

    if (item == "wood") {
        int qty = static_cast<int>(std::clamp<int64_t>(amount <= 0 ? 5 : amount, 1, 100));
        int price = qty * Tuning::WOOD_PRICE;
        if (auto no = afford(price)) return *no;
        s.addWood(qty);
        return bought(price, std::to_string(qty) + " wood");
    }
    if (item == "hull") {
        if (s.getHull() == Hull::Crystal) return note("Hull is already maxed.");
        Hull next = hullFromTier(tier(s.getHull()) + 1);
        int price = next == Hull::Steel ? Tuning::STEEL_HULL_PRICE : Tuning::CRYSTAL_HULL_PRICE;
        if (auto no = afford(price)) return *no;
        s.setHull(next);
        return bought(price, toString(next) + " hull");
    }
    if (item == "cannons") {
        if (s.getCannons() == Cannons::Gold) return note("Cannons are already maxed.");
        Cannons next = cannonsFromTier(tier(s.getCannons()) + 1);
        int price = next == Cannons::Silver ? Tuning::SILVER_CANNON_PRICE : Tuning::GOLD_CANNON_PRICE;
        if (auto no = afford(price)) return *no;
        s.setCannons(next);
        return bought(price, toString(next) + " cannons");
    }
    if (item == "gun") {
        if (s.getGuns() >= MAX_GUNS) return note("Guns are already maxed.");
        int price = Tuning::GUN_PRICE_PER_GUN * s.getGuns();
        if (auto no = afford(price)) return *no;
        s.setGuns(s.getGuns() + 1);
        return bought(price, "gun #" + std::to_string(s.getGuns()));
    }
    return note("Choose wood, hull, cannons or gun.");
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
        r.title = "⚔️ Pirates!";
        r.body = bold(e.foe.name) + DOT + toString(enemy.getHull()) + DOT + toString(enemy.getCannons()) + DOT +
                 std::to_string(enemy.getGuns()) + " guns\n`/fire` `/repair` `/flee`" + DOT + "they board " + when(e.deadline);
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
        r.title = "💰 Merchant";
        r.body = bold(e.foe.name) + DOT + "light guns, full hold\n`/fire` to capture" + DOT + "leaves " + when(e.deadline);
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
        r.title = "🐙 Kraken!";
        r.body = "Tentacles on the hull.\n`/fire` `/repair` `/flee`" + DOT + "lets go " + when(e.deadline);
        break;
    case EventKind::Storm:
        e.startingSeverity = e.severity = rng.range(35, 55) + 10 * hullTier;
        e.deadline = now + Tuning::HAZARD_DURATION;
        e.nextPulseAt = now + Tuning::HAZARD_PULSE_EVERY;
        r.title = "🌊 Storm!";
        r.body = "Water " + bar(e.severity, e.startingSeverity) + "\n`/bail`" + DOT + "ends " + when(e.deadline);
        break;
    case EventKind::Fire:
        e.startingSeverity = e.severity = rng.range(30, 50) + 10 * hullTier;
        e.deadline = now + Tuning::HAZARD_DURATION;
        e.nextPulseAt = now + Tuning::HAZARD_PULSE_EVERY;
        r.title = "🔥 Fire!";
        r.body = "Flames " + bar(e.severity, e.startingSeverity) + "\n`/douse`" + DOT + "burns out " + when(e.deadline);
        break;
    case EventKind::Wreckage:
        e.startingSeverity = e.severity = rng.range(8, 16);
        e.goldPool = rng.range(30, 90);
        e.deadline = now + Tuning::BOUNTY_DURATION;
        e.nextPulseAt = e.deadline;
        r.title = "🪵 Wreckage";
        r.body = loot(e.goldPool, e.severity, "") + " floating\n`/salvage`" + DOT + "drifts off " + when(e.deadline);
        r.tone = Tone::Good;
        break;
    case EventKind::None:
        break;
    }

    g.event = e;
    return r;
}

// Enemy shoots at us. Returns "2/3 hits · −18 HP" (plus the sinking report if that happens).
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
    std::string text = damage == 0 ? "Missed us"
                     : std::to_string(hits) + "/" + std::to_string(f.guns) +
                       (g.event.kind == EventKind::Kraken ? " tentacles" : " hits") + DOT + "−" + std::to_string(damage) + " HP";
    if (g.ship.sunk()) text += "\n" + sink(g, now);
    return text;
}

// Collect the spoils. Returns "+180 gold · +4 wood".
std::string Game::winFight(GuildState& g, int64_t now) {
    Event& e = g.event;
    int gold = e.foe.goldReward, wood = e.foe.woodReward;
    g.ship.addGold(gold);
    g.ship.addWood(wood);
    g.stats.lifetimeGold += gold;
    if (e.kind == EventKind::Kraken) g.stats.krakens++;
    else g.stats.victories++;
    return loot(gold, wood) + endEvent(g, now);
}

// Our ship goes down. Lose a chunk of the cargo, get towed to port at anchor.
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
    return "Lost " + loot(goldLost, woodLost, "") + ".\nTowed to port at " + std::to_string(s.getHp()) + "/" +
           std::to_string(s.getMaxHp()) + " HP" + DOT + "`/sail` when ready";
}

// Clears the current event and schedules the next one. Returns an extra line if the ship anchors itself.
std::string Game::endEvent(GuildState& g, int64_t now) {
    bool responded = g.event.crewResponded;
    g.event = Event{};
    g.nextEventAt = now + rng.range(Tuning::EVENT_GAP_MIN, Tuning::EVENT_GAP_MAX);

    g.ignoredEvents = responded ? 0 : g.ignoredEvents + 1;
    const Ship& s = g.ship;
    if (g.sailing && s.getHp() * 100 < s.getMaxHp() * Tuning::MIN_SAIL_HP_PERCENT) {
        g.sailing = false;
        return "\n⚓ Too damaged to sail. Anchored for repairs.";
    }
    if (g.sailing && g.ignoredEvents >= Tuning::AUTO_ANCHOR_AFTER) {
        g.sailing = false;
        g.ignoredEvents = 0;
        return "\n⚓ " + std::to_string(Tuning::AUTO_ANCHOR_AFTER) + " alerts missed. Anchored" + DOT + "`/sail` to continue";
    }
    return "";
}

// Something happens mid-event: enemy volley, storm surge, fire spreading
std::optional<Reply> Game::pulse(GuildState& g, int64_t now) {
    Event& e = g.event;
    if (e.kind == EventKind::Wreckage) return std::nullopt;
    int sunkBefore = g.stats.timesSunk;

    if (e.isCombat()) {
        bool kraken = e.kind == EventKind::Kraken;
        e.nextPulseAt = now + Tuning::COMBAT_VOLLEY_EVERY;
        std::string volley = foeVolley(g, now);  // may sink us, which clears the event
        if (g.stats.timesSunk > sunkBefore) return reply("💀 Sunk", volley, Tone::Bad);

        // Pirates (and the Kraken) want our cargo, not a wreck: once we're badly holed they stop
        // attacking and take what they can. That's a loss, but it means one missed alert can't sink us.
        const Ship& s = g.ship;
        if (e.kind != EventKind::Merchant && s.getHp() * 100 <= s.getMaxHp() * Tuning::BOARD_BELOW_PERCENT) {
            Reply taken = timeout(g, now);
            taken.body = volley + "\n" + taken.body;
            return taken;
        }
        return reply(kraken ? "🐙 Kraken attack" : "💣 Incoming",
                     volley + "\nHull " + bar(s.getHp(), s.getMaxHp()) + "\nEnemy " + std::to_string(e.foe.hp) + "/" +
                     std::to_string(e.foe.maxHp) + DOT + "ends " + when(e.deadline), Tone::Bad);
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
    std::string hit = "−" + std::to_string(damage) + " HP" + (woodBurned ? DOT + "−1 wood" : "");
    if (g.ship.sunk()) return reply("💀 Sunk", hit + "\n" + sink(g, now), Tone::Bad);

    return reply(storm ? "🌊 Storm" : "🔥 Fire",
                 hit + "\nHull " + bar(g.ship.getHp(), g.ship.getMaxHp()) + "\n" + (storm ? "`/bail`" : "`/douse`") + DOT +
                 "ends " + when(e.deadline), Tone::Bad);
}

// The event's time is up. The more the crew did, the less it hurts.
Reply Game::timeout(GuildState& g, int64_t now) {
    Event e = g.event;  // copy: sink()/endEvent() clear g.event
    Ship& s = g.ship;

    switch (e.kind) {
    case EventKind::EnemyShip:
    case EventKind::Kraken: {
        // What they take scales with how healthy they still are
        bool kraken = e.kind == EventKind::Kraken;
        double strength = static_cast<double>(e.foe.hp) / e.foe.maxHp;
        int goldLost = static_cast<int>(std::lround(s.getGold() * (kraken ? 0.25 : 0.30) * strength));
        int woodLost = static_cast<int>(std::lround(s.getWood() * (kraken ? 0.40 : 0.30) * strength));
        s.addGold(-goldLost);
        s.addWood(-woodLost);
        return reply(kraken ? "🐙 Kraken gone" : "🏴‍☠️ Boarded",
                     std::string(kraken ? "It took " : "They took ") + loot(goldLost, woodLost, "") + "." + endEvent(g, now), Tone::Bad);
    }
    case EventKind::Merchant:
        return reply("💨 Merchant escaped", bold(e.foe.name) + " got away." + endEvent(g, now));
    case EventKind::Storm:
    case EventKind::Fire: {
        bool storm = e.kind == EventKind::Storm;
        int damage = s.takeDamage(e.severity / 4);
        int woodLost = storm ? 0 : std::min(s.getWood(), e.severity / 10);
        s.addWood(-woodLost);
        std::string hit = "−" + std::to_string(damage) + " HP" + (woodLost ? DOT + "−" + std::to_string(woodLost) + " wood" : "");
        if (s.sunk()) return reply("💀 Sunk", hit + "\n" + sink(g, now), Tone::Bad);
        return reply(storm ? "🌤️ Storm over" : "🔥 Fire burnt out",
                     hit + "\nHull " + bar(s.getHp(), s.getMaxHp()) + endEvent(g, now), Tone::Bad);
    }
    case EventKind::Wreckage:
        return reply("🌊 Wreckage gone", "It drifted away." + endEvent(g, now));
    case EventKind::None:
        break;
    }
    return reply("", "");
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
