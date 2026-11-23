// Author: Aaron Snyder
// File: Ship.cpp
// Implementation of the Ship class. Hull decides HP and armor class, cannons decide the attack
// bonus and damage die, and the number of guns decides how many shots a broadside fires.

#include "Ship.h"
#include <algorithm>

using namespace ShipSpecs;

// ---------------- ShipSpecs lookup tables ----------------

int ShipSpecs::tier(Cannons c) { return static_cast<int>(c); }
int ShipSpecs::tier(Hull h) { return static_cast<int>(h); }
Cannons ShipSpecs::cannonsFromTier(int t) { return static_cast<Cannons>(std::clamp(t, 0, 2)); }
Hull ShipSpecs::hullFromTier(int t) { return static_cast<Hull>(std::clamp(t, 0, 2)); }

std::string ShipSpecs::toString(Cannons c) {
    switch (c) {
    case Cannons::Iron: return "Iron";
    case Cannons::Silver: return "Silver";
    case Cannons::Gold: return "Gold";
    }
    return "Unknown";
}

std::string ShipSpecs::toString(Hull h) {
    switch (h) {
    case Hull::Wood: return "Wood";
    case Hull::Steel: return "Steel";
    case Hull::Crystal: return "Crystal";
    }
    return "Unknown";
}

int ShipSpecs::hitBonus(Cannons c) {
    switch (c) {
    case Cannons::Iron: return 2;
    case Cannons::Silver: return 4;
    case Cannons::Gold: return 6;
    }
    return 0;
}

int ShipSpecs::damageDie(Cannons c) {
    switch (c) {
    case Cannons::Iron: return 20;
    case Cannons::Silver: return 40;
    case Cannons::Gold: return 60;
    }
    return 1;
}

int ShipSpecs::baseHp(Hull h) {
    switch (h) {
    case Hull::Wood: return 100;
    case Hull::Steel: return 200;
    case Hull::Crystal: return 300;
    }
    return 1;
}

int ShipSpecs::armorClass(Hull h) {
    switch (h) {
    case Hull::Wood: return 12;
    case Hull::Steel: return 15;
    case Hull::Crystal: return 18;
    }
    return 10;
}

// ---------------- Ship ----------------

Ship::Ship() : Ship(Cannons::Iron, Hull::Wood, 2) {
    gold = 100;
    wood = 5;
}

Ship::Ship(Cannons can, Hull hul, int g) : cannons(can), hull(hul) {
    setGuns(g);
    updateArmor();
    hp = maxHp;
}

Ship Ship::randomEnemy(Rng& rng, const Ship& player) {
    // Cross-matched so fights stay fair whatever the crew upgraded:
    //  - enemy firepower (cannons, guns) follows OUR hull, so it can't one-shot a ship that only bought guns
    //  - enemy toughness (hull) follows OUR cannons, so better cannons don't make every fight trivial
    // Each stat then drifts -1/0/+1, leaning towards an even fight.
    static const int drift[] = { -1, 0, 0, 0, 1 };
    auto nudge = [&]() { return drift[rng.range(0, 4)]; };

    Ship enemy(cannonsFromTier(tier(player.hull) + nudge()),
               hullFromTier(tier(player.cannons) + nudge()),
               2 + tier(player.hull) + nudge());
    enemy.gold = 0;
    enemy.wood = 0;
    return enemy;
}

void Ship::updateArmor() {
    maxHp = baseHp(hull);
    ac = armorClass(hull);
}

void Ship::setHull(Hull hul) {
    // Keep the same amount of damage when the hull changes, so an upgrade isn't a free full heal
    int damage = maxHp - hp;
    hull = hul;
    updateArmor();
    hp = std::max(1, maxHp - damage);
}

void Ship::setGuns(int g) { guns = std::clamp(g, MIN_GUNS, MAX_GUNS); }
void Ship::setHp(int h) { hp = std::clamp(h, 0, maxHp); }
void Ship::addGold(int g) { gold = std::max(0, gold + g); }
void Ship::addWood(int w) { wood = std::max(0, wood + w); }

// Returns 1d20 + cannon buff to hit the target
int Ship::hit(Rng& rng) const { return rng.die(20) + hitBonus(cannons); }

// Rolls damage based on current cannons
int Ship::damageRoll(Rng& rng) const { return rng.die(damageDie(cannons)); }

Ship::Broadside Ship::fireBroadside(Rng& rng, int targetAc) const {
    Broadside result;
    for (int i = 0; i < guns; i++) {
        result.shots++;
        if (hit(rng) >= targetAc) {
            result.hits++;
            result.damage += damageRoll(rng);
        }
    }
    return result;
}

int Ship::takeDamage(int damage) {
    int taken = std::clamp(damage, 0, std::max(hp, 0));
    hp -= taken;
    return taken;
}

int Ship::repair() {
    if (wood <= 0 || hp >= maxHp) {
        return 0;
    }
    wood--;
    int before = hp;
    hp = std::min(maxHp, hp + REPAIR_AMOUNT);
    return hp - before;
}

double Ship::firepower() const {
    // Chance that d20 + bonus >= 15, times the average damage of a hit, times the number of guns
    const int averageAc = 15;
    int winningFaces = std::clamp(21 - (averageAc - hitBonus(cannons)), 1, 20);
    double hitChance = winningFaces / 20.0;
    double averageDamage = (damageDie(cannons) + 1) / 2.0;
    return guns * hitChance * averageDamage;
}

std::string Ship::info() const {
    return toString(hull) + " hull ship with " + toString(cannons) + " cannons (" +
           std::to_string(guns) + (guns == 1 ? " gun)" : " guns)");
}
