#include <iostream>
#include <chrono>
#include <thread>
#include <functional>
#include <random>
#include <string>

bool TIMER_GO = false;

void timer_start(std::function<void(void)> func, unsigned int interval)
{
  std::thread([func, interval]()
  { 
    while (TIMER_GO)
    { 
      auto x = std::chrono::steady_clock::now() + std::chrono::milliseconds(interval);
      func();
      std::this_thread::sleep_until(x);
    }
  }).detach();
}

void startCombat()
{
  std::cout << "Entered combat" << std::endl;
}

void startLooting() {
  std::cout << "Started looting" << std::endl;
}

void startDefending() {
  std::cout << "Started defending" << std::endl;
}

int main()
{
  std::string activity = "combat";
  if (activity == "combat") {
    timer_start(startCombat, 1000);
  } else if (activity == "looting") {
    timer_start(startLooting, 1000);
  } else if (activity == "defending") {
    timer_start(startDefending, 1000);
  } else {
    std::cout << "Error processing activity." << std::endl;
  } 
  while (true)
    ;
}