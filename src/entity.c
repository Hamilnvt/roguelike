#include "game.h"

const char *entity_type_to_string(EntityType type)
{
    switch (type)
    {
    case ENTITY_PLAYER: return "Player";
    case ENTITY_GENERIC: return "Generic";

    case __entity_types_count:
    default:
        print_error_and_exit("Unreachable entity type %u in entity_type_to_string", type);
    }
}

const char *entity_rank_to_string(EntityRank rank)
{
    switch (rank)
    {
    case RANK_CIVILIAN:  return "Civilian";
    case RANK_WARRIOR:   return "Warrior";
    case RANK_NOBLE:     return "Noble";
    case RANK_KING:      return "King";
    case RANK_EMPEROR:   return "Emperor";
    case RANK_WORLDLORD: return "World Lord";

    case __entity_ranks_count:
    default: print_error_and_exit("Unreachable entity rank %u in entity_rank_to_string", rank);
    }
}

char entity_rank_char(EntityRank rank)
{
    switch (rank)
    {
    case RANK_CIVILIAN:  return 'c';
    case RANK_WARRIOR:   return 'w';
    case RANK_NOBLE:     return 'N';
    case RANK_KING:      return 'K';
    case RANK_EMPEROR:   return 'E';
    case RANK_WORLDLORD: return 'W';

    case __entity_ranks_count:
    default: print_error_and_exit("Unreachable entity rank %u in entity_rank_char", rank);
    }
}

bool entity_is_player(Entity *e) { return e == &game.data.player; }
bool entity_is_dead(Entity *entity) { return entity->dead || entity->current_health <= 0; }

static inline Stats stats_sum(Stats s1, Stats s2)
{
    return (Stats){
        .attack   = s1.attack   + s2.attack,
        .accuracy = s1.accuracy + s2.accuracy,
        .health   = s1.health   + s2.health,
        .defense  = s1.defense  + s2.defense,
        .agility  = s1.agility  + s2.agility
    };
}

Stats entity_extra_stats_from_equipment(Entity *e)
{
    Stats stats = {0};
    da_foreach (e->equipment, ItemSlot, item_slot) {
        if (!item_slot->occupied) continue;
        stats = stats_sum(stats, item_slot->item.stats);     
    }
    return stats;
}

#define NO_FACTION 0
static uint64_t faction_id_count = 1;
uint64_t get_random_faction_id(void)
{
    size_t index = rng_generate(ENTITIES_RNG) % (game.data.factions.count+1);
    if (index == game.data.factions.count) {
        Faction faction = {
            .id = faction_id_count++,
            .members = 1
        };
        snprintf(faction.name, sizeof(faction.name), "Faction %lu", faction.id); // TODO: random name
        da_push(&game.data.factions, faction);
        write_message("Faction '%s' arises", faction.name);
        return faction.id;
    } else {
        Faction *faction = &game.data.factions.items[index];
        faction->members++;
        return faction->id;
    }
}

Faction *get_faction_by_id(uint64_t id, size_t *index)
{
    for (size_t i = 0; i < game.data.factions.count; i++) {
        Faction *f = &game.data.factions.items[i];
        if (f->id == id) {
            if (index) *index = i;
            return f;
        }
    }
    return NULL;
}

static uint64_t entity_id_counter = 1;
Entity make_entity_random_at(size_t x, size_t y)
{
    Entity e = {
        .id = entity_id_counter++,
        .type = ENTITY_GENERIC,
        .faction = get_random_faction_id(),
        .pos = (V2i){x, y},
        .direction = rng_generate(ENTITIES_RNG) % __directions_count,
        .rank      = rng_generate(ENTITIES_RNG) % __entity_ranks_count,
        .level     = rng_generate(ENTITIES_RNG) % (10*(e.rank+1)) + 1,
        .base_stats = (Stats){
            .health  = rng_generate(ENTITIES_RNG) % (100*(e.rank+1)),
            .defense = rng_generate(ENTITIES_RNG) % (10*(e.rank+1)),
            .accuracy = rng_generate(ENTITIES_RNG) % (100*(e.rank+1)),
            .attack  = rng_generate(ENTITIES_RNG) % (100*(e.rank+1)),
            .agility = rng_generate(ENTITIES_RNG) % (10*(e.rank+1))
        },
        .movement_timer = rng_generate(ENTITIES_RNG) % 10 + 2
    };

    size_t items_count = rng_generate(ENTITIES_RNG) % (e.rank+1);
    for (size_t i = 0; i < items_count; i++) {
        ItemSlot item_slot = make_item_slot_random();
        Item item = make_item_random_of_type(item_slot.type);
        item.picked_up = true;
        item_slot.item = item;
        item_slot.occupied = true;
        da_push(&e.equipment, item_slot);
    }

    e.extra_stats = entity_extra_stats_from_equipment(&e);
    e.current_health = e.base_stats.health + e.extra_stats.health;

    snprintf(e.name, sizeof(e.name), "Entity %lu", e.id); // TODO: random name

    return e;
}

