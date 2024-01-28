#include <iostream>
#include <chrono>
#include <thread>
#include <functional>
#include <random>

bool combat = false;
bool looting = false;
bool defending = true;

void timer_start(std::function<void(void)> func, unsigned int interval)
{
  std::thread([func, interval]()
  { 
    while (true)
    { 
      auto x = std::chrono::steady_clock::now() + std::chrono::milliseconds(interval);
      func();
      std::this_thread::sleep_until(x);
    }
  }).detach();
}

void startCombat()
{
  looting = false;
  defending = false;
  combat = true;
  std::cout << "Entered combat" << std::endl;
}

void startLooting() {
  looting = true;
  defending = false;
  combat = false;
  std::cout << "Started looting" << std::endl;
}

void startDefending() {
  looting = false;
  defending = true;
  combat = false;
  std::cout << "Started defending" << std::endl;
}

int main()
{
  srand (time(NULL));
  switch (rand() % 3 + 1) {
    case 1:
      timer_start(startCombat, 1000);
      break;
    case 2:
      timer_start(startLooting, 1000);
      break;
    case 3:
      timer_start(startDefending, 1000);
      break;
  }
  
  while (true)
    ;
}