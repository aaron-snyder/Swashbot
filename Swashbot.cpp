#include <iostream>
#include <string>
#include "Ship.h"

using namespace std;

int main() {
    // Create an instance of the Ship class
    Ship ship1;

    cout << ship1.getAc() << endl << ship1.getHp();

    return 0;
}