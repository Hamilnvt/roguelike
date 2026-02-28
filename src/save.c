#include "game.h"

#define save_da(da, save_da_item_fn, file)            \
    do {                                              \
        fwrite(&(da).count, sizeof(size_t), 1, file); \
        for (size_t i = 0; i < (da).count; i++) {     \
            save_da_item_fn(file, (da.items)+i);      \
        }                                             \
    } while (0)

#define load_da(da_ptr, load_da_item_fn, file)                            \
    do {                                                                  \
        da_clear(da_ptr);                                                 \
        size_t count = 0;                                                 \
        if (fread(&count, sizeof(size_t), 1, file) != 1) goto fail;       \
        if (count > 0) {                                                  \
            (da_ptr)->count = count;                                      \
            (da_ptr)->capacity = count;                                   \
            (da_ptr)->items = malloc(count * sizeof((da_ptr)->items[0])); \
            if (!(da_ptr)->items) goto fail;                              \
            da_foreach (*(da_ptr), __typeof__((da_ptr)->items[0]), _item) \
                if (!load_da_item_fn(file, _item)) goto fail;             \
        }                                                                 \
    } while (0)

void save_effect(FILE *f, Effect *effect)
{
    fwrite(&effect->type, sizeof(EffectType), 1, f);
    fwrite(&effect->applied_by, sizeof(int), 1, f);
    fwrite(&effect->value, sizeof(int), 1, f);
    fwrite(&effect->duration, sizeof(int), 1, f);
}
bool load_effect(FILE *f, Effect *effect)
{
    if (fread(&effect->type, sizeof(EffectType), 1, f) != 1) return false;
    if (fread(&effect->applied_by, sizeof(int), 1, f) != 1) return false;
    if (fread(&effect->value, sizeof(int), 1, f) != 1) return false;
    if (fread(&effect->duration, sizeof(int), 1, f) != 1) return false;
    return true;
}

void save_stats(FILE *f, Stats *stats)
{
    fwrite(&stats->attack, sizeof(int), 1, f);
    fwrite(&stats->accuracy, sizeof(int), 1, f);
    fwrite(&stats->health, sizeof(int), 1, f);
    fwrite(&stats->defense, sizeof(int), 1, f);
    fwrite(&stats->agility, sizeof(int), 1, f);
}
bool load_stats(FILE *f, Stats *stats)
{
    if (fread(&stats->attack, sizeof(int), 1, f) != 1) return false;
    if (fread(&stats->accuracy, sizeof(int), 1, f) != 1) return false;
    if (fread(&stats->health, sizeof(int), 1, f) != 1) return false;
    if (fread(&stats->defense, sizeof(int), 1, f) != 1) return false;
    if (fread(&stats->agility, sizeof(int), 1, f) != 1) return false;
    return true;
}

void save_vector(FILE *f, V2i *v)
{
    fwrite(&v->x, sizeof(int), 1, f);
    fwrite(&v->y, sizeof(int), 1, f);
}
bool load_vector(FILE *f, V2i *v)
{
    if (fread(&v->x, sizeof(int), 1, f) != 1) return false;
    if (fread(&v->y, sizeof(int), 1, f) != 1) return false;
    return true;
}

/*
typedef struct
{
    union {
        struct { // Equipment
            EquipmentType equipment_type;
            int durability;
            Stats stats;
            Effects effects;
        };
        struct { // Collectible
            CollectibleType collectible_type;
        };
    };
} Item;
*/

void save_item(FILE *f, Item *item)
{
    fwrite(&item->kind, sizeof(ItemKind), 1, f);
    fwrite(item->name, sizeof(item->name), 1, f);
    save_vector(f, &item->pos);
    fwrite(&item->picked_up, sizeof(bool), 1, f);

    switch (item->kind)
    {
    case ITEM_EQUIPMENT: {
        fwrite(&item->equipment_type, sizeof(EquipmentType), 1, f);
        fwrite(&item->durability, sizeof(int), 1, f);
        save_stats(f, &item->stats);
        save_da(item->effects, save_effect, f); 
    } break;
    case ITEM_COLLECTIBLE: {
        fwrite(&item->collectible_type, sizeof(CollectibleType), 1, f);
    } break;

    case __item_kinds_count:
    default:
        print_error_and_exit("Unreachable item kind %u in save_item", item->kind);
    }
}
bool load_item(FILE *f, Item *item)
{
    if (fread(&item->kind, sizeof(ItemKind), 1, f) != 1) goto fail;
    if (fread(item->name, sizeof(item->name), 1, f) != 1) goto fail;
    if (!load_vector(f, &item->pos)) goto fail;
    if (fread(&item->picked_up, sizeof(bool), 1, f) != 1) goto fail;

    switch (item->kind)
    {
    case ITEM_EQUIPMENT: {
        if (fread(&item->equipment_type, sizeof(EquipmentType), 1, f) != 1) goto fail;
        if (fread(&item->durability, sizeof(int), 1, f) != 1) goto fail;
        if (!load_stats(f, &item->stats)) goto fail;
        load_da(&item->effects, load_effect, f);
    } break;
    case ITEM_COLLECTIBLE: {
        if (fread(&item->collectible_type, sizeof(CollectibleType), 1, f) != 1) goto fail;
    } break;

    case __item_kinds_count:
    default:
        print_error_and_exit("Unreachable item kind %u in save_item", item->kind);
    }

    return true;
fail:
    return false;
}

