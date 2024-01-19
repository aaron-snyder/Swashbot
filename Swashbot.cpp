#include <iostream>
#include <string>
#include "Ship.h"

#include <random>

using namespace std;

void battle(Ship player, Ship enemy);

int main() {
    // Seed random
    srand(time(0));

    // Ensure both constructors work
    Ship ship1;
    cout << "Ship 1:" << endl << "AC: " << ship1.getAc() << endl << "HP: " << ship1.getHp() << endl << endl;
    Ship ship2(ShipSpecs::Cannons::Gold, ShipSpecs::Hull::Crystal);
    cout << "Ship 2:" << endl << "AC: " << ship2.getAc() << endl << "HP: " << ship2.getHp() << endl << endl;

    // Check average hit for default ship
    int average = 0;
    for (int i = 0; i < 100; i++) {
        average += ship1.hit();
    }
    cout << "Average hit roll for Ship 1: " << average / 100 << endl << endl;

    // Check average hit for upgraded ship
    average = 0;
    for (int i = 0; i < 100; i++) {
        average += ship2.hit();
    }
    cout << "Average hit roll for Ship 2: " << average / 100 << endl << endl;

    // Check average damage for default ship
    average = 0;
    for (int i = 0; i < 100; i++) {
        average += ship1.damage();
    }
    cout << "Average damage roll for Ship 1: " << average / 100 << endl << endl;

    // Check average damage for upgraded ship
    average = 0;
    for (int i = 0; i < 100; i++) {
        average += ship2.damage();
    }
    cout << "Average damage roll for Ship 2: " << average / 100 << endl << endl;

    battle(ship1, ship2);

    return 0;
}

void battle(Ship player, Ship enemy) {
    int userInput;
    while (player.getHp() > 0 || enemy.getHp() > 0) {
        cout << "Press 1 to hit, 2 to heal:" << endl;
        cin >> userInput;
        cout << "Number pressed: " << userInput << endl;
    }
}