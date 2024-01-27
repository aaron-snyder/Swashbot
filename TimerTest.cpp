#include <iostream>
#include <chrono>
#include <thread>
#include <functional>

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

int main()
{
  timer_start(do_something, 1000);
  while (true)
    ;
}