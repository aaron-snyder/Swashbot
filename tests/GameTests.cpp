// File: GameTests.cpp
// Tests for the game engine. No Discord needed: time is just a number we pass in.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <iostream>
#include <string>
#include <vector>
#include "../src/Game.h"

using namespace ShipSpecs;

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        checks++;                                                                      \
        if (!(cond)) {                                                                 \
            failures++;                                                                \
            std::cerr << "  FAILED " << __FILE__ << ":" << __LINE__ << ": " #cond "\n"; \
        }                                                                              \
    } while (0)

static bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

// A configured server with two crew members, ready for testing
constexpr uint64_t GUILD = 1001, CHANNEL = 2002, ROLE = 3003, ALICE = 11, BOB = 12, CAROL = 13;
constexpr int64_t T0 = 1'700'000'000;

Ctx ctx(uint64_t user, int64_t now, uint64_t channel = CHANNEL, uint64_t guild = GUILD) {
    Ctx c;
    c.guildId = guild;
    c.channelId = channel;
    c.userId = user;
    c.userName = user == ALICE ? "Alice" : user == BOB ? "Bob" : "Carol";
    c.now = now;
    return c;
}

std::unique_ptr<Game> makeGame(std::string path = "", uint32_t seed = 42) {
    auto g = std::make_unique<Game>(path, seed);
    g->configure(GUILD, "Test Server", CHANNEL, ROLE, T0);
    g->join(ctx(ALICE, T0));
    g->join(ctx(BOB, T0));
    return g;
}

// ---------------------------------------------------------------------------

void testShip() {
    Ship s;
    CHECK(s.getHp() == 100 && s.getMaxHp() == 100 && s.getAc() == 12);
    CHECK(s.getGold() == 100 && s.getWood() == 5 && s.getGuns() == 2);

    // Repairs use wood and never overheal (the original heal() bug: maxHp was never set)
    CHECK(s.repair() == 0);  // already full
    s.takeDamage(30);
    CHECK(s.repair() == 20 && s.getWood() == 4 && s.getHp() == 90);
    CHECK(s.repair() == 10 && s.getHp() == 100);

    // Damage can't go below zero hp
    CHECK(s.takeDamage(500) == 100 && s.sunk());

    // Hull upgrades keep the damage you've taken
    Ship t;
    t.takeDamage(40);
    t.setHull(Hull::Steel);
    CHECK(t.getMaxHp() == 200 && t.getHp() == 160 && t.getAc() == 15);

    // Firepower grows with every upgrade
    Ship u;
    double base = u.firepower();
    u.setGuns(3);
    double moreGuns = u.firepower();
    u.setCannons(Cannons::Gold);
    CHECK(moreGuns > base && u.firepower() > moreGuns);
    u.setGuns(99);
    CHECK(u.getGuns() == MAX_GUNS);

    // Enemies are within one tier of the player
    Rng rng(7);
    Ship player(Cannons::Silver, Hull::Steel, 3);
    for (int i = 0; i < 200; i++) {
        Ship e = Ship::randomEnemy(rng, player);
        CHECK(std::abs(tier(e.getHull()) - 1) <= 1 && std::abs(tier(e.getCannons()) - 1) <= 1);
        CHECK(e.getGuns() >= 2 && e.getGuns() <= 4);
    }

    // Broadside: one roll per gun, AC 0 always hits, AC 99 never does
    Ship v(Cannons::Iron, Hull::Wood, 4);
    Ship::Broadside all = v.fireBroadside(rng, 0);
    CHECK(all.shots == 4 && all.hits == 4 && all.damage >= 4 && all.damage <= 80);
    Ship::Broadside none = v.fireBroadside(rng, 99);
    CHECK(none.hits == 0 && none.damage == 0);
}

