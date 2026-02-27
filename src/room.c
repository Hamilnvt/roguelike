#include "game.h"

char tile_char(const Tile *tile)
{
    switch (tile->type)
    {
    case TILE_FLOOR:  return ' ';
    case TILE_WALL:   return '#';
    case TILE_DOOR:   return tile->open ? 'O' : '0';
    
    case __tile_types_count:
    default: print_error_and_exit("Unreachable tile type %u in tile_char", tile->type);
    }
}

char *tile_type_to_string(TileType type)
{
    switch (type)
    {
    case TILE_FLOOR:  return "floor";
    case TILE_WALL:   return "wall";
    case TILE_DOOR:   return "door";

    case __tile_types_count:
    default: print_error_and_exit("Unreachable tile type %u in tile_type_to_string", type);
    }
}

static inline size_t index_in_room(Room *room, size_t x, size_t y) { return y*room->width + x; }
static inline V2i pos_in_room(Room *room, size_t i)
{ return (V2i){i%room->width, (size_t)(i/room->height)}; }
Tile *tile_at(Room *room, size_t x, size_t y) { return &room->tiles[index_at(x, y, room->width)]; }
size_t room_tiles_count(Room *room) { return room->width*room->height; }
EntitiesIds *entities_at(Room *room, size_t x, size_t y) { return &room->entities_map[index_in_room(room, x, y)]; }
ItemsIndices *items_at(Room *room, size_t x, size_t y) { return &room->items_map[index_in_room(room, x, y)]; }

Tile *get_tile_under_player(void)
{
    V2i pos = PLAYER->pos;
    return tile_at(CURRENT_ROOM, pos.x, pos.y);
}

EntitiesIds *get_entities_under_player(void)
{
    V2i pos = PLAYER->pos;
    return entities_at(CURRENT_ROOM, pos.x, pos.y);
}

Tile *get_looking_tile(void)
{
    V2i dir = direction_vector(PLAYER->direction);
    V2i pos = {
        .x = PLAYER->pos.x + dir.x,
        .y = PLAYER->pos.y + dir.y,
    };
    return tile_at(CURRENT_ROOM, pos.x, pos.y);
}

EntitiesIds *get_looking_entities(void)
{
    V2i dir = direction_vector(PLAYER->direction);
    V2i pos = {
        .x = PLAYER->pos.x + dir.x,
        .y = PLAYER->pos.y + dir.y,
    };
    return entities_at(CURRENT_ROOM, pos.x, pos.y);
}

static inline void set_tile_wall(Tile *tile, bool destructible)
{
    tile->type = TILE_WALL;
    tile->destructible = destructible;
}

static inline void set_tile_wall_random(Tile *tile)
{
    bool destructible = rng_generate(ROOMS_RNG)%2;
    set_tile_wall(tile, destructible);
}

void set_tile_door(Tile *tile, bool open, bool heavy, int leads_to)
{
    
    tile->type = TILE_DOOR;
    tile->open = open;
    tile->heavy = heavy;
    tile->leads_to = leads_to;
}

static inline void set_tile_door_random(Tile *tile)
{
    bool open = rng_generate(ROOMS_RNG)%2;
    bool heavy = open ? false : rng_generate(ROOMS_RNG)%2;
    int leads_to = DOOR_LEADS_TO_NEW_ROOM; // TODO
    set_tile_door(tile, open, heavy, leads_to);
}

Tile *get_door_that_leads_to(int room_index)
{
    for (size_t y = 0; y < CURRENT_ROOM->height; y++) {
        for (size_t x = 0; x < CURRENT_ROOM->width; x++) {
            Tile *tile = tile_at(CURRENT_ROOM, x, y);
            if (tile->type == TILE_DOOR && tile->leads_to == room_index) return tile;
        }
    }
    return NULL;
}

typedef bool (* TilePredicate)(Tile *tile, void *_args);

Tile *get_random_tile_predicate(Room *room, TilePredicate predicate, void *args)
{
    size_t tiles_count = room_tiles_count(room);
    size_t *tiles_indices = malloc(sizeof(size_t)*tiles_count);
    if (!tiles_indices) return NULL;
    for (size_t i = 0; i < tiles_count; i++) tiles_indices[i] = i;
    shuffle_indices(tiles_indices, tiles_count, ROOMS_RNG);

    Tile *tile = NULL;
    for (size_t i = 0; i < tiles_count; i++) {
        size_t index = tiles_indices[i];
        Tile *candidate = &room->tiles[index];
        if (predicate(candidate, args)) {
            tile = candidate;
            break;
        }
    }
    free(tiles_indices);
    return tile;
}

