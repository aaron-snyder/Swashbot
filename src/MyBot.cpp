// Author: Aaron Snyder
// File: MyBot.cpp
// The Discord side of Swashbot. Turns slash commands into Game calls and posts the replies.
//
// The time-based events run off a D++ timer: every TICK_SECONDS the bot asks the game engine
// "did anything happen?" (Game::tick) and posts whatever comes back. The bot process just has to
// stay running somewhere - see README.md for free ways to host it.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <dpp/dpp.h>
#include "Game.h"

namespace {

constexpr uint64_t TICK_SECONDS = 10;
constexpr const char* SHIP_CHANNEL_NAME = "the-ship";
constexpr const char* CREW_ROLE_NAME = "Swashbot Crew";
// View Channel, Send Messages, Embed Links, Read Message History, Manage Channels, Manage Roles
constexpr uint64_t INVITE_PERMISSIONS = 268520464;

int64_t unixNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

uint32_t colourFor(Tone tone) {
    switch (tone) {
    case Tone::Good: return 0x2E9E5B;
    case Tone::Bad: return 0xC0392B;
    case Tone::Alert: return 0xE67E22;
    case Tone::Info: break;
    }
    return 0x3B6EA5;
}

// Reply -> Discord message. Embeds never ping anyone; only the role mention in `content` does.
dpp::message toMessage(const Reply& r, uint64_t roleId = 0) {
    dpp::message m;
    if (r.title.empty()) {
        m.set_content(r.body);
    } else {
        m.add_embed(dpp::embed().set_title(r.title).set_description(r.body).set_color(colourFor(r.tone)));
    }
    if (r.pingCrew && roleId != 0) {
        m.set_content("<@&" + std::to_string(roleId) + ">" + (m.content.empty() ? "" : " " + m.content));
    }
    m.set_allowed_mentions(true, true, false, false, {}, {});
    if (r.ephemeral) m.set_flags(dpp::m_ephemeral);
    return m;
}

std::string displayName(const dpp::slashcommand_t& event) {
    std::string nick = event.command.member.get_nickname();
    if (!nick.empty()) return nick;
    const dpp::user& u = event.command.get_issuing_user();
    return u.global_name.empty() ? u.username : u.global_name;
}

Ctx contextOf(const dpp::slashcommand_t& event) {
    Ctx c;
    c.guildId = event.command.guild_id;
    c.channelId = event.command.channel_id;
    c.userId = event.command.get_issuing_user().id;
    c.userName = displayName(event);
    c.now = unixNow();
    return c;
}

std::string stringParam(const dpp::slashcommand_t& event, const std::string& name) {
    dpp::command_value v = event.get_parameter(name);
    return std::holds_alternative<std::string>(v) ? std::get<std::string>(v) : "";
}

int64_t intParam(const dpp::slashcommand_t& event, const std::string& name) {
    dpp::command_value v = event.get_parameter(name);
    return std::holds_alternative<int64_t>(v) ? std::get<int64_t>(v) : 0;
}

std::string errorText(const dpp::confirmation_callback_t& cb) {
    return cb.get_error().human_readable.empty() ? cb.get_error().message : cb.get_error().human_readable;
}

// ---------------------------------------------------------------------------
// /setup: create the crew role and a private channel only that role can see
// ---------------------------------------------------------------------------
void handleSetup(dpp::cluster& bot, Game& game, const dpp::slashcommand_t& event) {
    const dpp::snowflake guildId = event.command.guild_id;
    Game::SetupInfo existing = game.setupInfo(guildId);
    if (existing.channelId != 0 && dpp::find_channel(existing.channelId) != nullptr) {
        event.reply(dpp::message("Swashbot already lives in <#" + std::to_string(existing.channelId) +
                                 ">. Delete that channel and run `/setup` again if you want to rebuild it.").set_flags(dpp::m_ephemeral));
        return;
    }

    event.thinking(true);
    dpp::guild* g = dpp::find_guild(guildId);
    const std::string guildName = g ? g->name : "";
    const Ctx admin = contextOf(event);

    auto createChannel = [&bot, &game, event, guildId, guildName, admin](dpp::snowflake roleId) {
        const uint64_t crewPerms = dpp::p_view_channel | dpp::p_send_messages | dpp::p_read_message_history;
        dpp::channel ch;
        ch.set_name(SHIP_CHANNEL_NAME)
          .set_guild_id(guildId)
          .set_topic("The deck of our pirate ship. /join to sign on, /help for commands.")
          .add_permission_overwrite(guildId, dpp::ot_role, 0, dpp::p_view_channel)        // hidden from @everyone
          .add_permission_overwrite(roleId, dpp::ot_role, crewPerms, 0)                   // visible to the crew
          .add_permission_overwrite(bot.me.id, dpp::ot_member, crewPerms | dpp::p_embed_links, 0);

        bot.channel_create(ch, [&bot, &game, event, guildId, guildName, admin, roleId](const dpp::confirmation_callback_t& cb) {
            if (cb.is_error()) {
                event.edit_original_response(dpp::message("I couldn't create the ship channel: " + errorText(cb) +
                                                           "\nMake sure I have the **Manage Channels** and **Manage Roles** permissions."));
                return;
            }
            dpp::channel created = cb.get<dpp::channel>();
            Reply welcome = game.configure(guildId, guildName, created.id, roleId, unixNow());
            bot.message_create(toMessage(welcome).set_channel_id(created.id));

            // Sign the admin up straight away
            bot.guild_member_add_role(guildId, admin.userId, roleId, [&game, admin](const dpp::confirmation_callback_t& rc) {
                if (!rc.is_error() && !game.checkJoin(admin)) game.join(admin);
            });
            event.edit_original_response(dpp::message("All set! The ship lives in <#" + std::to_string(created.id) +
                                                      ">. Members use `/join` to get the **" + CREW_ROLE_NAME + "** role and see it."));
        });
    };

    if (existing.roleId != 0 && dpp::find_role(existing.roleId) != nullptr) {
        createChannel(existing.roleId);
        return;
    }
    dpp::role role;
    role.set_name(CREW_ROLE_NAME).set_guild_id(guildId).set_flags(dpp::r_mentionable).set_colour(0xC8A04A);
    bot.role_create(role, [event, createChannel](const dpp::confirmation_callback_t& cb) {
        if (cb.is_error()) {
            event.edit_original_response(dpp::message("I couldn't create the crew role: " + errorText(cb) +
                                                       "\nMake sure I have the **Manage Roles** permission."));
            return;
        }
        createChannel(cb.get<dpp::role>().id);
    });
}

// ---------------------------------------------------------------------------
// /join and /leave: hand out or take back the crew role
// ---------------------------------------------------------------------------
void handleJoin(dpp::cluster& bot, Game& game, const dpp::slashcommand_t& event) {
    Ctx ctx = contextOf(event);
    if (auto no = game.checkJoin(ctx)) {
        event.reply(toMessage(*no));
        return;
    }
    Game::SetupInfo info = game.setupInfo(ctx.guildId);
    event.thinking(true);
    bot.guild_member_add_role(ctx.guildId, ctx.userId, info.roleId, [&bot, &game, event, ctx, info](const dpp::confirmation_callback_t& cb) {
        if (cb.is_error()) {
            event.edit_original_response(dpp::message("I couldn't give you the crew role (" + errorText(cb) + ").\n"
                                                       "An admin needs to give me **Manage Roles** and keep my role above **" + CREW_ROLE_NAME + "**."));
            return;
        }
        event.edit_original_response(toMessage(game.join(ctx)));
        bot.message_create(dpp::message(info.channelId, "\xE2\x9A\x93 <@" + std::to_string(ctx.userId) + "> has signed on to the crew!")
                               .set_allowed_mentions(true, false, false, false, {}, {}));
    });
}

void handleLeave(dpp::cluster& bot, Game& game, const dpp::slashcommand_t& event) {
    Ctx ctx = contextOf(event);
    if (auto no = game.checkLeave(ctx)) {
        event.reply(toMessage(*no));
        return;
    }
    Game::SetupInfo info = game.setupInfo(ctx.guildId);
    Reply r = game.leave(ctx);
    event.thinking(true);
    bot.guild_member_remove_role(ctx.guildId, ctx.userId, info.roleId, [event, r](const dpp::confirmation_callback_t& cb) {
        Reply out = r;
        if (cb.is_error()) out.body += "\n(I couldn't remove the crew role - " + errorText(cb) + ". An admin can remove it by hand.)";
        event.edit_original_response(toMessage(out));
    });
}

std::vector<dpp::slashcommand> buildCommands(dpp::snowflake appId) {
    auto cmd = [appId](const std::string& name, const std::string& description) {
        dpp::slashcommand c(name, description, appId);
        c.set_interaction_contexts({ dpp::itc_guild });  // server-only, no DMs
        return c;
    };

    std::vector<dpp::slashcommand> cmds = {
        cmd("join", "Sign on to the crew and get access to the ship channel"),
        cmd("leave", "Leave the crew"),
        cmd("status", "Show the ship, cargo and what's happening"),
        cmd("crew", "Who's pulling their weight on this ship"),
        cmd("help", "List every Swashbot command"),
        cmd("shop", "See upgrade and supply prices"),
        cmd("sail", "Raise anchor - random events start happening"),
        cmd("anchor", "Drop anchor - no random events until you sail again"),
        cmd("hunt", "Go looking for an enemy ship to fight"),
        cmd("raid", "Raid a nearby island for loot (it might be guarded)"),
        cmd("fire", "Fire a broadside at the enemy"),
        cmd("flee", "Try to escape the current fight"),
        cmd("repair", "Patch the hull using 1 wood"),
        cmd("bail", "Bail water out of the hold during a storm"),
        cmd("douse", "Fight a fire aboard"),
        cmd("salvage", "Haul floating wreckage aboard"),
    };

    dpp::slashcommand leaderboard = cmd("leaderboard", "Top ships across every server");
    leaderboard.add_option(dpp::command_option(dpp::co_string, "category", "What to rank by", false)
        .add_choice(dpp::command_option_choice("Score", std::string("score")))
        .add_choice(dpp::command_option_choice("Gold", std::string("gold")))
        .add_choice(dpp::command_option_choice("Firepower", std::string("firepower")))
        .add_choice(dpp::command_option_choice("Victories", std::string("victories")))
        .add_choice(dpp::command_option_choice("Crew size", std::string("crew"))));
    cmds.push_back(leaderboard);

    dpp::slashcommand buy = cmd("buy", "Spend the ship's gold");
    buy.add_option(dpp::command_option(dpp::co_string, "item", "What to buy", true)
        .add_choice(dpp::command_option_choice("Wood", std::string("wood")))
        .add_choice(dpp::command_option_choice("Hull upgrade", std::string("hull")))
        .add_choice(dpp::command_option_choice("Cannon upgrade", std::string("cannons")))
        .add_choice(dpp::command_option_choice("Extra gun", std::string("gun"))));
    buy.add_option(dpp::command_option(dpp::co_integer, "amount", "How much wood (default 5)", false)
        .set_min_value(int64_t{ 1 }).set_max_value(int64_t{ 100 }));
    cmds.push_back(buy);

    // Admin-only: Discord hides these from members without Manage Server
    dpp::slashcommand setup = cmd("setup", "Create the Swashbot crew channel and role");
    setup.set_default_permissions(dpp::p_manage_guild);
    cmds.push_back(setup);

    dpp::slashcommand rename = cmd("rename", "Give the ship a new name");
    rename.set_default_permissions(dpp::p_manage_guild);
    rename.add_option(dpp::command_option(dpp::co_string, "name", "The new name", true).set_max_length(int64_t{ 40 }));
    cmds.push_back(rename);

    return cmds;
}

}  // namespace