// TODO: should I search in a specific room or in all the rooms
//       - If I choose the second option I might switch to the generational ID/handle system
Entity *get_entity_by_id(Room *room, uint64_t id)
{
    for (size_t i = 0; i < room->entities.count; i++) {
        Entity *e = &room->entities.items[i];
        if (e->id == id) return e;
    }
    return NULL;
}

Entity make_entity_random(size_t x_low, size_t x_high, size_t y_low, size_t y_high)
{
    size_t x = (rng_generate(ENTITIES_RNG) % (x_high - x_low)) + x_low;
    size_t y = (rng_generate(ENTITIES_RNG) % (y_high - y_low)) + y_low;
    return make_entity_random_at(x, y);
}

static inline void player_killed_entity(Entity *e)
{
    write_message("You killed %s", e->name);
    e->dead = true;
    PLAYER->level += 1;
    PLAYER->xp += e->level;
    // TODO: think about what should happen
}

static inline void entity_killed_player(Entity *e)
{
    write_message("%s killed you", e->name);
    // TODO: think about what should happen
}

static inline void entity_killed_itself(Entity *e)
{
    write_message("%s killed itself", e->name);
    // TODO
}

static inline void player_killed_themselves(void)
{
    write_message("You killed yourself");
    // TODO
}

static inline void entity_killed_entity(Entity *killer, Entity *victim)
{
    write_message("%s killed %s", killer->name, victim->name);
    victim->dead = true;
    // TODO
}

static void dispatch_kill(Entity *killer, Entity *victim)
{
    bool player_is_killer = entity_is_player(killer);
    bool player_is_victim = entity_is_player(victim);

    if (player_is_killer && player_is_victim) player_killed_themselves();
    else if (player_is_killer) player_killed_entity(victim);
    else if (player_is_victim) entity_killed_player(killer);
    else if (killer == victim) entity_killed_itself(killer);
    else entity_killed_entity(killer, victim);
}

static_assert(__death_causes_count == 2, "Make the entities die from each death cause");
// TODO: I don't think this function is really needed, I can make one function for each death cause, reducing complexity
void entity_die(Entity *entity, DeathCause cause, ...)
{
    va_list args;
    va_start(args, cause);

    bool player_is_dying = entity_is_player(entity);

    Entity *attacker;
    switch (cause)
    {
    case DEATH_BY_ENTITY_ATTACK:
        attacker = va_arg(args, Entity*);
        dispatch_kill(attacker, entity);
        break;

    case DEATH_BY_EFFECT: break;
        Effect *effect = va_arg(args, Effect*);
        EffectDefinition *def = get_effect(effect->type);
        if (effect->applied_by == EFFECT_WAS_NOT_APPLIED_BY_ENTITY) {
            write_message("YOU DIED from effect %s", def->name);
        } else {
            Entity *entity = &CURRENT_ROOM->entities.items[effect->applied_by]; // TODO: attenzione
            write_message("YOU DIED from effect %s applied by %s", def->name, entity->name);
        }
        break;

    case __death_causes_count:
    default:
        print_error_and_exit("Unreachable death cause %u in player_die", cause);
    }

    va_end(args);

    if (player_is_dying) {
        // TODO: think about what should happen next
        // - lose levels, items or something else?
        if (PLAYER->level > 1) PLAYER->level -= 1;
        game.data.current_room_index = 0; // maybe go to initial room
                                          // (that could be "safer", less to no monsters, some way to heal...)

        PLAYER->pos = (V2i){CURRENT_ROOM->width/2, 5}; // just to see something
        PLAYER->current_health = 100*PLAYER->level; // TODO okaye, I got it:
                                                   //      levels give base stats and items add them up
                                                   //      so, now I just have to calculate what is the base health
                                                   //      for the level;
        PLAYER->extra_stats = (Stats){0};
        // TODO: recalculate extra stats based on kept equipment/powers
        PLAYER->faction = NO_FACTION;
    } else {
        entity->dead = true;
        // TODO: I don't know, a necromancer here would spawn its last gremlin's wave
    }

    size_t faction_index;
    Faction *faction = get_faction_by_id(entity->faction, &faction_index);
    if (faction) {
        faction->members--; 
        if (faction->members == 0) da_remove(&game.data.factions, faction_index);
    }
}