bool predicate_tile_all(Tile *tile, void *_args) { UNUSED(tile); UNUSED(_args); return true; }
static inline Tile *get_random_tile(Room *room) { return get_random_tile_predicate(room, predicate_tile_all, NULL); }

bool predicate_tile_is_floor(Tile *tile, void *_args) { UNUSED(_args); return tile->type == TILE_FLOOR; }
static inline Tile *get_random_floor_tile(Room *room)
{
   return get_random_tile_predicate(room, predicate_tile_is_floor, NULL);
}

typedef struct
{
    Room *room;
} __TilePredicateArgs_PerimeterWall;
bool predicate_tile_is_perimeter_wall(Tile *tile, void *_args)
{
    __TilePredicateArgs_PerimeterWall args = *(__TilePredicateArgs_PerimeterWall *)_args;
    size_t x = tile->pos.x;
    size_t y = tile->pos.y;
    size_t width = args.room->width;
    size_t height = args.room->height;

    bool tile_on_vertical_edge = (y == 0 || y == height-1);
    bool tile_on_horizontal_edge = (x == 0 || x == width-1);
    bool result = tile->type == TILE_WALL && (tile_on_vertical_edge != tile_on_horizontal_edge);
    return result;
}
Tile *get_random_perimeter_wall(Room *room)
{
    return get_random_tile_predicate(room, predicate_tile_is_perimeter_wall,
            &(__TilePredicateArgs_PerimeterWall){room});
}

bool get_random_entity_slot_as_vector(Room *room, V2i *pos)
{
    //size_t tiles_count = room_tiles_count(room);
    //size_t *tiles_indices = malloc(sizeof(size_t)*tiles_count);
    //for (size_t i = 0; i < tiles_count; i++) tiles_indices[i] = i;
    //shuffle_indices(tiles_indices, tiles_count, ROOMS_RNG);
    //Tile *tile = NULL;
    //for (size_t i = 0; i < tiles_count; i++) {
    //    tile = &room->tiles[tiles_indices[i]];
    //    if (predicate(tile)) break;
    //    else tile = NULL;
    //}

    //free(tiles_indices);
    //return tile;

    int tries = 10;
    size_t x;
    size_t y;
    while (tries > 0) {
        x = rng_generate(ROOMS_RNG) % (room->width-1) + 1;
        y = rng_generate(ROOMS_RNG) % (room->height-1) + 1;
        const Tile *tile = tile_at(room, x, y);
        if (tile->type != TILE_WALL) {
            *pos = (V2i){x, y};
            return true;
        } else tries--;
    }
    for (size_t y = 1; y < room->height-1; y++) {
        for (size_t x = 1; x < room->width-1; x++) {
            const Tile *tile = tile_at(room, x, y);
            if (tile->type != TILE_WALL) {
                *pos = (V2i){x, y};
                return true;
            }
        }
    }
    return false;
}

Tile *create_tiles(size_t width, size_t height)
{
    Tile *tiles = malloc(sizeof(Tile)*width*height);
    if (!tiles) return NULL; // TODO handle it when function is used
    for (size_t y = 0; y < height; y++) {
        for (size_t x = 0; x < width; x++) {
            size_t index = index_at(x, y, width);
            tiles[index] = (Tile){ .pos = (V2i){x, y} };
        }
    }
    return tiles;
}

void spawn_random_entity(Room *room)
{
    V2i pos;
    if (!get_random_entity_slot_as_vector(room, &pos)) return;
    Entity e = make_entity_random_at(pos.x, pos.y);
    da_push(&room->entities, e);
}

void spawn_random_item(Room *room)
{
    V2i pos;
    if (!get_random_entity_slot_as_vector(room, &pos)) return;
    Item item = make_item_equipment_random();
    item.pos = pos;
    da_push(&room->items, item);
}