void testGating() {
    auto game = std::make_unique<Game>("", 1);
    // Before /setup
    Reply r = game->command("status", ctx(ALICE, T0));
    CHECK(r.ephemeral && contains(r.body, "/setup"));
    CHECK(game->checkJoin(ctx(ALICE, T0)).has_value());

    game->configure(GUILD, "Test Server", CHANNEL, ROLE, T0);
    // Not crew yet
    r = game->command("fire", ctx(ALICE, T0));
    CHECK(r.ephemeral && contains(r.body, "/join"));

    CHECK(!game->checkJoin(ctx(ALICE, T0)).has_value());
    game->join(ctx(ALICE, T0));
    CHECK(game->checkJoin(ctx(ALICE, T0)).has_value());  // already aboard

    // Crew, but in the wrong channel
    r = game->command("sail", ctx(ALICE, T0, 9999));
    CHECK(r.ephemeral && contains(r.body, "<#2002>"));

    // Read-only commands work anywhere but stay private outside the ship channel
    CHECK(game->command("status", ctx(CAROL, T0, 9999)).ephemeral);
    CHECK(!game->command("status", ctx(CAROL, T0)).ephemeral);
    CHECK(game->command("leaderboard", ctx(CAROL, T0, 9999)).ephemeral);
    CHECK(!game->command("leaderboard", ctx(CAROL, T0)).ephemeral);
}

void testSetupAndRepair() {
    auto game = std::make_unique<Game>("", 3);
    game->configure(GUILD, "Test Server", CHANNEL, ROLE, T0);
    GuildState g = game->snapshot(GUILD);
    CHECK(g.configured() && !g.sailing);
    CHECK(game->tick(T0 + 24 * 3600).empty());  // nobody aboard, nobody gets pinged

    game->join(ctx(ALICE, T0 + 100));  // first sailor: she sets sail
    g = game->snapshot(GUILD);
    CHECK(g.sailing && g.nextEventAt >= T0 + 100 + Tuning::FIRST_EVENT_MIN);
    game->join(ctx(BOB, T0 + 100));
    std::vector<uint64_t> ids = game->crewIds(GUILD);
    CHECK(ids.size() == 2);

    // Re-running setup with a rebuilt channel and role keeps the ship, crew and voyage
    game->edit(GUILD, [](GuildState& s) { s.stats.victories = 4; });
    int64_t nextEvent = game->snapshot(GUILD).nextEventAt;
    game->configure(GUILD, "", 7777, 8888, T0 + 200);
    g = game->snapshot(GUILD);
    CHECK(g.channelId == 7777 && g.roleId == 8888 && g.guildName == "Test Server");
    CHECK(g.stats.victories == 4 && g.crewCount() == 2 && g.sailing && g.nextEventAt == nextEvent);
    CHECK(game->command("status", ctx(ALICE, T0, 7777)).ephemeral == false);  // the new channel is the deck now
}

void testJoinLeave() {
    auto game = makeGame();
    game->edit(GUILD, [](GuildState& g) { g.crew[ALICE].points = 42; });
    CHECK(!game->checkLeave(ctx(ALICE, T0)).has_value());
    game->leave(ctx(ALICE, T0));
    CHECK(game->snapshot(GUILD).crewCount() == 1);
    CHECK(game->checkLeave(ctx(ALICE, T0)).has_value());
    CHECK(game->command("fire", ctx(ALICE, T0)).ephemeral);

    Reply back = game->join(ctx(ALICE, T0 + 100));
    CHECK(contains(back.body, "Welcome back"));
    CHECK(game->snapshot(GUILD).crew.at(ALICE).points == 42);  // stats survive leaving
}

void testRandomEventsFromTick() {
    auto game = makeGame();
    GuildState g = game->snapshot(GUILD);
    CHECK(g.sailing);
    CHECK(g.nextEventAt >= T0 + Tuning::FIRST_EVENT_MIN && g.nextEventAt <= T0 + Tuning::FIRST_EVENT_MAX);

    CHECK(game->tick(g.nextEventAt - 1).empty());
    std::vector<Announcement> out = game->tick(g.nextEventAt);
    CHECK(out.size() == 1);
    CHECK(out[0].channelId == CHANNEL && out[0].roleId == ROLE && out[0].message.pingCrew);
    CHECK(game->snapshot(GUILD).event.active());

    // Anchored ships get no events
    auto calm = makeGame();
    calm->edit(GUILD, [](GuildState& s) { s.sailing = false; });
    CHECK(calm->tick(T0 + 999999).empty());
}

