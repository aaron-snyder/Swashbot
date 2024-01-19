// Author: Aaron Snyder
// File: Ship.cpp
// Ship.cpp is the class definition file for Ship.h. Meant to simulate a pirate ship, updating
// HP and AC accordingly with hull, and returning an attack roll dependant on cannons.

#include "Ship.h"
#include <iostream>
#include <random>

using namespace ShipSpecs;

// Default constructor
Ship::Ship(){
    cannons = Cannons::Iron;
    hull = Hull::Wood;
    updateArmor();
    wood = 10;
    srand(time(0));
}

Ship::Ship(Cannons can, Hull hul) {
    cannons = can;
    hull = hul;
    wood = 10;
    updateArmor();
    srand(time(0));
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
    updateArmor();
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

void Ship::heal() {
    if (hp < maxHp && wood > 0) {
        wood--;
        hp += rand() % 20 + 1;
        if (hp > maxHp) {
            hp = maxHp;
        }
    }
}

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

int Ship::damage() {
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
