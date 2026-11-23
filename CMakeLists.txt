cmake_minimum_required(VERSION 3.16)
project(Swashbot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

# JSON library for saving ships. Uses an installed copy if there is one, otherwise downloads it.
find_package(nlohmann_json 3.10 QUIET)
if(NOT nlohmann_json_FOUND)
    include(FetchContent)
    FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
    FetchContent_MakeAvailable(json)
endif()

# Source files contain emoji, so tell MSVC they are UTF-8 (GCC/Clang assume it)
if(MSVC)
    add_compile_options(/utf-8)
endif()

# The game engine has no Discord dependency, so it can be tested on its own
add_library(swashbot_game STATIC src/Ship.cpp src/Game.cpp src/Storage.cpp)
target_include_directories(swashbot_game PUBLIC src)
target_link_libraries(swashbot_game PRIVATE nlohmann_json::nlohmann_json)
if(MSVC)
    target_compile_options(swashbot_game PRIVATE /W4)
else()
    target_compile_options(swashbot_game PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_executable(swashbot_tests tests/GameTests.cpp)
target_link_libraries(swashbot_tests PRIVATE swashbot_game)
add_test(NAME game_tests COMMAND swashbot_tests)

# The Discord bot itself needs D++ (https://dpp.dev). Skipped if it isn't installed, so the tests still build.
find_package(dpp QUIET)
if(dpp_FOUND)
    add_executable(swashbot src/MyBot.cpp)
    target_link_libraries(swashbot PRIVATE swashbot_game dpp::dpp)
else()
    message(WARNING "D++ not found - building the game engine and tests only. Install D++ (or use the Dockerfile) to build the bot.")
endif()