void save_equipment_slot(FILE *f, EquipmentSlot *slot)
{
    fwrite(&slot->type, sizeof(EquipmentType), 1, f);
    fwrite(&slot->occupied, sizeof(bool), 1, f);
    save_item(f, &slot->item);
}
bool load_equipment_slot(FILE *f, EquipmentSlot *slot)
{
    if (fread(&slot->type, sizeof(EquipmentType), 1, f) != 1) return false;
    if (fread(&slot->occupied, sizeof(bool), 1, f) != 1) return false;
    if (!load_item(f, &slot->item)) return false;
    return true;
}

void save_faction(FILE *f, Faction *faction)
{
    fwrite(&faction->id, sizeof(uint64_t), 1, f);
    fwrite(faction->name, sizeof(faction->name), 1, f);
}
bool load_faction(FILE *f, Faction *faction)
{
    if (fread(&faction->id, sizeof(uint64_t), 1, f) != 1) return false;
    if (fread(faction->name, sizeof(faction->name), 1, f) != 1) return false;
    return true;
}

static_assert(__entity_types_count == 2-1, "save each entity type");
void save_entity(FILE *f, Entity *e)
{
    // POD
    fwrite(&e->id, sizeof(uint64_t), 1, f);
    fwrite(&e->type, sizeof(EntityType), 1, f);
    fwrite(e->name, sizeof(e->name), 1, f);
    fwrite(&e->faction, sizeof(uint64_t), 1, f);
    save_vector(f, &e->pos);
    fwrite(&e->direction, sizeof(Direction), 1, f);
    fwrite(&e->dead, sizeof(bool), 1, f);
    fwrite(&e->rank, sizeof(EntityRank), 1, f);
    fwrite(&e->level, sizeof(size_t), 1, f);
    fwrite(&e->movement_timer, sizeof(float), 1, f);

    fwrite(&e->current_health, sizeof(int), 1, f);
    save_stats(f, &e->base_stats);
    save_stats(f, &e->extra_stats);
    
    save_da(e->equipment, save_equipment_slot, f);
    save_da(e->effects, save_effect, f);

    switch (e->type)
    {
        case ENTITY_PLAYER:
            fwrite(&e->xp, sizeof(size_t), 1, f);
            save_da(e->inventory, save_item, f); 
            break;

        case ENTITY_GENERIC: break;
        case __entity_types_count:
        default:
            print_error_and_exit("Unreachable entity type %u in save_entity", e->type);
    }
}
static_assert(__entity_types_count == 2-1, "load each entity type");
bool load_entity(FILE  *f, Entity *e)
{
    // POD
    if (fread(&e->id, sizeof(uint64_t), 1, f) != 1) goto fail;
    if (fread(&e->type, sizeof(EntityType), 1, f) != 1) goto fail;
    if (fread(e->name, sizeof(e->name), 1, f) != 1) goto fail;
    if (fread(&e->faction, sizeof(uint64_t), 1, f) != 1) goto fail;
    if (!load_vector(f, &e->pos)) goto fail;
    if (fread(&e->direction, sizeof(Direction), 1, f) != 1) goto fail;
    if (fread(&e->dead, sizeof(bool), 1, f) != 1) goto fail;
    if (fread(&e->rank, sizeof(EntityRank), 1, f) != 1) goto fail;
    if (fread(&e->level, sizeof(size_t), 1, f) != 1) goto fail;
    if (fread(&e->movement_timer, sizeof(float), 1, f) != 1) goto fail;

    if (fread(&e->current_health, sizeof(int), 1, f) != 1) goto fail;
    if (!load_stats(f, &e->base_stats)) goto fail;
    if (!load_stats(f, &e->extra_stats)) goto fail;

    load_da(&e->equipment, load_equipment_slot, f);
    e->extra_stats = entity_calculate_extra_stats(e);

    load_da(&e->effects, load_effect, f);

    switch (e->type)
    {
        case ENTITY_PLAYER:
            if (fread(&e->xp, sizeof(size_t), 1, f) != 1) goto fail;
            load_da(&e->inventory, load_item, f); 
            break;

        case ENTITY_GENERIC: break;
        case __entity_types_count:
        default:
            print_error_and_exit("Unreachable entity type %u in load_entity", e->type);
    }

    return true;
fail:
    return false;
}

