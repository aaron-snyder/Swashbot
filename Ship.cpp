// Author: Aaron Snyder
// File: Ship.cpp
// Ship.cpp is the class definition file for Ship.h. Meant to simulate a pirate ship, updating
// HP and AC accordingly with hull, and returning an attack roll dependant on cannons.

#include "Ship.h"
#include <iostream>
#include <random>

using namespace ShipSpecs;

// Default constructor
// The player ship will always be instantiated here. Setting activity matters in the default constructor for this reason.
Ship::Ship() {
    combat = false;
    cannons = Cannons::Iron;
    hull = Hull::Wood;
    updateArmor();
    srand(time(0));
}

// Manual constructor
// Enemy ships will never use activity, so setting it is meaningless. activity is only to check timer behavior for the player ship.
Ship::Ship(Cannons can, Hull hul) {
    combat = false;
    cannons = can;
    hull = hul;
    updateArmor();
    srand(time(0));
}

// Sets armor class based on current hull
void Ship::updateArmor() {
    switch (hull) {
    case Hull::Wood:
        hp = 100;
        ac = 12;
        break;
    case Hull::Steel:
        hp = 200;
        ac = 15;
        break;
    case Hull::Crystal:
        hp = 300;
        ac = 18;
        break;
    }
}

void Ship::setCannons(Cannons can) {
    cannons = can;
}

Cannons Ship::getCannons() {
    return cannons;
}

std::string Ship::getCannonString() {
    switch (cannons) {
    case Cannons::Iron:
        return "Iron";
    case Cannons::Silver:
        return "Silver";
    case Cannons::Gold:
        return "Gold";
    }
    return "ERROR IN CANNONS SWITCH";
}

void Ship::setHull(Hull hul) {
    hull = hul;
    updateArmor();
}

std::string Ship::getHullString() {
    switch (hull) {
        case Hull::Wood:
            return "Wood";
        case Hull::Steel:
            return "Steel";
        case Hull::Crystal:
            return "Crystal";
    }
    return "ERROR IN HULL SWITCH";
}

bool Ship::inCombat() {
    return combat;
}

void Ship::setCombat(bool c) {
    combat = c;
}

Hull Ship::getHull() {
    return hull;
}

int Ship::getHp() {
    return hp;
}

int Ship::getMaxHp() {
    return maxHp;
}

int Ship::getAc() {
    return ac;
}

// Returns 1d20 + cannon buff to hit the target
int Ship::hit() {
    int returnHit = 0;

    returnHit += rand() % 20 + 1;

    switch (cannons) {
    case Cannons::Iron:
        returnHit += 2;
        break;
    case Cannons::Silver:
        returnHit += 4;
        break;
    case Cannons::Gold:
        returnHit += 6;
        break;
    }

    return returnHit;
}

// Rolls damage based on current cannons
int Ship::damageRoll() {
    int returnDamage = 0;

    switch (cannons) {
    case Cannons::Iron:
        returnDamage += rand() % 20 + 1;
        break;
    case Cannons::Silver:
        returnDamage += rand() % 40 + 1;
        break;
    case Cannons::Gold:
        returnDamage += rand() % 60 + 1;
        break;
    }

    return returnDamage;
}

// Removes hp
void Ship::takeDamage(int damage) {
    hp -= damage;
}

// Adds hp
void Ship::heal() {
    hp += 20;
    if (hp > maxHp) {
        hp = maxHp;
    }
}

// Getter for ship wood
int Ship::getWood() {
    return wood;
}

// Returns string with info about ship
std::string Ship::info() {
    std::string returnString = getHullString() + " hull ship with " + getCannonString() + " cannons";
    return returnString;
}