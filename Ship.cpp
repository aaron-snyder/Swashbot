// Author: Aaron Snyder
// File: Ship.cpp
// Ship.cpp is the class definition file for Ship.h. Meant to simulate a pirate ship, updating
// HP and AC accordingly with hull, and returning an attack roll dependant on cannons.

#include "Ship.h"

using namespace ShipSpecs;

// Default constructor
Ship::Ship(){
    cannons = Cannons::Iron;
    hull = Hull::Wood;
    updateArmor();
}

Ship::Ship(Cannons can, Hull hul) {
    cannons = can;
    hull = hul;
    updateArmor();
}

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

void Ship::setHull(Hull hul) {
    hull = hul;
}

Hull Ship::getHull() {
    return hull;
}

int Ship::getHp() {
    return hp;
}

int Ship::getAc() {
    return ac;
}

int Ship::hit() {
    // Returns d20 + cannonBuff to hit
    return 20;
}

int Ship::damage() {
    // Returns damage based on cannon damage + cannonBuff
    return 20;
}
