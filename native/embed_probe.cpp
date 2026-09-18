// Host-side embedding probe. The maze game compiled to a C archive
// (examples/maze/capi.elisa) is linked directly and driven by the host, so
// gameplay runs in-process rather than only through a fixture. The second half
// reads a device event through SDL, maps it to a move code, and calls
// maze_step, which is live input driving Elisa gameplay across the boundary.
#include "libmaze.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace {

int move_code_for(SDL_Keycode code) {
    if (code == SDLK_W) {
        return 0;
    }
    if (code == SDLK_S) {
        return 1;
    }
    if (code == SDLK_A) {
        return 2;
    }
    if (code == SDLK_D) {
        return 3;
    }
    return -1;
}

} // namespace

namespace {

std::map<std::string, std::string> read_manifest(const char* path) {
    std::map<std::string, std::string> values;
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() or line.front() == '#') {
            continue;
        }
        const auto separator = line.find('=');
        if (separator != std::string::npos) {
            values[line.substr(0, separator)] = line.substr(separator + 1);
        }
    }
    return values;
}

} // namespace

int main(int argc, char** argv) {
    const char* manifest_path = argc > 1 ? argv[1] : "backends/scene_manifest.txt";
    const int status = maze_start();
    if (status != 1) {
        std::fprintf(stderr, "embed: start returned %d, expected Playing(1)\n", status);
        return 1;
    }
    const int start_x = maze_player_x();
    const int start_y = maze_player_y();
    const int east = maze_step(3);
    const int after_x = maze_player_x();
    const int north_blocked = maze_step(0);
    const int lives = maze_lives();
    const int playing = maze_status();
    std::fprintf(stdout, "embed: start=%d from=(%d,%d) east=%d after_x=%d north_blocked=%d lives=%d status=%d\n",
        status, start_x, start_y, east, after_x, north_blocked, lives, playing);
    if (start_x != 1 or start_y != 1 or east != 1 or after_x != 2 or north_blocked != 0) {
        std::fprintf(stderr, "embed: gameplay exports disagreed\n");
        return 2;
    }
    if (lives != 3 or playing != 1) {
        std::fprintf(stderr, "embed: lives or status wrong\n");
        return 3;
    }

    if (!SDL_Init(SDL_INIT_EVENTS)) {
        std::fprintf(stderr, "embed: SDL3 event init failed: %s\n", SDL_GetError());
        return 4;
    }
    SDL_Event event;
    std::memset(&event, 0, sizeof(event));
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_D;
    event.key.scancode = SDL_GetScancodeFromKey(SDLK_D, nullptr);
    const bool pushed = SDL_PushEvent(&event);
    int direction = -1;
    SDL_Event polled;
    while (SDL_PollEvent(&polled)) {
        if (polled.type == SDL_EVENT_KEY_DOWN) {
            direction = move_code_for(polled.key.key);
        }
    }
    SDL_Quit();
    if (!pushed or direction != 3) {
        std::fprintf(stderr, "embed: device event did not map to a move code\n");
        return 5;
    }
    const int live_moved = maze_step(direction);
    const int live_x = maze_player_x();
    std::fprintf(stdout, "embed live input: sdl_key=d move_code=%d moved=%d player_x=%d\n",
        direction, live_moved, live_x);
    if (live_moved != 1 or live_x != 3) {
        std::fprintf(stderr, "embed: live input did not drive gameplay\n");
        return 6;
    }

    // The rules run through the ABI too: restart, walk onto the hazard at
    // (2,5), and require a life to be spent and the player reset.
    maze_start();
    for (int step = 0; step < 4; ++step) {
        maze_step(1); // south down the open first column
    }
    const int hazard = maze_step(3); // east onto the hazard
    const int hazard_lives = maze_lives();
    const int reset_x = maze_player_x();
    const int reset_y = maze_player_y();
    std::fprintf(stdout, "embed rules: hazard_move=%d lives=%d reset=(%d,%d)\n",
        hazard, hazard_lives, reset_x, reset_y);
    if (hazard != 1 or hazard_lives != 2 or reset_x != 1 or reset_y != 1) {
        std::fprintf(stderr, "embed: hazard rules did not run through the ABI\n");
        return 7;
    }

    // Query surface: the host can inspect the world it drives.
    const int width = maze_width();
    const int height = maze_height();
    const int walls = maze_wall_count();
    const int goal_x = maze_goal_x();
    const int goal_y = maze_goal_y();
    std::fprintf(stdout, "embed world: %dx%d walls=%d goal=(%d,%d) wall(0,1)=%d wall(1,1)=%d\n",
        width, height, walls, goal_x, goal_y, maze_is_wall(0, 1), maze_is_wall(1, 1));
    if (width != 8 or height != 8 or walls != 34 or goal_x != 6 or goal_y != 6) {
        std::fprintf(stderr, "embed: world query disagreed\n");
        return 8;
    }
    if (maze_is_wall(0, 1) != 1 or maze_is_wall(1, 1) != 0) {
        std::fprintf(stderr, "embed: wall query disagreed\n");
        return 9;
    }

    // The two boundaries must agree: the live game's topology is the fixture's.
    const std::map<std::string, std::string> manifest = read_manifest(manifest_path);
    int manifest_walls = 0;
    {
        std::stringstream cells(manifest.count("walls") ? manifest.at("walls") : "");
        std::string cell;
        while (std::getline(cells, cell, ';')) {
            if (not cell.empty()) {
                ++manifest_walls;
            }
        }
    }
    int manifest_goal_x = -1;
    int manifest_goal_y = -1;
    if (manifest.count("goal") != 0) {
        const std::string goal = manifest.at("goal");
        const auto comma = goal.find(',');
        if (comma != std::string::npos) {
            manifest_goal_x = std::atoi(goal.substr(0, comma).c_str());
            manifest_goal_y = std::atoi(goal.substr(comma + 1).c_str());
        }
    }
    std::fprintf(stdout, "embed fixture: walls=%d vs %d goal=(%d,%d) vs (%d,%d)\n",
        walls, manifest_walls, goal_x, goal_y, manifest_goal_x, manifest_goal_y);
    if (manifest_walls != walls or manifest_goal_x != goal_x or manifest_goal_y != goal_y) {
        std::fprintf(stderr, "embed: the live game and the fixture disagree\n");
        return 11;
    }

    // Bridge-call benchmark: the plan asks for the ABI boundary to be measured
    // separately from frame time or submitted bytes, so record the per-call
    // cost of the exported query surface instead of assuming it.
    const int bridge_calls = 100000;
    long long bridge_checksum = 0;
    const auto bridge_start = std::chrono::steady_clock::now();
    for (int call = 0; call < bridge_calls; ++call) {
        bridge_checksum += maze_player_x() + maze_player_y();
    }
    const auto bridge_stop = std::chrono::steady_clock::now();
    const long long bridge_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(bridge_stop - bridge_start).count();
    std::fprintf(stdout, "embed bridge: calls=%d total_ns=%lld per_call_ns=%.1f checksum=%lld\n",
        bridge_calls, bridge_ns, (double)bridge_ns / (double)bridge_calls, bridge_checksum);

    // Play the winning route entirely through the ABI and render the world from
    // the query surface, so a host can drive and display the live game.
    maze_start();
    for (int step = 0; step < 5; ++step) {
        maze_step(1); // south to the open last row
    }
    for (int step = 0; step < 5; ++step) {
        maze_step(3); // east to the goal
    }
    const int won = maze_status();
    const int won_x = maze_player_x();
    const int won_y = maze_player_y();
    std::fprintf(stdout, "embed win: status=%d player=(%d,%d)\n", won, won_x, won_y);
    if (won != 3 or won_x != 6 or won_y != 6) {
        std::fprintf(stderr, "embed: winning route did not win through the ABI\n");
        return 10;
    }
    std::fprintf(stdout, "embed map:\n");
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const char cell = (x == won_x and y == won_y) ? 'P'
                : (x == goal_x and y == goal_y) ? 'G'
                : maze_is_wall(x, y) == 1 ? '#' : '.';
            std::fputc(cell, stdout);
        }
        std::fputc('\n', stdout);
    }
    return 0;
}