void testCombatVictory() {
    auto game = makeGame();
    game->startEvent(GUILD, EventKind::EnemyShip, T0);
    game->edit(GUILD, [](GuildState& g) { g.event.foe.ac = 0; g.event.foe.hp = 5; });  // guaranteed hit, one shot left
    int goldBefore = game->snapshot(GUILD).ship.getGold();
    int reward = game->snapshot(GUILD).event.foe.goldReward;

    Reply r = game->command("fire", ctx(ALICE, T0 + 1));
    CHECK(!r.ephemeral && contains(r.title, "Victory"));
    GuildState g = game->snapshot(GUILD);
    CHECK(!g.event.active());
    CHECK(g.stats.victories == 1 && g.stats.lifetimeGold == reward);
    CHECK(g.ship.getGold() == goldBefore + reward);
    CHECK(g.crew.at(ALICE).points >= 11 && g.crew.at(ALICE).shots == 2);
    CHECK(g.nextEventAt >= T0 + 1 + Tuning::EVENT_GAP_MIN);
}

void testCooldowns() {
    auto game = makeGame();
    game->startEvent(GUILD, EventKind::EnemyShip, T0);
    game->edit(GUILD, [](GuildState& g) { g.event.foe.hp = g.event.foe.maxHp = 100000; });

    CHECK(!game->command("fire", ctx(ALICE, T0)).ephemeral);
    CHECK(game->command("fire", ctx(ALICE, T0 + 1)).ephemeral);                     // Alice is reloading
    CHECK(!game->command("fire", ctx(BOB, T0 + 1)).ephemeral);                      // but Bob can fire
    CHECK(!game->command("fire", ctx(ALICE, T0 + Tuning::CD_FIRE)).ephemeral);      // and Alice again later
}

void testEnemyBoardsOnTimeout() {
    // Untouched enemy takes 30% of the gold
    auto game = makeGame();
    game->edit(GUILD, [](GuildState& g) { g.ship.addGold(900); });  // 1000 gold
    game->startEvent(GUILD, EventKind::EnemyShip, T0);
    game->edit(GUILD, [](GuildState& g) { g.event.foe.guns = 0; });  // no volleys, just the boarding
    std::vector<Announcement> out = game->tick(T0 + Tuning::COMBAT_DURATION);
    CHECK(out.size() == 1 && contains(out[0].message.title, "Boarded"));
    CHECK(game->snapshot(GUILD).ship.getGold() == 700);

    // A badly beaten enemy takes much less: fighting back reduces losses
    auto game2 = makeGame();
    game2->edit(GUILD, [](GuildState& g) { g.ship.addGold(900); });
    game2->startEvent(GUILD, EventKind::EnemyShip, T0);
    game2->edit(GUILD, [](GuildState& g) { g.event.foe.guns = 0; g.event.foe.hp = g.event.foe.maxHp / 10; });
    game2->tick(T0 + Tuning::COMBAT_DURATION);
    CHECK(game2->snapshot(GUILD).ship.getGold() == 970);
}

void testEnemyVolleysAndSinking() {
    auto game = makeGame();
    game->startEvent(GUILD, EventKind::EnemyShip, T0);
    // Nothing happens before the first volley
    CHECK(game->tick(T0 + Tuning::COMBAT_FIRST_VOLLEY - 1).empty());

    game->edit(GUILD, [](GuildState& g) {
        g.ship.addGold(100);     // 200 gold
        g.ship.setHp(1);
        g.event.foe.hitBonus = 100;  // can't miss
    });
    std::vector<Announcement> out = game->tick(T0 + Tuning::COMBAT_FIRST_VOLLEY);
    CHECK(out.size() == 1 && contains(out[0].message.title, "Sunk"));
    GuildState g = game->snapshot(GUILD);
    CHECK(g.stats.timesSunk == 1 && !g.sailing && !g.event.active());
    CHECK(g.ship.getGold() == 120 && g.ship.getHp() == 40);
}