static_assert(__death_causes_count == 2,
        "Create two wrapper functions for each death cause (one for generic entities and one for player");
static inline void entity_die_from_entity_attack(Entity *entity, Entity *attacker)
{ entity_die(entity, DEATH_BY_ENTITY_ATTACK, attacker); }
static inline void entity_die_from_effect(Entity *entity, Effect *effect)
{ entity_die(entity, DEATH_BY_EFFECT, effect); }

static inline void player_die_from_entity_attack(Entity *attacker)
{ entity_die_from_entity_attack(PLAYER, attacker); }
static inline void player_die_from_effect(Effect *effect) { entity_die_from_effect(PLAYER, effect); }

void entity_add_effect(Entity *entity, Effect effect) { da_push(&entity->effects, effect); }

EntityStatus entity_apply_effects(Entity *entity)
{
    da_foreach (entity->effects, Effect, effect) {
        EffectDefinition *effect_definition = get_effect(effect->type);
        log_this("Applying '%s' to %s", effect_definition->name, entity->name);
        effect_definition->action(effect, entity);
        if (entity_is_dead(entity)) {
            entity_die_from_effect(entity, effect);
            return ESTATUS_DEAD;
        }
    }
    return ESTATUS_OK;
}
static inline EntityStatus player_apply_effects(void) { return entity_apply_effects(PLAYER); }

const char *damage_strings[] = {
    "ouch",
    "ugh",
    "waaah",
};
const size_t damage_strings_count = sizeof(damage_strings)/sizeof(*damage_strings);
static inline const char *get_random_damage_string(void)
{ return damage_strings[rng_generate(COMBAT_RNG) % damage_strings_count]; }

static inline Stats entity_get_stats_sum(Entity *e) { return stats_sum(e->base_stats, e->extra_stats); }

EntityStatus entity_attack_entity(Entity *attacker, Entity *defender)
{
    write_message("%s is attacking %s", attacker->name, defender->name);
    Stats attacker_stats = entity_get_stats_sum(attacker);
    Stats defender_stats = entity_get_stats_sum(defender);
    if (attacker_stats.accuracy <= 0) {
        write_message("%s missed the attack, didn't even try", attacker->name);
        return ESTATUS_OK;
    }
    int multiplier = attacker_stats.accuracy / 100;
    uint64_t accuracy = attacker_stats.accuracy % 100;
    if (accuracy > 0 && (rng_generate(COMBAT_RNG) % 100) >= accuracy) multiplier += 1;
    if (multiplier <= 0) {
        write_message("%s missed the attack, unlucky", attacker->name);
        return ESTATUS_OK;
    }
    int damage = attacker_stats.attack*multiplier;
    int total_damage = damage - defender_stats.defense;
    if (total_damage <= 0) {
        write_message("%s defended %d damage, unbothered", defender->name, damage);
        return ESTATUS_OK;
    }
    write_message("%s inflicted %u damage, %s", attacker->name, total_damage, get_random_damage_string());
    defender->current_health -= total_damage;
    if (entity_is_dead(defender)) {
        entity_die_from_entity_attack(defender, attacker);    
        return ESTATUS_DEAD;
    }
    return ESTATUS_OK;
}