Room *generate_room(void) // TODO: add a from Room to ensure that there is one door
                      //       that leads to the previous room (except for the initial room)
{
    const size_t width  = rng_range(ROOMS_RNG, main_width  / 2, main_width  * 2);
    const size_t height = rng_range(ROOMS_RNG, main_height / 2, main_height * 2);
    Room room = {
        .width = width,
        .height = height,
        .tiles = create_tiles(width, height),
        .entities = (Entities){0},
        .entities_map = malloc(sizeof(EntitiesIds)*width*height), // TODO: handle malloc fail
        .items = (Items){0},
        .items_map = malloc(sizeof(ItemsIndices)*width*height) // TODO: handle malloc fail
    };

    // TODO: si puo' migliorare questo loop
    for (size_t y = 0; y < height; y++) {
        for (size_t x = 0; x < width; x++) {
            if (x == 0 || y == 0 || x == width-1 || y == height-1) {
                set_tile_wall(&room.tiles[index_at(x, y, width)], !WALL_IS_DESTRUCTIBLE);
            }
        }
    }

    Tile *sure_door = get_random_perimeter_wall(&room);
    set_tile_door(sure_door, DOOR_IS_OPEN, !DOOR_IS_HEAVY, DOOR_LEADS_TO_NEW_ROOM);

    size_t doors_count = rng_generate(ROOMS_RNG) % 3;
    for (size_t i = 0; i < doors_count; i++) {
        Tile *door = get_random_perimeter_wall(&room);
        set_tile_door_random(door);
    }

    size_t entities_count = (rng_generate(ROOMS_RNG) % 10) + 1;
    for (size_t i = 0; i < entities_count; i++) {
        spawn_random_entity(&room);
    }

    size_t items_count = (rng_generate(ROOMS_RNG) % 3) == 0 ? 1 : 0; // TODO: make a probability function based on rng
    for (size_t i = 0; i < items_count; i++) {
        spawn_random_item(&room);
    }

    room.index = game.data.rooms.count;
    da_push(&game.data.rooms, room);

    return &game.data.rooms.items[room.index];
}

Room *generate_initial_room(void)
{
    const size_t width  = 151;
    const size_t height = 119;
    Room room = {
        .width = width,
        .height = height,
        .tiles = create_tiles(width, height),
        .entities = (Entities){0},
        .entities_map = malloc(sizeof(EntitiesIds)*width*height), // TODO: handle malloc fail
        .items = (Items){0},
        .items_map = malloc(sizeof(ItemsIndices)*width*height) // TODO: handle malloc fail
    };

    // TODO: si puo' migliorare questo loop
    for (size_t y = 0; y < height; y++) {
        for (size_t x = 0; x < width; x++) {
            if (x == 0 || y == 0 || x == width-1 || y == height-1) {
                set_tile_wall(&room.tiles[index_at(x, y, width)], !WALL_IS_DESTRUCTIBLE);
            }
        }
    }

    Tile *begin_door = tile_at(&room, width/2, 0);
    set_tile_door(begin_door, DOOR_IS_OPEN, !DOOR_IS_HEAVY, DOOR_LEADS_TO_NEW_ROOM);

    room.index = game.data.rooms.count;
    da_push(&game.data.rooms, room);

    return &game.data.rooms.items[room.index];
}

static void clear_and_populate_entities_map(Room *room)
{
    for (size_t i = 0; i < room_tiles_count(room); i++)
        da_clear(&room->entities_map[i]);

    size_t i = 0;
    while (i < room->entities.count) {
        Entity *e = &room->entities.items[i];
        if (entity_is_dead(e)) {
            // TODO: free entity fields
            da_remove(&room->entities, i);
        } else {
            size_t index = index_in_room(room, e->pos.x, e->pos.y);
            da_push(&room->entities_map[index], e->id);
            i++;
        }
    }
}

static void clear_and_populate_items_map(Room *room)
{
    for (size_t i = 0; i < room_tiles_count(room); i++)
        da_clear(&room->items_map[i]);

    size_t i = 0;
    while (i < room->items.count) {
        Item *item = &room->items.items[i];
        if (item->picked_up) {
            // TODO: free item fields
            da_remove(&room->items, i);
        } else {
            size_t index = index_in_room(room, item->pos.x, item->pos.y);
            da_push(&room->items_map[index], i);
            i++;
        }
    }
}

void room_update(Room *room)
{
    clear_and_populate_entities_map(room);
    clear_and_populate_items_map(room);
}
