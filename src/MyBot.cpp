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
#include <functional>
#include <memory>
#include <mutex>
#include <set>
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
        dpp::embed e;
        e.set_title(r.title).set_description(r.body).set_color(colourFor(r.tone));
        for (const Reply::Field& f : r.fields) e.add_field(f.name, f.value, true);
        if (!r.footer.empty()) e.set_footer(r.footer, "");
        m.add_embed(e);
    }
    if (r.button) {
        m.add_component(dpp::component().add_component(
            dpp::component().set_type(dpp::cot_button).set_style(dpp::cos_success).set_label(r.button->label).set_id(r.button->id)));
    }
    if (r.pingCrew && roleId != 0) {
        m.set_content("<@&" + std::to_string(roleId) + ">" + (m.content.empty() ? "" : " " + m.content));
    }
    m.set_allowed_mentions(true, true, false, false, {}, {});
    if (r.ephemeral) m.set_flags(dpp::m_ephemeral);
    return m;
}

// Works for slash commands and button clicks alike
std::string displayName(const dpp::interaction_create_t& event) {
    std::string nick = event.command.member.get_nickname();
    if (!nick.empty()) return nick;
    const dpp::user& u = event.command.get_issuing_user();
    return u.global_name.empty() ? u.username : u.global_name;
}

// Server owner, Administrator, or Manage Server
bool isAdmin(const dpp::interaction_create_t& event) {
    dpp::guild* g = dpp::find_guild(event.command.guild_id);
    return g != nullptr && g->base_permissions(event.command.member).can(dpp::p_manage_guild);
}

