// Author: Aaron Snyder
// File: Ship.h
// Class header file for the Ship object. Ships fire broadsides (one d20 attack roll per gun),
// take damage, get repaired with wood, and carry the crew's gold and wood.

#pragma once
#include <string>
#include "Random.h"

// Namespace for Cannon type and Hull type
namespace ShipSpecs {
    enum class Cannons { Iron, Silver, Gold };
    enum class Hull { Wood, Steel, Crystal };

    constexpr int MIN_GUNS = 1;
    constexpr int MAX_GUNS = 6;
    constexpr int REPAIR_AMOUNT = 20;   // HP restored per piece of wood

    int tier(Cannons c);                 // 0, 1, 2
    int tier(Hull h);                    // 0, 1, 2
    Cannons cannonsFromTier(int t);
    Hull hullFromTier(int t);

    std::string toString(Cannons c);
    std::string toString(Hull h);

    int hitBonus(Cannons c);             // added to the d20 attack roll
    int damageDie(Cannons c);            // damage per hit is 1..damageDie
    int baseHp(Hull h);
    int armorClass(Hull h);
}

class Ship {
public:
    // Starter player ship: wood hull, iron cannons, 2 guns, a little gold and wood
    Ship();
    // Manual constructor, used for enemy ships
    Ship(ShipSpecs::Cannons can, ShipSpecs::Hull hul, int guns);

    // Builds an enemy roughly matched to `player` (each stat can be one step weaker or stronger)
    static Ship randomEnemy(Rng& rng, const Ship& player);

    // --- Combat ---
    struct Broadside { int shots = 0; int hits = 0; int damage = 0; };
    // Every gun makes a d20 + cannon bonus attack roll against `targetAc`; hits roll cannon damage
    Broadside fireBroadside(Rng& rng, int targetAc) const;
    int hit(Rng& rng) const;            // single d20 + bonus attack roll
    int damageRoll(Rng& rng) const;     // single cannon damage roll
    int takeDamage(int damage);         // returns damage actually taken
    int repair();                       // uses 1 wood, returns HP restored (0 if nothing happened)
    bool sunk() const { return hp <= 0; }

    // Expected damage per broadside against an average (AC 15) target. Used for the firepower leaderboard.
    double firepower() const;

    // --- Upgrades ---
    void setCannons(ShipSpecs::Cannons can) { cannons = can; }
    void setHull(ShipSpecs::Hull hul);  // keeps current damage when swapping hulls
    void setGuns(int g);

    // --- Getters / setters ---
    const std::string& getName() const { return name; }
    void setName(const std::string& n) { name = n; }
    ShipSpecs::Cannons getCannons() const { return cannons; }
    ShipSpecs::Hull getHull() const { return hull; }
    int getGuns() const { return guns; }
    int getHp() const { return hp; }
    int getMaxHp() const { return maxHp; }
    int getAc() const { return ac; }
    int getGold() const { return gold; }
    int getWood() const { return wood; }

    void setHp(int h);
    void addGold(int g);                // can be negative, never drops below 0
    void addWood(int w);                // can be negative, never drops below 0

    std::string info() const;           // "Steel hull ship with Silver cannons (3 guns)"

private:
    void updateArmor();                 // sets maxHp and ac from the hull

    std::string name = "The Swashbot";
    ShipSpecs::Cannons cannons = ShipSpecs::Cannons::Iron;
    ShipSpecs::Hull hull = ShipSpecs::Hull::Wood;
    int guns = 2;
    int hp = 0;
    int maxHp = 0;
    int ac = 0;
    int gold = 0;
    int wood = 0;
};