void testPiratesBoardInsteadOfSinking() {
    auto game = makeGame();
    game->startEvent(GUILD, EventKind::EnemyShip, T0);
    game->edit(GUILD, [](GuildState& g) {
        g.ship.setHp(40);
        g.event.foe.hitBonus = 100;  // every shot hits...
        g.event.foe.damageDie = 1;   // ...for exactly 1 damage
        g.event.foe.guns = 15;       // 40 - 15 = 25 HP, below the boarding line
    });
    std::vector<Announcement> out = game->tick(T0 + Tuning::COMBAT_FIRST_VOLLEY);
    CHECK(out.size() == 1 && contains(out[0].message.title, "Boarded"));
    GuildState g = game->snapshot(GUILD);
    CHECK(!g.event.active() && g.stats.timesSunk == 0 && g.ship.getHp() == 25);
    CHECK(g.ship.getGold() == 70);  // untouched enemy takes 30%
}

void testBadlyDamagedShipHeadsForCove() {
    auto game = makeGame();
    game->startEvent(GUILD, EventKind::Wreckage, T0);
    game->edit(GUILD, [](GuildState& g) { g.ship.setHp(20); });
    std::vector<Announcement> out = game->tick(T0 + Tuning::BOUNTY_DURATION);
    CHECK(out.size() == 1 && contains(out[0].message.body, "Too damaged to sail"));
    CHECK(!game->snapshot(GUILD).sailing);

    Reply r = game->command("sail", ctx(ALICE, T0 + Tuning::BOUNTY_DURATION));
    CHECK(r.ephemeral && contains(r.body, "Too damaged"));
    game->command("repair", ctx(ALICE, T0 + Tuning::BOUNTY_DURATION));  // 20 -> 40 HP
    CHECK(!game->command("sail", ctx(ALICE, T0 + Tuning::BOUNTY_DURATION)).ephemeral);
}

void testAnchoredShipsRepairSlowly() {
    auto game = makeGame();
    game->edit(GUILD, [](GuildState& g) { g.sailing = false; g.ship.setHp(50); });
    game->tick(T0);                                   // schedules the first patch
    game->tick(T0 + Tuning::DOCK_REPAIR_EVERY - 1);
    CHECK(game->snapshot(GUILD).ship.getHp() == 50);
    game->tick(T0 + Tuning::DOCK_REPAIR_EVERY);
    CHECK(game->snapshot(GUILD).ship.getHp() == 55);
    for (int i = 2; i < 30; i++) game->tick(T0 + i * Tuning::DOCK_REPAIR_EVERY);
    CHECK(game->snapshot(GUILD).ship.getHp() == 100);  // never overheals
}

void testFlee() {
    auto game = makeGame();
    game->startEvent(GUILD, EventKind::Merchant, T0);
    Reply r = game->command("flee", ctx(ALICE, T0));  // letting a merchant go always works
    CHECK(contains(r.title, "Let go") && !game->snapshot(GUILD).event.active());

    // Across many tries, some escapes succeed and some fail
    int escaped = 0;
    for (int i = 0; i < 40; i++) {
        auto g2 = makeGame("", 100 + i);
        g2->startEvent(GUILD, EventKind::EnemyShip, T0);
        g2->command("flee", ctx(ALICE, T0));
        if (!g2->snapshot(GUILD).event.active()) escaped++;
    }
    CHECK(escaped > 5 && escaped < 35);
}

void testStorm() {
    auto game = makeGame();
    game->startEvent(GUILD, EventKind::Storm, T0);
    CHECK(game->command("douse", ctx(ALICE, T0)).ephemeral);  // wrong tool for the job

    int64_t t = T0;
    for (int i = 0; i < 100 && game->snapshot(GUILD).event.active(); i++) {
        t += Tuning::CD_HAZARD;
        game->command("bail", ctx(ALICE, t));
        game->command("bail", ctx(BOB, t));
    }
    GuildState g = game->snapshot(GUILD);
    CHECK(!g.event.active() && g.stats.hazardsWeathered == 1);
    CHECK(g.crew.at(ALICE).hazardWork > 0 && g.crew.at(BOB).hazardWork > 0);
}

