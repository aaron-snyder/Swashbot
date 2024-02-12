#include <iostream>
#include <string>
#include <random>
#include "MyBot.h"
#include "Ship.h"
#include <dpp/dpp.h>

const std::string BOT_TOKEN = "server token goes here!";

int main() {
	// Seed random
	srand(time(0));

	// Create player ship
	Ship playerShip; // Eventually, add checks here to load in previously saved player ships (guild logins?)

	// Create bot cluster
	dpp::cluster bot(BOT_TOKEN);

	// Output simple log messages to stdout
	bot.on_log(dpp::utility::cout_logger());



	// Handle slash command
    // Slash command is issued
    bot.on_slashcommand([&](const dpp::slashcommand_t& event) {
        // Check command and call appropriate function
        if (event.command.get_command_name() == "battle")
        {
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
            if (playerShip.getActivity() == "combat") {
                event.reply("Firing cannons!");
            }
            else {
                event.reply("Not in combat!");
            }
        }
        else if (event.command.get_command_name() == "heal")
        {
            if (playerShip.getHp() < playerShip.getMaxHp()) {
                playerShip.heal();
                event.reply("New HP: " + playerShip.getHp() + '/' + playerShip.getMaxHp());
            }
            else {
                event.reply("Already at max HP!");
            }
        }
        else if (event.command.get_command_name() == "loot")
        {
            playerShip.setActivity("looting");
            event.reply("Looting nearest island!");
        }
        else if (event.command.get_command_name() == "hide")
        {
            playerShip.setActivity("hiding");
            event.reply("Dropping anchor in a nearby cove!");
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


