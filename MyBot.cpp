#include <iostream>
#include <string>
#include <random>
#include "MyBot.h"
#include "Ship.h"
#include <dpp/dpp.h>

const std::string BOT_TOKEN = "server token goes here!";

void enemyAttack(Ship enemyShip, Ship playerShip, const dpp::slashcommand_t& event) {

}

int main() {

	// Seed random
	srand(time(0));

	// Create player ship
	Ship playerShip;
    Ship enemyShip;
    // try: load playerShip.ser {playerShip = playerShip.ser}
    // catch: file read error? {cout << "No previous ship found"}

	// Create bot cluster
	dpp::cluster bot(BOT_TOKEN);

	// Output simple log messages to stdout
	bot.on_log(dpp::utility::cout_logger());

    // Slash command is issued
    bot.on_slashcommand([&](const dpp::slashcommand_t& event) {
        // Check command and call appropriate function
        if (event.command.get_command_name() == "battle")
        {
            // If the player isn't in combat, start combat
            if (playerShip.getActivity() == "combat") {
                event.reply("Already in combat!");
            }
            else {
                event.reply("Finding ship!");
                playerShip.setActivity("combat");
            }

        }
        else if (event.command.get_command_name() == "fire")
        {
            // If the player is in combat, make a roll to hit
            if (playerShip.getActivity() == "combat") {
                if (playerShip.hit() > enemyShip.getAc()) {
                    int damageRoll = playerShip.damageRoll();
                    enemyShip.takeDamage(damageRoll);
                    event.reply("Hit enemy for " + damageRoll + " damage!");
                } else {
                    event.reply("Attack missed!");
                }
            }
            else {
                event.reply("Not in combat!");
            }
        }
        else if (event.command.get_command_name() == "heal")
        {
            if (playerShip.getHp() < playerShip.getMaxHp() && playerShip.getWood() > 0) {
                playerShip.heal();
                event.reply("New HP: " + playerShip.getHp() + '/' + playerShip.getMaxHp());
            }
            else if (playerShip.getHp() == playerShip.getMaxHp()) {
                event.reply("Already at max HP!");
            } else {
                event.reply("We're out of wood!");
            }
        }
        else if (event.command.get_command_name() == "loot")
        {
            event.reply("Looting nearest island!");
        }
        else if (event.command.get_command_name() == "run")
        {
            int success = rand() % 100 + 1;
            if (playerShip.inCombat()) {
                if (success > 50) {
                    event.reply("Ran away successfully!");
                } else {
                    event.reply("Couldn't get away!");
                    enemyAttack(enemyShip, playerShip, event);
                }
                
            } else {
                event.reply("Already out of combat!");
            }
            
        }
    });



	// Registering the commands
    bot.on_ready([&bot](const dpp::ready_t& event) {
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

	// Start the bot
	bot.start(dpp::st_wait);

	return 0;
}