void testIgnoredStormHurts() {
    auto game = makeGame();
    game->startEvent(GUILD, EventKind::Storm, T0);
    for (int64_t t = T0; t <= T0 + Tuning::HAZARD_DURATION; t += 10) game->tick(t);
    GuildState g = game->snapshot(GUILD);
    CHECK(!g.event.active() && g.ship.getHp() < 100 && g.stats.hazardsWeathered == 0);
}

void testFireBurnsWood() {
    auto game = makeGame();
    game->startEvent(GUILD, EventKind::Fire, T0);
    game->tick(T0 + Tuning::HAZARD_PULSE_EVERY);
    CHECK(game->snapshot(GUILD).ship.getWood() == 4);
}

void testWreckage() {
    auto game = makeGame();
    game->startEvent(GUILD, EventKind::Wreckage, T0);
    int64_t t = T0;
    for (int i = 0; i < 100 && game->snapshot(GUILD).event.active(); i++) {
        t += Tuning::CD_SALVAGE;
        game->command("salvage", ctx(ALICE, t));
    }
    GuildState g = game->snapshot(GUILD);
    CHECK(!g.event.active() && g.ship.getWood() > 5 && g.crew.at(ALICE).plunder > 0);
}

void testAutoAnchor() {
    auto game = makeGame();
    int64_t t = T0;
    for (int i = 0; i < Tuning::AUTO_ANCHOR_AFTER; i++) {
        game->startEvent(GUILD, EventKind::Wreckage, t);
        t += Tuning::BOUNTY_DURATION;
        game->tick(t);
    }
    CHECK(!game->snapshot(GUILD).sailing);
    CHECK(game->tick(t + 24 * 3600).empty());  // stays quiet
    CHECK(!game->command("sail", ctx(ALICE, t)).ephemeral);
    CHECK(game->snapshot(GUILD).sailing);
}

void testHuntRaidAnchor() {
    auto game = makeGame();
    CHECK(!game->command("hunt", ctx(ALICE, T0)).ephemeral);
    CHECK(game->snapshot(GUILD).event.kind == EventKind::EnemyShip);
    CHECK(game->command("hunt", ctx(BOB, T0)).ephemeral);    // already busy
    CHECK(game->command("anchor", ctx(BOB, T0)).ephemeral);  // can't anchor mid-fight

    auto g2 = makeGame("", 5);
    Reply raid = g2->command("raid", ctx(ALICE, T0));
    CHECK(!raid.ephemeral);
    CHECK(g2->command("raid", ctx(BOB, T0 + 60)).ephemeral);  // islands need time to restock
    if (!g2->snapshot(GUILD).event.active()) {
        CHECK(!g2->command("anchor", ctx(BOB, T0)).ephemeral);
        CHECK(g2->command("hunt", ctx(BOB, T0)).ephemeral);   // anchored
    }
}

void testShop() {
    auto game = makeGame();
    CHECK(game->command("buy", ctx(ALICE, T0), "hull").ephemeral);  // can't afford
    CHECK(!game->command("buy", ctx(ALICE, T0), "wood", 3).ephemeral);
    GuildState g = game->snapshot(GUILD);
    CHECK(g.ship.getWood() == 8 && g.ship.getGold() == 100 - 3 * Tuning::WOOD_PRICE);

    game->edit(GUILD, [](GuildState& s) { s.ship.addGold(5000); });
    CHECK(!game->command("buy", ctx(ALICE, T0), "hull").ephemeral);
    CHECK(!game->command("buy", ctx(ALICE, T0), "cannons").ephemeral);
    CHECK(!game->command("buy", ctx(ALICE, T0), "gun").ephemeral);
    g = game->snapshot(GUILD);
    CHECK(g.ship.getHull() == Hull::Steel && g.ship.getCannons() == Cannons::Silver && g.ship.getGuns() == 3);
    CHECK(g.ship.getGold() == 5076 - Tuning::STEEL_HULL_PRICE - Tuning::SILVER_CANNON_PRICE - 2 * Tuning::GUN_PRICE_PER_GUN);

    game->startEvent(GUILD, EventKind::Storm, T0);
    CHECK(game->command("buy", ctx(ALICE, T0), "wood").ephemeral);  // no shopping mid-storm
}