bool entity_can_move(Entity *e)
{
    V2i d = direction_vector(e->direction);
    return (e->pos.x + d.x >= 0
         && (size_t)e->pos.x + d.x < CURRENT_ROOM->width
         && e->pos.y + d.y >= 0
         && (size_t)e->pos.y + d.y < CURRENT_ROOM->height
         && tile_at(CURRENT_ROOM, e->pos.x + d.x, e->pos.y + d.y)->type != TILE_WALL);
}

void entity_set_position_and_direction_entering_room(Entity *entity, Room *room, Tile *door)
{
    Direction direction;
         if (door->pos.x == 0)                              direction = DIRECTION_RIGHT;
    else if (door->pos.y == 0)                              direction = DIRECTION_DOWN;
    else if ((size_t)door->pos.y == room->height-1) direction = DIRECTION_UP;
    else                                                    direction = DIRECTION_LEFT;

    entity->pos = door->pos;
    entity->direction = direction;
    if (!entity_is_player(entity)) entity_move(entity);
}

void entity_interact_with_door(Entity *entity, Tile *door)
{
    if (!door->open || door->heavy || door->leads_to != DOOR_LEADS_TO_NEW_ROOM) return;

    Tile *arrival_door;
    int leaving_room_index = CURRENT_ROOM->index;
    arrival_door = get_door_that_leads_to(leaving_room_index);
    assert(arrival_door != NULL);
    // TODO: remove entity from the entities of this room and add it to the other room
    entity_set_position_and_direction_entering_room(entity, CURRENT_ROOM, arrival_door);
}

void entity_interact_with_entities(Entity *entity, EntitiesIds *entities)
{
    if (entity_apply_effects(entity) == ESTATUS_DEAD) return;

    da_foreach (*entities, uint64_t, id) {
        Entity *other = get_entity_by_id(CURRENT_ROOM, *id);
        if (!other || entity_is_dead(other)) continue;

        if (entity_apply_effects(other) == ESTATUS_DEAD) continue;

        Stats entity_stats = entity_get_stats_sum(entity);
        Stats other_stats = entity_get_stats_sum(other);

        if (entity_stats.agility >= other_stats.agility) {
            if (entity_attack_entity(entity, other) == ESTATUS_DEAD) continue;
            if (entity_attack_entity(other, entity) == ESTATUS_DEAD) return;
        } else {
            if (entity_attack_entity(other, entity) == ESTATUS_DEAD) return;
            if (entity_attack_entity(entity, entity) == ESTATUS_DEAD) continue;
        }
    }
}

void entity_move(Entity *e)
{
    if (!entity_can_move(e)) return;
    V2i *curr_pos = &e->pos;
    V2i dir = direction_vector(e->direction);
    V2i new_pos = {curr_pos->x + dir.x, curr_pos->y + dir.y};
    Tile *tile = tile_at(CURRENT_ROOM, new_pos.x, new_pos.y);
    if (tile->type == TILE_WALL) return;
    ///

    EntitiesIds *entities = entities_at(CURRENT_ROOM, new_pos.x, new_pos.y);

    if (da_is_empty(entities)) {
        if (tile->type == TILE_DOOR) entity_interact_with_door(e, tile);
        else if (tile->type == TILE_FLOOR) *curr_pos = new_pos;
    } else entity_interact_with_entities(e, entities);
}

static inline EntityStatus player_attack_entity(Entity *entity) { return entity_attack_entity(PLAYER, entity); }
static inline EntityStatus entity_attack_player(Entity *entity) { return entity_attack_entity(entity, PLAYER); }

void player_interact_with_entities(EntitiesIds *entities)
{
    if (player_apply_effects() == ESTATUS_DEAD) return;

    da_foreach (*entities, uint64_t, id) {
        Entity *entity = get_entity_by_id(CURRENT_ROOM, *id);
        if (!entity || entity_is_dead(entity)) continue;

        if (entity_apply_effects(entity) == ESTATUS_DEAD) continue;

        Stats player_stats = entity_get_stats_sum(PLAYER);
        Stats entity_stats = entity_get_stats_sum(entity);

        if (player_stats.agility >= entity_stats.agility) {
            if (player_attack_entity(entity) == ESTATUS_DEAD) continue;
            if (entity_attack_player(entity) == ESTATUS_DEAD) return;
        } else {
            if (entity_attack_player(entity) == ESTATUS_DEAD) return;
            if (player_attack_entity(entity) == ESTATUS_DEAD) continue;
        }
    }
}

