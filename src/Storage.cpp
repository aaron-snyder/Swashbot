// File: Storage.cpp
// Saves every ship to a single JSON file and loads it back on startup - the
// "load playerShip.ser" idea from the original MyBot.cpp, done for every server at once.
// Writes go to a temp file first and are then renamed over the real one, so a crash mid-save
// can't leave a half-written file behind.

#include "Game.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

using nlohmann::json;
using namespace ShipSpecs;

namespace {

json shipToJson(const Ship& s) {
    return {
        { "name", s.getName() }, { "hull", tier(s.getHull()) }, { "cannons", tier(s.getCannons()) },
        { "guns", s.getGuns() }, { "hp", s.getHp() }, { "gold", s.getGold() }, { "wood", s.getWood() },
    };
}

Ship shipFromJson(const json& j) {
    Ship s(cannonsFromTier(j.value("cannons", 0)), hullFromTier(j.value("hull", 0)), j.value("guns", 2));
    s.setName(j.value("name", std::string("The Swashbot")));
    s.setHp(j.value("hp", s.getMaxHp()));
    s.addGold(j.value("gold", 0));
    s.addWood(j.value("wood", 0));
    return s;
}

json eventToJson(const Event& e) {
    return {
        { "kind", static_cast<int>(e.kind) }, { "startedAt", e.startedAt }, { "deadline", e.deadline },
        { "nextPulseAt", e.nextPulseAt }, { "severity", e.severity }, { "startingSeverity", e.startingSeverity },
        { "goldPool", e.goldPool }, { "crewResponded", e.crewResponded },
        { "foe", {
            { "name", e.foe.name }, { "hp", e.foe.hp }, { "maxHp", e.foe.maxHp }, { "ac", e.foe.ac },
            { "hitBonus", e.foe.hitBonus }, { "damageDie", e.foe.damageDie }, { "guns", e.foe.guns },
            { "goldReward", e.foe.goldReward }, { "woodReward", e.foe.woodReward },
        } },
    };
}

Event eventFromJson(const json& j) {
    Event e;
    e.kind = static_cast<EventKind>(j.value("kind", 0));
    e.startedAt = j.value("startedAt", int64_t{ 0 });
    e.deadline = j.value("deadline", int64_t{ 0 });
    e.nextPulseAt = j.value("nextPulseAt", int64_t{ 0 });
    e.severity = j.value("severity", 0);
    e.startingSeverity = j.value("startingSeverity", 0);
    e.goldPool = j.value("goldPool", 0);
    e.crewResponded = j.value("crewResponded", false);
    if (j.contains("foe")) {
        const json& f = j["foe"];
        e.foe.name = f.value("name", std::string());
        e.foe.hp = f.value("hp", 0);
        e.foe.maxHp = f.value("maxHp", 1);
        e.foe.ac = f.value("ac", 10);
        e.foe.hitBonus = f.value("hitBonus", 0);
        e.foe.damageDie = f.value("damageDie", 1);
        e.foe.guns = f.value("guns", 1);
        e.foe.goldReward = f.value("goldReward", 0);
        e.foe.woodReward = f.value("woodReward", 0);
    }
    return e;
}

}  // namespace

bool Game::save() {
    std::lock_guard<std::mutex> lock(mutex);
    return saveLocked();
}