void save_tile(FILE *f, Tile *tile)
{
    fwrite(&tile->type, sizeof(TileType), 1, f);
    save_vector(f, &tile->pos);
    switch (tile->type)
    {
    case TILE_FLOOR: break;
    case TILE_WALL:
        fwrite(&tile->destructible, sizeof(bool), 1, f);
        break;

    case TILE_DOOR:
        fwrite(&tile->open, sizeof(bool), 1, f);
        fwrite(&tile->heavy, sizeof(bool), 1, f);
        fwrite(&tile->leads_to, sizeof(int), 1, f);
        break;

    case __tile_types_count:
    default:
        print_error_and_exit("Unreachable tile type %u in save_tile", tile->type);
    }
}
bool load_tile(FILE *f, Tile *tile)
{
    if (fread(&tile->type, sizeof(TileType), 1, f) != 1) return false;
    if (!load_vector(f, &tile->pos)) return false;
    switch (tile->type)
    {
    case TILE_FLOOR: break;
    case TILE_WALL:
        if (fread(&tile->destructible, sizeof(bool), 1, f) != 1) return false;
        break;

    case TILE_DOOR:
        if (fread(&tile->open, sizeof(bool), 1, f) != 1) return false;
        if (fread(&tile->heavy, sizeof(bool), 1, f) != 1) return false;
        if (fread(&tile->leads_to, sizeof(int), 1, f) != 1) return false;
        break;

    case __tile_types_count:
    default:
        print_error_and_exit("Unreachable tile type %u in load_tile", tile->type);
    }
    return true;
}

void save_room(FILE *f, Room *room)
{
    fwrite(&room->index, sizeof(size_t), 1, f);

    fwrite(&room->width, sizeof(size_t), 1, f);
    fwrite(&room->height, sizeof(size_t), 1, f);
    for (size_t i = 0; i < room_tiles_count(room); i++)
        save_tile(f, &room->tiles[i]);

    save_da(room->entities, save_entity, f);
    save_da(room->items, save_item, f);

    // NOTE: no need to save entities_map and items_map
}
bool load_room(FILE *f, Room *room)
{
    if (fread(&room->index, sizeof(size_t), 1, f) != 1) goto fail;

    if (fread(&room->width, sizeof(size_t), 1, f) != 1) goto fail;
    if (fread(&room->height, sizeof(size_t), 1, f) != 1) goto fail;
    size_t count = room_tiles_count(room);
    // TODO: I can even avoid to save/load tiles positions, i can recalculate it here
    room->tiles = malloc(sizeof(Tile)*count);
    if (!room->tiles) goto fail;
    for (size_t i = 0; i < count; i++)
        if (!load_tile(f, room->tiles + i)) goto fail;

    load_da(&room->entities, load_entity, f);
    room->entities_map = malloc(sizeof(EntitiesIds)*count);
    if (!room->entities_map) goto fail;
    memset(room->entities_map, 0, sizeof(EntitiesIds)*count);

    load_da(&room->items, load_item, f);
    room->items_map = malloc(sizeof(ItemsIndices)*count);
    if (!room->items_map) goto fail;
    memset(room->items_map, 0, sizeof(ItemsIndices)*count);

    return true;
fail:
    return false;
}

void save_rng(FILE *f, RNG *rng)
{
    for (size_t i = 0; i < 4; i++) fwrite(&rng->state[i], sizeof(uint64_t), 1, f);
}
bool load_rng(FILE *f, RNG *rng)
{
    for (size_t i = 0; i < 4; i++) if (fread(&rng->state[i], sizeof(uint64_t), 1, f) != 1) return false;
    return true;
}

void save_size_t(FILE *f, size_t *n) { fwrite(n, sizeof(size_t), 1, f); }
bool load_size_t(FILE *f, size_t *n) { return fread(n, sizeof(size_t), 1, f) == 1; }

void save_room_description(FILE *f, RoomDescription *desc)
{
    fwrite(desc->name, sizeof(desc->name), 1, f);
    save_da(desc->connections, save_size_t, f);
}
bool load_room_description(FILE *f, RoomDescription *desc)
{

    if (fread(desc->name, sizeof(desc->name), 1, f) != 1) goto fail;
    load_da(&desc->connections, load_size_t, f);
    return true;
fail:
    return false;
}

