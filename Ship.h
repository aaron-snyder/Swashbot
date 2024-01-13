// Author: Aaron Snyder
// File: Card.h
// Class header file for the Ship object. Class allows ship to fire, and heal.

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
    int hp;
    int ac;

public:
    // Constructors
    Ship();
    Ship(Cannons can, Hull hul);

    // Class functions
    void setCannons(Cannons can);
    Cannons getCannons();
    void setHull(Hull hul);
    Hull getHull();
    int getHp();
    int getAc();
    int attack();
}