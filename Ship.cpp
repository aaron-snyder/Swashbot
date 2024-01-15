// Author: Aaron Snyder
// File: Ship.cpp
// Ship.cpp is the class definition file for Ship.h. Meant to simulate a pirate ship, updating
// HP and AC accordingly with hull, and returning an attack roll dependant on cannons.

#include "Ship.h"

using namespace ShipSpecs;

// Default constructor
Ship::Ship(){

}

Ship::Ship(Cannons can, Hull hul) {

}

void Ship::setCannons(Cannons can) {

}

Cannons Ship::getCannons() {
    return cannons;
}

void Ship::setHull(Hull hul) {

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
}

int Ship::damage() {
    // Returns damage based on cannon damage + cannonBuff
}
