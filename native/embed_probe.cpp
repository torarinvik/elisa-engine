// Host-side embedding probe. The maze game compiled to a C archive
// (examples/maze/capi.elisa) is linked directly and driven by the host, so
// gameplay runs in-process rather than only through a fixture. The second half
// reads a device event through SDL, maps it to a move code, and calls
// maze_step, which is live input driving Elisa gameplay across the boundary.
#include "libmaze.h"

#include <SDL2/SDL.h>

#include <cstdio>
#include <cstring>

namespace {

int move_code_for(SDL_Keycode code) {
    if (code == SDLK_w) {
        return 0;
    }
    if (code == SDLK_s) {
        return 1;
    }
    if (code == SDLK_a) {
        return 2;
    }
    if (code == SDLK_d) {
        return 3;
    }
    return -1;
}

} // namespace

int main() {
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

    if (SDL_Init(SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "embed: SDL event init failed: %s\n", SDL_GetError());
        return 4;
    }
    SDL_Event event;
    std::memset(&event, 0, sizeof(event));
    event.type = SDL_KEYDOWN;
    event.key.type = SDL_KEYDOWN;
    event.key.keysym.sym = SDLK_d;
    event.key.keysym.scancode = SDL_GetScancodeFromKey(SDLK_d);
    const int pushed = SDL_PushEvent(&event);
    int direction = -1;
    SDL_Event polled;
    while (SDL_PollEvent(&polled) == 1) {
        if (polled.type == SDL_KEYDOWN) {
            direction = move_code_for(polled.key.keysym.sym);
        }
    }
    SDL_Quit();
    if (pushed != 1 or direction != 3) {
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
    return 0;
}
