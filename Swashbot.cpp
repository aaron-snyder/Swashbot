#include <iostream>
#include <string>
#include "Ship.h"
#include <random>
#include "Timer.h"

using namespace std;

void battle(Ship player, Ship enemy);
void startTimer(function<void(void)> func, unsigned int interval);
void timerTick();

int main() {

    bool TIMER_GO;

    // Create player ship
    Ship playerShip();

    // Seed random
    srand(time(0));

    dpp::cluster bot("token");
 
    bot.on_log(dpp::utility::cout_logger());
 
    // Slash command is issued
    bot.on_slashcommand([&bot](const dpp::slashcommand_t & event) {
        // Check command and call appropriate function
        if (event.command.get_command_name() == "battle") 
        {
            if (playerShip.getActivity() == "combat") {
                event.reply("Already in combat!");
            } else {
                event.reply("Finding ship");
                playerShip.setActivity("combat");
            }
            
        } 
        else if (event.command.get_command_name() == "fire") 
        {
            if (playerShip.getActivity() == "combat") {
                event.reply("Firing cannons");
            } else {
                event.reply("Not in combat!");
            }
        } 
        else if (event.command.get_command_name() == "heal") 
        {
            if (playerShip.getHp() < playerShip.getMaxHp()) {
                playerShip.heal();
                event.reply("New HP: " + playerShip.getHp() + "/" + playerShip.getMaxHp())
            } else {
                event.reply("Already at max HP!");
            }
        } 
        else if (event.command.get_command_name() == "loot") 
        {
            playerShip.setActivity("looting");
            event.reply("Looting nearest island!");
            TIMER_GO = false;
            startTimer()
        } 
        else if (event.command.get_command_name() == "hide") 
        {
            playerShip.setActivity("hiding");
            event.reply("Dropping anchor in a nearby cove!");
            TIMER_GO = false;
            // Because hiding resembles pausing the game, no timer needs to be started.
        }
    });
 
    bot.on_ready([&bot](const dpp::ready_t & event) {
        if (dpp::run_once<struct register_bot_commands>()) {
            // Create commands
            dpp::slashcommand battlecommand("battle", "Finds enemy ship", bot.me.id);
            dpp::slashcommand firecommand("fire", "Fires cannons", bot.me.id);
            dpp::slashcommand healcommand("heal", "Uses wood to heal ship", bot.me.id);
            dpp::slashcommand lootcommand("loot", "Finds an island to loot", bot.me.id);
            dpp::slashcommand defendcommand("hide", "Finds a sneaky place to drop anchor", bot.me.id);
 
            // Register commands
            bot.global_bulk_command_create({ battlecommand, firecommand, healcommand, lootcommand, defendcommand });
        }
    });
 
    // Start bot
    bot.start(dpp::st_wait);
 
    return 0;
}

void startTimer(function<void(void)> func, unsigned int interval)
{
  TIMER_GO = true;
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
