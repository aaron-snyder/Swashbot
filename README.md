# Swashbot

A pirate ship Discord bot where **the whole server crews one ship together**.

Every server gets its own ship. Members `/join` the crew, which gives them access to a private ship channel. While the ship is sailing, things keep happening: pirates attack, storms flood the hold, fires break out, fat merchant ships wander past. The crew gets pinged and has a few minutes to respond. The more people who show up and run commands, the less the ship loses. Ships compete on public leaderboards across every server the bot is in.

> The original version I wrote by hand as a sophomore is preserved on the [`Human`](https://github.com/aaron-snyder/Swashbot/tree/Human) branch.

## How a fight plays out

```
@Swashbot Crew
⚔️ Enemy attack!
Pirates! the Black Wake, a Steel hull ship with Iron cannons (3 guns), is bearing down on us!
/fire to fight back • /repair to patch holes • /flee to run
If they're still afloat in 8 minutes they'll board us and plunder the hold.
```

- Every `/fire` shoots one attack roll per gun (d20 + cannon bonus against the enemy's armor class), the same dice system as the original.
- Each person has a short cooldown per action, so **five people firing beat one person spamming**.
- The enemy shoots back every 75 seconds. If they're still afloat at the deadline, or our hull drops below 30%, they board and steal up to 30% of the gold. **The more damage the crew did, the less they take.**
- Sink them first and the crew splits the plunder.

## Commands

| Who | Command | What it does |
|---|---|---|
| Admin | `/setup` | Creates the `#the-ship` channel and the **Swashbot Crew** role (only crew can see the channel) |
| Admin | `/rename <name>` | Names the ship |
| Anyone | `/join`, `/leave` | Sign on/off the crew (adds/removes the role) |
| Anyone | `/status`, `/crew`, `/shop`, `/help` | Ship info, crew rankings, prices, help. Outside the ship channel only you see the reply |
| Anyone | `/leaderboard [score\|gold\|firepower\|victories\|crew]` | Top ships across every server |
| Crew | `/sail`, `/anchor` | Start or stop random events. Anchored ships slowly repair |
| Crew | `/hunt`, `/raid` | Pick a fight, or raid an island for loot (sometimes it's an ambush) |
| Crew | `/fire`, `/flee`, `/repair` | Combat |
| Crew | `/bail`, `/douse`, `/salvage` | Storms, fires, floating wreckage |
| Crew | `/buy wood\|hull\|cannons\|gun` | Spend the ship's gold |

Crew commands only work in the ship channel, so non-players never see the spam.

### Events

| Event | Respond with | Ignore it and... |
|---|---|---|
| ⚔️ Enemy ship | `/fire`, `/repair`, `/flee` | they board and plunder gold and wood |
| 💰 Merchant | `/fire` | she sails off with the loot (no loss) |
| 🐙 Kraken | `/fire`, `/repair` | it drags cargo into the deep |
| 🌊 Storm | `/bail` | hull damage every minute, and a big hit at the end |
| 🔥 Fire | `/douse` | hull damage and burnt wood every minute |
| 🪵 Wreckage | `/salvage` | free wood and gold drift away |

If three alarms in a row get no response, the ship drops anchor by itself, so a quiet server doesn't wake up to a wreck. A badly damaged ship also heads for a cove until someone repairs it.

### Leaderboards

- **Score** = lifetime gold earned + 150 per victory + 300 per kraken + 50 per storm/fire beaten + 25 per raid − 200 per sinking
- **Gold**: current treasury
- **Firepower**: expected damage of one broadside (guns × hit chance × average damage)
- **Victories**, **Crew size**

## How the timed events work (and why it's free)

The hard part of the original project was "how do I make things happen on a timer when a Discord bot only reacts to commands?" Two ideas solve it:

1. **A Discord bot is a long-running program, not a web page.** It keeps an open connection to Discord, so it can run its own timer. D++ has one built in (`bot.start_timer`). Every 10 seconds Swashbot calls `Game::tick(now)`, which checks each ship and posts anything that happened. No threads to manage and no extra services.
2. **Store deadlines, not timers.** A ship doesn't hold a "countdown". It stores timestamps like `nextEventAt` and `event.deadline`, and `tick()` just asks "has that time passed?". All of it is saved to `swashbot.json` after every change, so a crash or restart loses nothing. When the bot comes back, running events get a few minutes of grace instead of resolving instantly.

That means the only requirement is **somewhere to keep one small program running 24/7**, and there are free options for that (below).

## Running it

### 1. Create the bot

1. Go to <https://discord.com/developers/applications> → **New Application** → **Bot** → **Reset Token** and copy the token.
2. No privileged intents are needed.

### 2. Run it with Docker (recommended)

```sh
cp .env.example .env        # then paste your token into .env
docker compose up -d --build
docker compose logs -f      # prints the invite link once it connects
```

Open the invite link, add the bot to your server, and run `/setup` as an admin. The invite asks for View Channels, Send Messages, Embed Links, Read Message History, Manage Channels and Manage Roles.

> If `/join` says it can't give the role, drag Swashbot's own role **above** "Swashbot Crew" in Server Settings → Roles.

### 3. Host it for free

Any always-on Linux machine with Docker works. Free options, at the time of writing:

- **Oracle Cloud "Always Free"**: an Arm (Ampere A1) VM with plenty of resources, free indefinitely. The Dockerfile builds for Arm automatically. Oracle may reclaim *idle* free VMs, and a Discord bot uses very little CPU, so either upgrade the account to Pay-As-You-Go (you still pay nothing while you stay inside the free limits) or keep an eye on it.
- **Google Cloud free tier**: one `e2-micro` VM in `us-west1`, `us-central1` or `us-east1` is free each month. 1 GB of RAM is plenty for Swashbot.
- **A Raspberry Pi or old laptop at home**: free if you have one lying around.

On a fresh Ubuntu VM:

```sh
sudo apt-get update && sudo apt-get install -y docker.io docker-compose-v2 git
git clone https://github.com/aaron-snyder/Swashbot && cd Swashbot
cp .env.example .env && nano .env   # paste the token
sudo docker compose up -d --build
```

`restart: unless-stopped` brings the bot back after crashes and reboots.

Avoid "free web hosting" tiers that put apps to sleep when nobody visits a URL (for example Render's free web services). A sleeping bot can't run timers.

### Building without Docker

Install [D++](https://dpp.dev/) and CMake, then:

```sh
cmake -S . -B build && cmake --build build
ctest --test-dir build          # game engine tests, no Discord needed
DISCORD_TOKEN=... ./build/swashbot
```

On Windows, `vcpkg install dpp nlohmann-json` and open the folder in Visual Studio (it understands CMake). The game engine and tests build even without D++ installed.

## Code layout

| File | What's in it |
|---|---|
| `src/Ship.h/.cpp` | The ship: hull/cannon tiers, d20 attack rolls, damage, repairs, upgrades |
| `src/Game.h/.cpp` | The game engine: commands, random events, the economy, leaderboards. No Discord code, so it's fully testable |
| `src/Storage.cpp` | Saving and loading every ship as JSON |
| `src/MyBot.cpp` | The Discord layer: slash commands, `/setup` channel and role creation, the 10-second heartbeat |
| `tests/GameTests.cpp` | Engine tests, including week-long simulated voyages |

Balance numbers (event frequency, cooldowns, prices) all live in the `Tuning` namespace at the top of `src/Game.h`.