void testScoreAndLeaderboard() {
    GuildState g;
    g.stats.lifetimeGold = 1000;
    g.stats.victories = 2;
    g.stats.krakens = 1;
    g.stats.timesSunk = 1;
    CHECK(g.score() == 1000 + 300 + 300 - 200);
    g.stats.timesSunk = 50;
    CHECK(g.score() == 0);

    auto game = makeGame();
    game->configure(5005, "Rival Server", 6006, 7007, T0);
    game->edit(5005, [](GuildState& s) { s.ship.setName("Rival"); s.stats.lifetimeGold = 999999; });
    game->configure(8008, "Middle Server", 9009, 1, T0);
    game->edit(8008, [](GuildState& s) { s.ship.setName("Middling"); s.stats.lifetimeGold = 5000; });
    game->edit(GUILD, [](GuildState& s) { s.ship.setName("Ours"); s.stats.lifetimeGold = 10; });

    Reply board = game->command("leaderboard", ctx(ALICE, T0));
    size_t rival = board.body.find("Rival"), middle = board.body.find("Middling"), ours = board.body.find("Ours");
    CHECK(rival < middle && middle < ours);
    CHECK(contains(board.body, "**Ours** · 10 · *Test Server* ◀"));

    Reply fp = game->command("leaderboard", ctx(ALICE, T0), "firepower");
    CHECK(contains(fp.title, "Firepower"));
}

void testAdminNaming() {
    auto game = makeGame();
    // Admins rename instantly, from any channel, and names are cleaned of pings and formatting
    Reply r = game->nameShip(ctx(CAROL, T0, 9999), "  @everyone <@123> **Sea Dog**  ", true);
    CHECK(r.ephemeral && contains(r.title, "Renamed"));
    CHECK(game->snapshot(GUILD).ship.getName() == "everyone 123 Sea Dog");
    CHECK(game->nameShip(ctx(CAROL, T0), "@@@", true).ephemeral);
}

void testCrewNameVote() {
    auto game = makeGame();
    game->join(ctx(CAROL, T0));  // crew of 3: a name needs 2 ayes
    std::string original = game->snapshot(GUILD).ship.getName();

    CHECK(game->nameShip(ctx(ALICE, T0, 9999), "Salty Gull", false).ephemeral);  // proposals happen on deck
    Reply r = game->nameShip(ctx(ALICE, T0), "Salty Gull", false);
    CHECK(r.button.has_value() && contains(r.body, "Aye 1/2"));
    int id = Game::parseVoteButtonId(r.button->id);
    CHECK(id > 0 && Game::parseVoteButtonId("something-else") == 0);
    CHECK(game->snapshot(GUILD).ship.getName() == original);

    CHECK(game->nameShip(ctx(BOB, T0), "Other", false).ephemeral);       // one vote at a time
    CHECK(game->voteOnName(ctx(ALICE, T0 + 1), id).ephemeral);           // proposer already voted
    CHECK(game->voteOnName(ctx(99, T0 + 1), id).ephemeral);              // not crew

    Reply done = game->voteOnName(ctx(BOB, T0 + 2), id);                 // 2/3 = half: passes
    CHECK(done.editsOriginal && contains(done.title, "Renamed") && !done.button);
    GuildState g = game->snapshot(GUILD);
    CHECK(g.ship.getName() == "Salty Gull" && !g.nameVote.open(T0 + 2));
    CHECK(game->voteOnName(ctx(CAROL, T0 + 3), id).editsOriginal);       // pressing an old button just closes it

    // Votes expire
    Reply slow = game->nameShip(ctx(BOB, T0 + 10), "Slow Boat", false);
    Reply late = game->voteOnName(ctx(CAROL, T0 + 10 + Tuning::NAME_VOTE_DURATION), Game::parseVoteButtonId(slow.button->id));
    CHECK(late.editsOriginal && contains(late.title, "closed"));
    CHECK(game->snapshot(GUILD).ship.getName() == "Salty Gull");

    // In a crew of two, the proposer alone is half
    auto duo = makeGame();
    Reply instant = duo->nameShip(ctx(ALICE, T0), "Twin Sails", false);
    CHECK(contains(instant.title, "Renamed") && !instant.button && duo->snapshot(GUILD).ship.getName() == "Twin Sails");
}

