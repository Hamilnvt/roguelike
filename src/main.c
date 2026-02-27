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
 * 
 * TODO
 * - maybe if an entity spawns on top of another entity it triggers some event:
 *   > on player: ambush
 *   > on another entity: combat
 * - when hovering on entities pressing 'i' shows their stats in the right window
 * - monsters drop key to open doors
 *   > heavy doors can be opened by defeating a King (or higher) in the room and lead to special rooms (?)
 * - each step increments a "timer" and after some time some actions are performed (a monster moves, a new monster spawns, something good/bad happens)
 * - monsters in a new room spawn accordingly to player's level
 * - think about the level
 *   > what does it give to the entity? Does it boosts its stats in some way?
 *   > It can be the lower value for the spawned entities
 *   > but for the player?
 * - Info struct with all the nerdy stuff:
 *   > total time
 *   > seed
 *   > monsters killed
 *   > deaths
 *   ...
 * - when in combat, entities of the same faction do not attack each other (but effects are applied)
 * - equip items (and apply stats and effects (the latter in combat to the defender))
 * - when entering in a room the camera should be centered on you (the player should be more visible in general)
 * - some doors can lead to already visited rooms
 *   > make sure that it's random and it cannot lead to soft locks
 *   > a map would be awesome:
 *      - it could be a graph that shows all the connected rooms
 *      - or just a list of all rooms (with their type) and the connections that you discovered
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

        napms(16); // TODO: fix FPS with the calculated dt
    }

    return 0;
}