bool Game::saveLocked() {
    if (dataPath.empty()) return true;

    json root;
    root["version"] = 1;
    json& all = root["guilds"] = json::object();
    for (const auto& [id, g] : guilds) {
        json crew = json::object();
        for (const auto& [userId, m] : g.crew) {
            crew[std::to_string(userId)] = {
                { "name", m.name }, { "joinedAt", m.joinedAt }, { "aboard", m.aboard }, { "points", m.points },
                { "shots", m.shots }, { "hits", m.hits }, { "damage", m.damage }, { "repairs", m.repairs },
                { "hazardWork", m.hazardWork }, { "plunder", m.plunder },
            };
        }
        // Snowflake ids are stored as strings: they don't fit in a JavaScript number if anyone reads this file elsewhere
        all[std::to_string(id)] = {
            { "guildName", g.guildName }, { "channelId", std::to_string(g.channelId) }, { "roleId", std::to_string(g.roleId) },
            { "ship", shipToJson(g.ship) },
            { "stats", {
                { "lifetimeGold", g.stats.lifetimeGold }, { "victories", g.stats.victories }, { "krakens", g.stats.krakens },
                { "hazardsWeathered", g.stats.hazardsWeathered }, { "raids", g.stats.raids }, { "timesSunk", g.stats.timesSunk },
            } },
            { "crew", crew }, { "sailing", g.sailing }, { "nextEventAt", g.nextEventAt }, { "nextRaidAt", g.nextRaidAt },
            { "nextDockRepairAt", g.nextDockRepairAt }, { "ignoredEvents", g.ignoredEvents }, { "event", eventToJson(g.event) },
        };
    }

    try {
        std::filesystem::path path(dataPath);
        if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
        std::filesystem::path tmp = path;
        tmp += ".tmp";
        {
            std::ofstream out(tmp, std::ios::trunc);
            if (!out) throw std::runtime_error("cannot open " + tmp.string());
            out << root.dump(2);
            if (!out) throw std::runtime_error("write failed");
        }
        std::filesystem::rename(tmp, path);
        return true;
    } catch (const std::exception& ex) {
        std::cerr << "[swashbot] save failed: " << ex.what() << std::endl;
        return false;
    }
}

bool Game::load(int64_t now) {
    std::lock_guard<std::mutex> lock(mutex);
    if (dataPath.empty() || !std::filesystem::exists(dataPath)) return false;

    try {
        std::ifstream in(dataPath);
        json root = json::parse(in);
        guilds.clear();

        for (const auto& [idText, j] : root.at("guilds").items()) {
            GuildState& g = guild(std::stoull(idText));
            g.guildName = j.value("guildName", std::string());
            g.channelId = std::stoull(j.value("channelId", std::string("0")));
            g.roleId = std::stoull(j.value("roleId", std::string("0")));
            g.ship = shipFromJson(j.at("ship"));

            const json& st = j.at("stats");
            g.stats.lifetimeGold = st.value("lifetimeGold", 0LL);
            g.stats.victories = st.value("victories", 0);
            g.stats.krakens = st.value("krakens", 0);
            g.stats.hazardsWeathered = st.value("hazardsWeathered", 0);
            g.stats.raids = st.value("raids", 0);
            g.stats.timesSunk = st.value("timesSunk", 0);

            for (const auto& [userText, m] : j.at("crew").items()) {
                CrewMember& c = g.crew[std::stoull(userText)];
                c.name = m.value("name", std::string("Sailor"));
                c.joinedAt = m.value("joinedAt", int64_t{ 0 });
                c.aboard = m.value("aboard", true);
                c.points = m.value("points", 0);
                c.shots = m.value("shots", 0);
                c.hits = m.value("hits", 0);
                c.damage = m.value("damage", 0);
                c.repairs = m.value("repairs", 0);
                c.hazardWork = m.value("hazardWork", 0);
                c.plunder = m.value("plunder", 0);
            }

            g.sailing = j.value("sailing", false);
            g.nextEventAt = j.value("nextEventAt", int64_t{ 0 });
            g.nextRaidAt = j.value("nextRaidAt", int64_t{ 0 });
            g.nextDockRepairAt = j.value("nextDockRepairAt", int64_t{ 0 });
            g.ignoredEvents = j.value("ignoredEvents", 0);
            g.event = eventFromJson(j.at("event"));

            // The bot may have been offline for a while. Don't punish the crew for that:
            // give any running event a few minutes' grace, and don't fire a backlog of events at once.
            if (g.event.active()) {
                g.event.deadline = std::max(g.event.deadline, now + Tuning::RESUME_GRACE);
                g.event.nextPulseAt = std::max(g.event.nextPulseAt, now + 60);
            } else if (g.sailing && g.nextEventAt < now) {
                g.nextEventAt = now + rng.range(2 * 60, 5 * 60);
            }
        }
        std::cout << "[swashbot] loaded " << guilds.size() << " ship(s) from " << dataPath << std::endl;
        return true;
    } catch (const std::exception& ex) {
        // Keep a copy of the unreadable file so the next save doesn't destroy anything recoverable
        std::string backup = dataPath + ".corrupt-" + std::to_string(now);
        std::error_code ignored;
        std::filesystem::copy_file(dataPath, backup, ignored);
        std::cerr << "[swashbot] could not load " << dataPath << ": " << ex.what() << " (copied to " << backup << ")" << std::endl;
        guilds.clear();
        return false;
    }
}
