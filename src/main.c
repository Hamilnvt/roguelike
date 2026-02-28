/* Ideas
 * - name: Dead Kings
 * - features:
 *   > the monsters you fight are divided into an hierarchy (at the top lie the Dead Kings)
 *      - each rank has some properties (from lesser to stronger + some other stuff
 *      - if a monster kills you it can rank up
 *      - if you kill a monster you can decide what to do with him
 *          > reclute it
 *          > kill it
 *          > downrank it
*/

#define STRINGS_IMPLEMENTATION

#include "game.h"

Game game = {0};
size_t main_width = 0;
size_t main_height = 0;

int main(int argc, char **argv)
{
    UNUSED(argc);
    UNUSED(argv);
    
    game_init();

    float current_time = get_time_in_seconds();
    float last_time = current_time;
    float dt = 0.f;

    while (true) {
        current_time = get_time_in_seconds();
        dt = current_time - last_time;
        last_time = current_time;
        game.data.total_time += dt;

        process_pressed_key();
        update_windows();
        update_cursor();
        doupdate();

        advance_all_timers(dt);

        room_update(CURRENT_ROOM);

        napms(16);
    }

    return 0;
}
