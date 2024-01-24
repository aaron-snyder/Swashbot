// Author: Aaron Snyder
// File: Ship.h
// Class header file for the Ship object. Class allows ship to fire, roll for damage, and heal.

#pragma once
#include <iostream>
#include <vector>
#include <string>

// Namespace for Cannon type and Hull type
namespace ShipSpecs {
    enum class Cannons { Iron, Silver, Gold };
    enum class Hull { Wood, Steel, Crystal };
}

using namespace ShipSpecs;

class Ship {
    // Private properties
private:
    Cannons cannons;
    Hull hull;
    int wood;
    int hp;
    int maxHp;
    int ac;

public:
    // Constructors
    Ship();
    Ship(Cannons can, Hull hul);

    // Class functions
    void updateArmor();
    void setCannons(Cannons can);
    Cannons getCannons();
    void setHull(Hull hul);
    Hull getHull();
    int getHp();
    int getMaxHp();
    int getAc();
    void heal();
    int hit();
    int damageRoll();
    void takeDamage(int damage);
};