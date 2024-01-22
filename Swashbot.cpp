#include <iostream>
#include <string>
#include "Ship.h"

#include <random>

using namespace std;

void battle(Ship player, Ship enemy);

int main() {
    // Seed random
    srand(time(0));

    dpp::cluster bot("token");
 
    bot.on_log(dpp::utility::cout_logger());
 
    /* The event is fired when someone issues your commands */
    bot.on_slashcommand([&bot](const dpp::slashcommand_t & event) {
        /* Check which command they ran */
        if (event.command.get_command_name() == "battle") {
            event.reply("Finding ship");
        } else if (event.command.get_command_name() == "fire") {
            event.reply("Firing cannons");
        } else if (event.command.get_command_name() == "heal") {
            event.reply("Healing");
        } else if (event.command.get_command_name() == "loot") {
            event.reply("Looking for island");
        }
    });
 
    bot.on_ready([&bot](const dpp::ready_t & event) {
        if (dpp::run_once<struct register_bot_commands>()) {
            /* Create some commands */
            dpp::slashcommand pingcommand("battle", "Finds enemy ship", bot.me.id);
            dpp::slashcommand pongcommand("fire", "Fires cannons", bot.me.id);
            dpp::slashcommand dingcommand("heal", "Uses wood to heal ship", bot.me.id);
            dpp::slashcommand dongcommand("loot", "Finds an island to loot", bot.me.id);
 
            /* Register our commands in a list using bulk */
            bot.global_bulk_command_create({ pingcommand, pongcommand, dingcommand, dongcommand });
        }
    });
 
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