int main() {
    // Flush every log line right away, so `docker logs` shows them live
    std::setvbuf(stdout, nullptr, _IOLBF, 0);

    // Never put the token in code - it ends up on GitHub. Read it from the environment instead.
    const char* token = std::getenv("DISCORD_TOKEN");
    if (token == nullptr || std::string(token).empty()) {
        std::cerr << "Set the DISCORD_TOKEN environment variable to your bot token." << std::endl;
        return 1;
    }
    const char* dataEnv = std::getenv("SWASHBOT_DATA");
    Game game(dataEnv ? dataEnv : "swashbot.json");
    game.load(unixNow());

    dpp::cluster bot(token);
    bot.on_log(dpp::utility::cout_logger());

    // Remember server names for the leaderboard
    bot.on_guild_create([&game](const dpp::guild_create_t& event) {
        game.setGuildName(event.created.id, event.created.name);
    });

    bot.on_slashcommand([&bot, &game](const dpp::slashcommand_t& event) {
        const std::string name = event.command.get_command_name();
        if (name == "setup") return handleSetup(bot, game, event);
        if (name == "join") return handleJoin(bot, game, event);
        if (name == "leave") return handleLeave(bot, game, event);

        std::string arg;
        int64_t amount = 0;
        if (name == "leaderboard") arg = stringParam(event, "category");
        if (name == "rename") arg = stringParam(event, "name");
        if (name == "buy") {
            arg = stringParam(event, "item");
            amount = intParam(event, "amount");
        }

        Ctx ctx = contextOf(event);
        Reply r = game.command(name, ctx, arg, amount);
        event.reply(toMessage(r, game.setupInfo(ctx.guildId).roleId));
    });

    bot.on_ready([&bot, &game](const dpp::ready_t&) {
        if (dpp::run_once<struct register_bot_commands>()) {
            bot.global_bulk_command_create(buildCommands(bot.me.id));

            // The heartbeat that drives every time-based event
            bot.start_timer([&bot, &game](dpp::timer) {
                for (const Announcement& a : game.tick(unixNow())) {
                    bot.message_create(toMessage(a.message, a.roleId).set_channel_id(a.channelId));
                }
            }, TICK_SECONDS);

            std::cout << "Swashbot is afloat! Invite link:\nhttps://discord.com/oauth2/authorize?client_id=" << bot.me.id
                      << "&permissions=" << INVITE_PERMISSIONS << "&scope=bot+applications.commands" << std::endl;
        }
    });

    bot.start(dpp::st_wait);
    return 0;
}