void testPersistence() {
    std::string path = (std::filesystem::temp_directory_path() / "swashbot-test.json").string();
    std::filesystem::remove(path);
    {
        auto game = makeGame(path);
        game->edit(GUILD, [](GuildState& g) {
            g.ship.setName("Persisted");
            g.ship.setHull(Hull::Steel);
            g.stats.victories = 7;
            g.crew[ALICE].points = 99;
            g.nameVote.id = 5;
            g.nameVote.name = "Pending Name";
            g.nameVote.ayes = { ALICE, BOB };
            g.nameVote.closesAt = T0 + 999999;
        });
        game->startEvent(GUILD, EventKind::Kraken, T0);
        game->edit(GUILD, [](GuildState& g) { g.event.foe.hp = 77; });
        CHECK(game->save());
    }
    {
        // Bot comes back a day later: the kraken is still there, with a fresh grace period
        auto game = std::make_unique<Game>(path, 1);
        int64_t later = T0 + 24 * 3600;
        CHECK(game->load(later));
        GuildState g = game->snapshot(GUILD);
        CHECK(g.configured() && g.channelId == CHANNEL && g.roleId == ROLE && g.guildName == "Test Server");
        CHECK(g.ship.getName() == "Persisted" && g.ship.getHull() == Hull::Steel && g.ship.getMaxHp() == 200);
        CHECK(g.stats.victories == 7 && g.crew.at(ALICE).points == 99 && g.crew.size() == 2);
        CHECK(g.event.kind == EventKind::Kraken && g.event.foe.hp == 77);
        CHECK(g.nameVote.id == 5 && g.nameVote.name == "Pending Name" && g.nameVote.ayes.size() == 2);
        CHECK(g.event.deadline >= later + Tuning::RESUME_GRACE);
        CHECK(game->tick(later + 1).empty());  // no instant punishment
    }
    {
        // A corrupt file is backed up instead of being silently overwritten
        std::ofstream(path) << "{ not json";
        auto game = std::make_unique<Game>(path, 1);
        CHECK(!game->load(T0));
        CHECK(std::filesystem::exists(path + ".corrupt-" + std::to_string(T0)));
        std::filesystem::remove(path + ".corrupt-" + std::to_string(T0));
    }
    std::filesystem::remove(path);
}

void testLongSimulation() {
    // A week of sailing with an active crew shouldn't crash, go negative or stall
    auto game = makeGame("", 2024);
    int64_t t = T0;
    int events = 0;
    const std::vector<std::string> actions = { "fire", "repair", "bail", "douse", "salvage" };
    for (int step = 0; step < 7 * 24 * 360; step++) {
        t += 10;
        events += static_cast<int>(game->tick(t).size());
        GuildState g = game->snapshot(GUILD);
        if (!g.sailing) game->command("sail", ctx(ALICE, t));
        if (g.event.active()) {
            for (const std::string& a : actions) {
                game->command(a, ctx(ALICE, t));
                game->command(a, ctx(BOB, t));
            }
        }
        if (step % 500 == 0) {
            game->command("buy", ctx(BOB, t), "gun");
            game->command("buy", ctx(BOB, t), "cannons");
            game->command("buy", ctx(BOB, t), "hull");
        }
    }
    GuildState g = game->snapshot(GUILD);
    CHECK(events > 100);
    CHECK(g.ship.getGold() >= 0 && g.ship.getWood() >= 0 && g.ship.getHp() >= 0);
    CHECK(g.stats.victories > 0);
    std::cout << "  week-long sim: " << events << " messages, score " << g.score() << ", " << g.stats.victories
              << " victories, " << g.stats.krakens << " krakens, " << g.stats.timesSunk << " sinkings, ship: " << g.ship.info() << "\n";
}