#define SAVE_FILEPATH "./save.bin"
void save_data(void)
{
    FILE *save_file = fopen(SAVE_FILEPATH, "wb");    
    if (!save_file) {
        print_error_and_exit("Could not save game data to %s", SAVE_FILEPATH);
        return;
    }

    // Player
    save_entity(save_file, PLAYER);

    // POD
    fwrite(&game.data.current_room_index, sizeof(size_t),   1, save_file);
    fwrite(&game.data.total_time,         sizeof(float),    1, save_file);
    fwrite(&game.data.rng_seed,           sizeof(uint64_t), 1, save_file);
    save_rng(save_file, &game.data.rooms_rng);
    save_rng(save_file, &game.data.entities_rng);
    save_rng(save_file, &game.data.items_rng);
    save_rng(save_file, &game.data.combat_rng);

    fwrite(&game.data.entity_id_counter, sizeof(uint64_t), 1, save_file);
    fwrite(&game.data.faction_id_counter, sizeof(uint64_t), 1, save_file);

    save_da(game.data.factions, save_faction, save_file);
    save_da(game.data.rooms, save_room, save_file);
    save_da(game.data.map, save_room_description, save_file);

    fclose(save_file);
    write_message("saved");
}

void init_data(void)
{
    RNG seed_rng = {0};
    rng_init(&seed_rng, time(NULL));
    uint64_t seed = rng_generate(&seed_rng);
    game.data.rng_seed = seed;
    rng_init(&game.data.rooms_rng,    seed++);
    rng_init(&game.data.entities_rng, seed++);
    rng_init(&game.data.items_rng,    seed++);
    rng_init(&game.data.combat_rng,   seed++);

    game.data.player = (Entity){
        .type = ENTITY_PLAYER,
        .rank = RANK_CIVILIAN,
        .level = 1,
    };

    PLAYER->base_stats = entity_calculate_base_stats(PLAYER);

    // TODO: maybe let the player choose "a class" from which to start
    //       and they will have different item slots / initial equipment
    da_push(&PLAYER->equipment, (EquipmentSlot){ .type = EQUIPMENT_HELMET });
    da_push(&PLAYER->equipment, (EquipmentSlot){ .type = EQUIPMENT_CHESTPLATE });
    da_push(&PLAYER->equipment, (EquipmentSlot){ .type = EQUIPMENT_CHAUSSES });
    da_push(&PLAYER->equipment, (EquipmentSlot){ .type = EQUIPMENT_SWORD });
    da_push(&PLAYER->equipment, (EquipmentSlot){ .type = EQUIPMENT_SHIELD });

    PLAYER->current_health = PLAYER->base_stats.health;

    memcpy(PLAYER->name, "Adventurer", 10);

    game.data.entity_id_counter = 1;
    game.data.faction_id_counter = 1;

    Room *initial_room = generate_initial_room();
    game.data.current_room_index = initial_room->index;

    da_push(&game.data.map, (RoomDescription){ .name = "Initial Room" });

    PLAYER->pos = (V2i){initial_room->width/2, initial_room->height/2};
}

bool load_data(void)
{
    FILE *save_file = fopen(SAVE_FILEPATH, "rb");    
    if (!save_file) return false;

    // Player
    if (!load_entity(save_file, &game.data.player)) goto fail;

    // POD
    if (fread(&game.data.current_room_index, sizeof(size_t),   1, save_file) != 1) goto fail;
    if (fread(&game.data.total_time,         sizeof(float),    1, save_file) != 1) goto fail;
    if (fread(&game.data.rng_seed,           sizeof(uint64_t), 1, save_file) != 1) goto fail;
    if (!load_rng(save_file, &game.data.rooms_rng)) goto fail;
    if (!load_rng(save_file, &game.data.entities_rng)) goto fail;
    if (!load_rng(save_file, &game.data.items_rng)) goto fail;
    if (!load_rng(save_file, &game.data.combat_rng)) goto fail;

    if (fread(&game.data.entity_id_counter, sizeof(uint64_t), 1, save_file) != 1) goto fail;
    if (fread(&game.data.faction_id_counter, sizeof(uint64_t), 1, save_file) != 1) goto fail;

    load_da(&game.data.factions, load_faction, save_file);
    load_da(&game.data.rooms, load_room, save_file);
    load_da(&game.data.map, load_room_description, save_file);

    fclose(save_file);
    return true;

fail:
    fclose(save_file);
    return false;
}