void player_pickup_items(ItemsIndices *items_indices)
{
    for (size_t i = 0; i < items_indices->count; i++) {
        Item *item = &CURRENT_ROOM->items.items[items_indices->items[i]];
        Item inventory_item = *item;
        da_push(&PLAYER->inventory, inventory_item);
        item->picked_up = true;
        write_message("Picked up %s", inventory_item.name);
    }
}

static inline void player_set_position_and_direction_entering_room(Room *room, Tile *door)
{ entity_set_position_and_direction_entering_room(PLAYER, room, door); }

void player_interact_with_door(Tile *door)
{
    if (door->open) {
        Tile *arrival_door;
        if (door->leads_to == DOOR_LEADS_TO_NEW_ROOM) {
            Room *new_room = generate_room();
            int leaving_room_index = CURRENT_ROOM->index;
            game.data.current_room_index = new_room->index;
            door->leads_to = game.data.rooms.count-1;

            arrival_door = get_random_perimeter_wall(CURRENT_ROOM);
            set_tile_door(arrival_door, DOOR_IS_OPEN, !DOOR_IS_HEAVY, leaving_room_index);
        } else {
            int leaving_room_index = CURRENT_ROOM->index;
            game.data.current_room_index = door->leads_to;
            arrival_door = get_door_that_leads_to(leaving_room_index);
        }
        assert(arrival_door != NULL);
        player_set_position_and_direction_entering_room(CURRENT_ROOM, arrival_door);
    } else if (door->heavy) {

    } else {

    }
}

static_assert(__tile_types_count == 3, "Move player onto all tiles");
void player_move(Direction direction)
{
    PLAYER->direction = direction;
    if (!entity_can_move(&game.data.player)) return;
    V2i *curr_pos = &PLAYER->pos;
    V2i dir = direction_vector(direction);
    V2i new_pos = {curr_pos->x + dir.x, curr_pos->y + dir.y};
    Tile *tile = tile_at(CURRENT_ROOM, new_pos.x, new_pos.y);
    if (tile->type == TILE_WALL) return;

    EntitiesIds *entities = entities_at(CURRENT_ROOM, new_pos.x, new_pos.y);

    ItemsIndices *items = items_at(CURRENT_ROOM, new_pos.x, new_pos.y);

    if (!da_is_empty(entities)) player_interact_with_entities(entities);
    else if (!da_is_empty(items)) {
        player_pickup_items(items);
        *curr_pos = new_pos;
    } else {
        if (tile->type == TILE_DOOR) player_interact_with_door(tile);
        else if (tile->type == TILE_FLOOR) *curr_pos = new_pos;
    }
}

// TODO: non funziona :)
void check_player_look_direction(void)
{
    EntitiesIds *entities = get_looking_entities();
    if (!da_is_empty(entities)) {
        if (!game.show_entities_info.enabled) {
            game.show_entities_info.enabled = true;
            game.show_entities_info.index = 0;
            game.show_entities_info.entities = entities;
        } else {
            if (game.show_entities_info.entities != entities) {
                game.show_entities_info.entities = entities;
                game.show_entities_info.index = 0;
            } else if (game.show_entities_info.index < entities->count-1) {
                game.show_entities_info.index++;
            } else {
                game.show_entities_info.index = 0;
            }
        }
    }
}

void player_equip_all(void)
{
    da_foreach (PLAYER->equipment, ItemSlot, slot) {
        if (slot->occupied) continue;
        size_t i = 0;
        while (i < PLAYER->inventory.count) {
            Item *item = &PLAYER->inventory.items[i];
            if (item->type == slot->type) {
                slot->item = *item;
                slot->occupied = true;
                da_remove(&PLAYER->inventory, i);
            } else i++;
        }
    }
    // TODO: be careful because maybe some other things will give extra stats, not just items and this overwrites all
    PLAYER->extra_stats = entity_extra_stats_from_equipment(PLAYER);
}