void testCasualCrewSimulation() {
    // A sleepy crew: notices only some alarms, acts about once a minute, and is offline at night.
    // The ship should still make progress, anchor itself overnight and never get stuck.
    auto game = makeGame("", 77);
    Rng dice(5);
    int64_t t = T0;
    int autoAnchors = 0;
    bool noticed = false;
    EventKind lastKind = EventKind::None;
    const std::vector<std::string> actions = { "fire", "repair", "bail", "douse", "salvage" };
    for (int step = 0; step < 14 * 24 * 360; step++) {
        t += 10;
        for (const Announcement& a : game->tick(t)) {
            if (contains(a.message.body, "alerts missed")) autoAnchors++;
        }
        GuildState g = game->snapshot(GUILD);
        int hour = static_cast<int>((t / 3600) % 24);
        bool awake = hour >= 9 && hour < 23;
        if (g.event.kind != lastKind) {
            lastKind = g.event.kind;
            noticed = dice.chance(60);
        }
        if (!awake) continue;
        if (!g.sailing && step % 360 == 0) game->command("sail", ctx(ALICE, t));
        if (g.event.active() && noticed && step % 6 == 0) {
            for (const std::string& a : actions) game->command(a, ctx(dice.chance(50) ? ALICE : BOB, t));
        }
        if (!g.event.active() && step % 1000 == 0) {
            game->command("raid", ctx(BOB, t));
            game->command("buy", ctx(BOB, t), "wood", 5);
            game->command("buy", ctx(BOB, t), "gun");
            game->command("buy", ctx(BOB, t), "hull");
            game->command("buy", ctx(BOB, t), "cannons");
        }
    }
    GuildState g = game->snapshot(GUILD);
    CHECK(g.score() > 0 && g.stats.victories > 0);
    CHECK(autoAnchors > 0);  // the ship protected itself at night
    std::cout << "  casual two-week sim: score " << g.score() << ", " << g.stats.victories << " victories, "
              << g.stats.timesSunk << " sinkings, " << autoAnchors << " auto-anchors, gold " << g.ship.getGold()
              << ", ship: " << g.ship.info() << "\n";
}

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        { "ship", testShip },
        { "gating", testGating },
        { "setup and repair", testSetupAndRepair },
        { "join/leave", testJoinLeave },
        { "random events from tick", testRandomEventsFromTick },
        { "combat victory", testCombatVictory },
        { "cooldowns", testCooldowns },
        { "enemy boards on timeout", testEnemyBoardsOnTimeout },
        { "enemy volleys and sinking", testEnemyVolleysAndSinking },
        { "pirates board instead of sinking", testPiratesBoardInsteadOfSinking },
        { "anchored ships repair slowly", testAnchoredShipsRepairSlowly },
        { "badly damaged ship heads for a cove", testBadlyDamagedShipHeadsForCove },
        { "flee", testFlee },
        { "storm", testStorm },
        { "ignored storm hurts", testIgnoredStormHurts },
        { "fire burns wood", testFireBurnsWood },
        { "wreckage", testWreckage },
        { "auto anchor", testAutoAnchor },
        { "hunt/raid/anchor", testHuntRaidAnchor },
        { "shop", testShop },
        { "score and leaderboard", testScoreAndLeaderboard },
        { "admin naming", testAdminNaming },
        { "crew name vote", testCrewNameVote },
        { "persistence", testPersistence },
        { "long simulation", testLongSimulation },
        { "casual crew simulation", testCasualCrewSimulation },
    };
    for (const auto& [name, fn] : tests) {
        int before = failures;
        fn();
        std::cout << (failures == before ? "[ ok ] " : "[FAIL] ") << name << "\n";
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