Ctx contextOf(const dpp::interaction_create_t& event) {
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
// Ship channel + crew role setup
//
// Runs by itself whenever the bot sees a server that isn't set up yet: when it's added to a new
// server, and at startup for servers it was already in (e.g. ones an older version joined).
// /setup runs the same steps again as a repair: anything missing is recreated, anything that
// still exists is reused, and the channel's permissions are put back the way they should be.
// ---------------------------------------------------------------------------
const std::string PERMISSION_HELP =
    "Swashbot needs **Manage Channels** and **Manage Roles**, then run `/setup` again.";
constexpr uint64_t CREW_PERMS = dpp::p_view_channel | dpp::p_send_messages | dpp::p_read_message_history;

struct SetupResult {
    bool ok = false;
    std::string summary;
};
using SetupDone = std::function<void(const SetupResult&)>;

// Only one setup per server at a time (startup and /setup could otherwise race and build two channels)
std::mutex setupMutex;
std::set<uint64_t> setupInProgress;

struct SetupGuard {
    explicit SetupGuard(uint64_t id) : guildId(id) {}
    ~SetupGuard() {
        std::lock_guard<std::mutex> lock(setupMutex);
        setupInProgress.erase(guildId);
    }
    uint64_t guildId;
};

std::string joinWithCommas(const std::vector<std::string>& parts) {
    std::string out;
    for (size_t i = 0; i < parts.size(); i++) out += (i == 0 ? "" : ", ") + parts[i];
    return out;
}

// A crew role left behind by an earlier setup (e.g. if the save file was lost)
dpp::snowflake findRoleByName(dpp::snowflake guildId, const std::string& name) {
    dpp::guild* g = dpp::find_guild(guildId);
    if (g == nullptr) return 0;
    for (dpp::snowflake id : g->roles) {
        dpp::role* r = dpp::find_role(id);
        if (r != nullptr && r->name == name) return id;
    }
    return 0;
}

void setupGuild(dpp::cluster& bot, Game& game, dpp::snowflake guildId, SetupDone done) {
    {
        std::lock_guard<std::mutex> lock(setupMutex);
        if (!setupInProgress.insert(guildId).second) {
            done({ false, "Setup is already running." });
            return;
        }
    }
    // Shared by every callback below; frees the "in progress" slot when the last one finishes
    auto guard = std::make_shared<SetupGuard>(guildId);
    auto fixed = std::make_shared<std::vector<std::string>>();

    dpp::guild* g = dpp::find_guild(guildId);
    const std::string guildName = g ? g->name : "";
    const Game::SetupInfo existing = game.setupInfo(guildId);

    // Step 3: save the ids, welcome the crew, and report back
    auto finish = [&bot, &game, guildId, guildName, fixed, guard, done](dpp::snowflake channelId, dpp::snowflake roleId,
                                                                        bool channelIsNew, bool roleIsNew) {
        Reply welcome = game.configure(guildId, guildName, channelId, roleId, unixNow());
        if (channelIsNew) bot.message_create(toMessage(welcome).set_channel_id(channelId));

        // A replacement role starts empty: hand it back to everyone already on the crew
        if (roleIsNew) {
            std::vector<uint64_t> crew = game.crewIds(guildId);
            for (uint64_t userId : crew) bot.guild_member_add_role(guildId, userId, roleId);
            if (!crew.empty()) fixed->push_back("restored the role for " + std::to_string(crew.size()) + " crew");
        }
        std::string summary = fixed->empty() ? "✅ All good" : "✅ " + joinWithCommas(*fixed);
        done({ true, summary + " · <#" + std::to_string(channelId) + ">" });
    };

    // Step 2: the private ship channel (hidden from @everyone, visible to the crew role and the bot)
    auto ensureChannel = [&bot, guildId, existing, fixed, guard, done, finish](dpp::snowflake roleId, bool roleIsNew) {
        dpp::channel* current = existing.channelId != 0 ? dpp::find_channel(existing.channelId) : nullptr;
        if (current != nullptr) {
            // Channel's still there: put its permissions back (the role may be new, or someone may have changed them)
            dpp::channel ch = *current;
            ch.set_permission_overwrite(guildId, dpp::ot_role, 0, dpp::p_view_channel)
              .set_permission_overwrite(roleId, dpp::ot_role, CREW_PERMS, 0)
              .set_permission_overwrite(bot.me.id, dpp::ot_member, CREW_PERMS | dpp::p_embed_links, 0);
            bot.channel_edit(ch, [ch, roleId, roleIsNew, guard, done, finish](const dpp::confirmation_callback_t& cb) {
                if (cb.is_error()) {
                    std::cout << "[swashbot] setup error: " << errorText(cb) << std::endl;
                    done({ false, "❌ Couldn't update the channel. " + PERMISSION_HELP });
                    return;
                }
                finish(ch.id, roleId, false, roleIsNew);
            });
            return;
        }

        dpp::channel ch;
        ch.set_name(SHIP_CHANNEL_NAME)
          .set_guild_id(guildId)
          .set_topic("The deck of our pirate ship. /join to sign on, /help for commands.")
          .add_permission_overwrite(guildId, dpp::ot_role, 0, dpp::p_view_channel)
          .add_permission_overwrite(roleId, dpp::ot_role, CREW_PERMS, 0)
          .add_permission_overwrite(bot.me.id, dpp::ot_member, CREW_PERMS | dpp::p_embed_links, 0);
        bot.channel_create(ch, [roleId, roleIsNew, fixed, guard, done, finish](const dpp::confirmation_callback_t& cb) {
            if (cb.is_error()) {
                std::cout << "[swashbot] setup error: " << errorText(cb) << std::endl;
                done({ false, "❌ Couldn't create the channel. " + PERMISSION_HELP });
                return;
            }
            fixed->push_back("made the channel");
            finish(cb.get<dpp::channel>().id, roleId, true, roleIsNew);
        });
    };

    // Step 1: the crew role
    if (existing.roleId != 0 && dpp::find_role(existing.roleId) != nullptr) {
        ensureChannel(existing.roleId, false);
        return;
    }
    if (dpp::snowflake leftover = findRoleByName(guildId, CREW_ROLE_NAME)) {
        fixed->push_back("reconnected the crew role");
        ensureChannel(leftover, true);
        return;
    }
    dpp::role role;
    role.set_name(CREW_ROLE_NAME).set_guild_id(guildId).set_flags(dpp::r_mentionable).set_colour(0xC8A04A);
    bot.role_create(role, [fixed, guard, done, ensureChannel](const dpp::confirmation_callback_t& cb) {
        if (cb.is_error()) {
            std::cout << "[swashbot] setup error: " << errorText(cb) << std::endl;
            done({ false, "❌ Couldn't create the crew role. " + PERMISSION_HELP });
            return;
        }
        fixed->push_back("made the crew role");
        ensureChannel(cb.get<dpp::role>().id, true);
    });
}

// /setup: an admin re-runs setup to repair a deleted channel or role
void handleSetup(dpp::cluster& bot, Game& game, const dpp::slashcommand_t& event) {
    event.thinking(true);
    setupGuild(bot, game, event.command.guild_id, [event](const SetupResult& r) {
        event.edit_original_response(dpp::message(r.summary));
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
            std::cout << "[swashbot] couldn't add crew role: " << errorText(cb) << std::endl;
            event.edit_original_response(dpp::message("❌ Couldn't give you the crew role. Ask an admin to move the **Swashbot** "
                                                      "role above **" + std::string(CREW_ROLE_NAME) + "**."));
            return;
        }
        event.edit_original_response(toMessage(game.join(ctx)));
        bot.message_create(dpp::message(info.channelId, "⚓ <@" + std::to_string(ctx.userId) + "> joined the crew.")
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
        if (cb.is_error()) out.body += " (Couldn't remove your role; an admin can.)";
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
        cmd("join", "Join the crew"),
        cmd("leave", "Leave the crew"),
        cmd("status", "Ship status"),
        cmd("crew", "Crew rankings"),
        cmd("help", "All commands"),
        cmd("shop", "Upgrade prices"),
        cmd("sail", "Raise anchor (events start)"),
        cmd("anchor", "Drop anchor (events stop)"),
        cmd("hunt", "Find a fight"),
        cmd("raid", "Raid an island for loot"),
        cmd("fire", "Fire at the enemy"),
        cmd("flee", "Escape a fight"),
        cmd("repair", "Patch the hull (1 wood)"),
        cmd("bail", "Bail water in a storm"),
        cmd("douse", "Put out a fire"),
        cmd("salvage", "Grab floating wreckage"),
    };

    dpp::slashcommand leaderboard = cmd("leaderboard", "Top ships everywhere");
    leaderboard.add_option(dpp::command_option(dpp::co_string, "category", "Rank by", false)
        .add_choice(dpp::command_option_choice("Score", std::string("score")))
        .add_choice(dpp::command_option_choice("Gold", std::string("gold")))
        .add_choice(dpp::command_option_choice("Firepower", std::string("firepower")))
        .add_choice(dpp::command_option_choice("Victories", std::string("victories")))
        .add_choice(dpp::command_option_choice("Crew size", std::string("crew"))));
    cmds.push_back(leaderboard);

    dpp::slashcommand buy = cmd("buy", "Spend gold");
    buy.add_option(dpp::command_option(dpp::co_string, "item", "What to buy", true)
        .add_choice(dpp::command_option_choice("Wood", std::string("wood")))
        .add_choice(dpp::command_option_choice("Hull upgrade", std::string("hull")))
        .add_choice(dpp::command_option_choice("Cannon upgrade", std::string("cannons")))
        .add_choice(dpp::command_option_choice("Extra gun", std::string("gun"))));
    buy.add_option(dpp::command_option(dpp::co_integer, "amount", "Wood amount (default 5)", false)
        .set_min_value(int64_t{ 1 }).set_max_value(int64_t{ 100 }));
    cmds.push_back(buy);

    dpp::slashcommand name = cmd("name", "Propose a ship name (admins set it instantly)");
    name.add_option(dpp::command_option(dpp::co_string, "name", "New name", true).set_max_length(int64_t{ 40 }));
    cmds.push_back(name);

    // Admin-only: Discord hides this from members without Manage Server
    dpp::slashcommand setup = cmd("setup", "Repair the ship channel and crew role");
    setup.set_default_permissions(dpp::p_manage_guild);
    cmds.push_back(setup);

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

    // Fires when the bot joins a server, and for every server it's in each time it starts up
    bot.on_guild_create([&bot, &game](const dpp::guild_create_t& event) {
        const dpp::guild& g = event.created;
        game.setGuildName(g.id, g.name);  // for the leaderboard
        if (game.setupInfo(g.id).channelId != 0) return;  // already set up; repairs are what /setup is for

        // Not set up yet (brand new, or joined by an older version of the bot): build the ship now
        const dpp::snowflake systemChannel = g.system_channel_id;
        setupGuild(bot, game, g.id, [&bot, systemChannel, guildId = g.id](const SetupResult& r) {
            std::cout << "[swashbot] auto-setup in guild " << guildId << ": " << r.summary << std::endl;
            // Let the server know the ship has arrived (only on success - no repeated error spam on restarts)
            if (r.ok && systemChannel != 0) {
                bot.message_create(dpp::message(systemChannel, "🏴‍☠️ **Swashbot is here!** `/join` to crew the server's ship."));
            }
        });
    });

    bot.on_slashcommand([&bot, &game](const dpp::slashcommand_t& event) {
        const std::string name = event.command.get_command_name();
        if (name == "setup") return handleSetup(bot, game, event);
        if (name == "join") return handleJoin(bot, game, event);
        if (name == "leave") return handleLeave(bot, game, event);

        Ctx ctx = contextOf(event);
        uint64_t roleId = game.setupInfo(ctx.guildId).roleId;
        if (name == "name") {
            event.reply(toMessage(game.nameShip(ctx, stringParam(event, "name"), isAdmin(event)), roleId));
            return;
        }

        std::string arg;
        int64_t amount = 0;
        if (name == "leaderboard") arg = stringParam(event, "category");
        if (name == "buy") {
            arg = stringParam(event, "item");
            amount = intParam(event, "amount");
        }

        event.reply(toMessage(game.command(name, ctx, arg, amount), roleId));
    });

    // The "Aye" button on a name vote
    bot.on_button_click([&game](const dpp::button_click_t& event) {
        int voteId = Game::parseVoteButtonId(event.custom_id);
        if (voteId == 0) return;
        Reply r = game.voteOnName(contextOf(event), voteId);
        if (r.editsOriginal) event.reply(dpp::ir_update_message, toMessage(r));
        else event.reply(toMessage(r));
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
