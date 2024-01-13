#include <iostream>
#include <string>

using namespace std;

class Ship {
private:
    std::string name;
    int age;

public:
    // Constructor
    Ship(const std::string& n, int a) : name(n), age(a) {}

    // Member function to set the name
    void setName(const std::string& n) {
        name = n;
    }

    // Member function to set the age
    void setAge(int a) {
        age = a;
    }

    // Member function to get the name
    std::string getName() const {
        return name;
    }

    // Member function to get the age
    int getAge() const {
        return age;
    }

    // Member function to display information
    void displayInfo() const {
        std::cout << "Name: " << name << ", Age: " << age << std::endl;
    }
};

int main() {
    // Create an instance of the Person class
    Ship ship1("John Doe", 25);

    // Display information using member function
    ship1.displayInfo();

    // Set new values for name and age
    ship1.setName("Jane Doe");
    ship1.setAge(30);

    // Display updated information
    ship1.displayInfo();

    return 0;
}
