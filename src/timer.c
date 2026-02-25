#include "game.h"

void advance_switch_timer(float dt) { game.switch_timer += dt; }

#define SAVE_TIME_INTERVAL 15.f
void advance_save_timer(float dt)
{
    game.save_timer += dt;
    if (game.save_timer >= SAVE_TIME_INTERVAL) {
        game.save_timer = 0.f;
        save_data();
    }
}

void advance_movement_timers(float dt)
{
    da_foreach (CURRENT_ROOM->entities, Entity, e) {
        e->movement_timer -= dt;
        if (e->movement_timer <= 0) {
            entity_move(e);
            e->movement_timer = rng_generate(ENTITIES_RNG) % 10 + 2;
            e->direction = rng_generate(ENTITIES_RNG) % __directions_count;
        }
    }
}

void advance_all_timers(float dt)
{
    advance_save_timer(dt);
    advance_switch_timer(dt);
    advance_movement_timers(dt);
}

