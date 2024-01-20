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
        average += ship1.damageRoll();
    }
    cout << "Average damage roll for Ship 1: " << average / 100 << endl << endl;

    // Check average damage for upgraded ship
    average = 0;
    for (int i = 0; i < 100; i++) {
        average += ship2.damageRoll();
    }
    cout << "Average damage roll for Ship 2: " << average / 100 << endl << endl;

    // Simulate battle between basic ship and upgraded ship
    battle(ship1, ship2);

    // Create 3rd ship, equal to first ship
    Ship ship3;
    cout << endl;

    // Initiate battle between two equal ships
    battle(ship1, ship3);

    return 0;
}

void battle(Ship player, Ship enemy) {
    int userInput;
    int turns = 0;
    while (player.getHp() > 0 && enemy.getHp() > 0) {
        turns++;
        if (player.hit() >= enemy.getAc()) {
            enemy.takeDamage(player.damageRoll());
        }

        if (enemy.hit() >= player.getAc()) {
            player.takeDamage(enemy.damageRoll());
        }
    }

    cout << "Player HP: " << player.getHp() << endl << "Enemy HP: " << enemy.getHp() << endl << turns << "Turns" << endl;
}