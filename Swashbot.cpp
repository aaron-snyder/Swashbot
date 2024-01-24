#include <iostream>
#include <string>
#include "Ship.h"

#include <random>

using namespace std;

void battle(Ship player, Ship enemy);

int main() {
    // Create player ship
    Ship playerShip();

    // Boolean combat determines if the player is currently in combat.
    bool combat = false;

    // Seed random
    srand(time(0));

    dpp::cluster bot("token");
 
    bot.on_log(dpp::utility::cout_logger());
 
    // Slash command is issued
    bot.on_slashcommand([&bot](const dpp::slashcommand_t & event) {
        // Check command and call appropriate function
        if (event.command.get_command_name() == "battle") {
            if (combat) {
                event.reply("Already in combat!");
            } else {
                event.reply("Finding ship");
                combat = true;
            }
            
        } else if (event.command.get_command_name() == "fire") {
            if (combat) {
                event.reply("Firing cannons");
            } else {
                event.reply("Not in combat!");
            }
            
        } else if (event.command.get_command_name() == "heal") {
            if (playerShip.getHp())
            event.reply("Healing");
        } else if (event.command.get_command_name() == "loot") {
            event.reply("Looking for island");
        }
    });
 
    bot.on_ready([&bot](const dpp::ready_t & event) {
        if (dpp::run_once<struct register_bot_commands>()) {
            // Create commands
            dpp::slashcommand battlecommand("battle", "Finds enemy ship", bot.me.id);
            dpp::slashcommand firecommand("fire", "Fires cannons", bot.me.id);
            dpp::slashcommand healcommand("heal", "Uses wood to heal ship", bot.me.id);
            dpp::slashcommand lootcommand("loot", "Finds an island to loot", bot.me.id);
 
            // Register commands
            bot.global_bulk_command_create({ pingcommand, pongcommand, dingcommand, dongcommand });
        }
    });
 
    // Start bot
    bot.start(dpp::st_wait);
 
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
