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
 * - basic interactions
 *   > monster: attack (one from the player and one from the monster)
 *   > door: go to room
 *   - an interaction happens when the player tries to move onto the tile
 *   - possibility to just "look" at a tile to gather information
 * - maybe if an entity spawns on top of another entity it triggers some event:
 *   > on player: ambush
 *   > on another entity: combat
 * - make entities_map a map to a da of pointer to entities so that many entities can exist in the same tile
 * - when hovering on entities pressing 'i' shows their stats in the right window
 * - since now entities can stack, there's no need to check if two of them "collide" they are just put in the same tile
 * - monsters drop key to open doors
 *   > heavy doors can be opened by defeating a King in the room and lead to special rooms (?)
 * - inventory to store/equip items
 * - each step increments a "timer" and after some time some actions are performed (a monster moves, a new monster spawns, something good/bad happens)
 * - clear entities_map at the end of the iteration and repopulate it at the beginning?
 * - monsters in a new room spawn accordingly to player's level
 * - cycle char shown if more than one entity/item on a tile
 * - items + items_map like for entities? They can stay on the ground but they're not tiles nor entities
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
*/

#include <string.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <termios.h>
#include <ncurses.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

#include "dynamic_arrays.h"
#define STRING_IMPLEMENTATION
#include "strings.h"

#define DEBUG true

static inline bool streq(const char *s1, const char *s2) { return strcmp(s1, s2) == 0; }
static inline bool strneq(const char *s1, const char *s2, size_t n) { return strncmp(s1, s2, n) == 0; }

static inline const char *bool_string(bool value) { return value ? "true" : "false"; }

_Noreturn void print_error_and_exit(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    clear();
    printw("ERROR: ");
    vw_printw(stdscr, fmt, ap);
    refresh();
    nodelay(stdscr, FALSE);
    getch();
    exit(1);
}

const char *logpath = "./log.txt";
void log_this(char *format, ...)
{
    if (!DEBUG) return;

    FILE *logfile = fopen(logpath, "a");
    if (logfile == NULL) {
        print_error_and_exit("Could not open log file at `%s`\n", logpath);
    }
    va_list fmt; 

    va_start(fmt, format);
    vfprintf(logfile, format, fmt);
    fprintf(logfile, "\n");

    va_end(fmt);
    fclose(logfile);
}

static inline size_t index_at(size_t x, size_t y, size_t width) { return y*width + x; }

typedef struct { uint64_t state[4]; } RNG;

static inline uint64_t rotl(const uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

static uint64_t splitmix64(uint64_t *x)
{
    uint64_t z = (*x += 0x9e3779b97f4a7c15);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
    z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
    return z ^ (z >> 31);
}

void rng_init(RNG *rng, uint64_t seed)
{
    uint64_t sm_state = seed;
    rng->state[0] = splitmix64(&sm_state);
    rng->state[1] = splitmix64(&sm_state);
    rng->state[2] = splitmix64(&sm_state);
    rng->state[3] = splitmix64(&sm_state);
}

uint64_t rng_generate(RNG *rng)
{
    const uint64_t result = rotl(rng->state[1] * 5, 7) * 9;
    const uint64_t t = rng->state[1] << 17;
    rng->state[2] ^= rng->state[0];
    rng->state[3] ^= rng->state[1];
    rng->state[1] ^= rng->state[2];
    rng->state[0] ^= rng->state[3];
    rng->state[2] ^= t;
    rng->state[3] = rotl(rng->state[3], 45);
    return result;
}

typedef struct
{
    int x;
    int y;
} V2i;

typedef enum
{
    RANK_CIVILIAN,
    RANK_WARRIOR,
    RANK_NOBLE,
    RANK_KING,
    RANK_EMPEROR,
    RANK_WORLDLORD,
    __entity_ranks_count
} EntityRank;

char *entity_rank_to_string(EntityRank rank)
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

typedef enum
{
    TILE_FLOOR,
    TILE_WALL,
    TILE_DOOR,
    __tile_types_count
} TileType;

typedef struct
{
    TileType type;
    V2i pos;
    union {
       bool destructible; // Wall
       struct {           // Door
           bool open;
           bool heavy;
           int leads_to;
       };
    };
} Tile;

typedef struct
{
    size_t width;
    size_t height;
    Tile *tiles;
} TileMap;

typedef enum
{
    EFFECT_HEAL,
    EFFECT_POISON,
    EFFECT_FIRE,
    __effect_types_count
} EffectType;

#define PERSISTENT_EFFECT -1
#define EFFECT_WAS_NOT_APPLIED_BY_ENTITY -1
typedef struct
{
    EffectType type;
    int applied_by; // TODO: it could be dangerous, when an entity dies I should check all the other entities to see
                    // if it applied an effect to it and set it to EFFECT_WAS_NOT_APPLIED_BY_ENTITY
                    // (which is technically false, but it works)
    int value;
    int duration;
} Effect;

static_assert(__effect_types_count == 3, "Make all effects in make_effect");
Effect make_effect(EffectType type, ...)
{
    va_list args;
    va_start(args, type);
    Effect effect = { .type = type };
    switch (type)
    {
    // TODO
    case EFFECT_HEAL:   break;
    case EFFECT_POISON: break;
    case EFFECT_FIRE:   break;

    case __effect_types_count:
    default:
        print_error_and_exit("Unreachable effect type %u in make_effect", type);
    }

    va_end(args);
    return effect;
}

typedef struct
{
    Effect *items; 
    size_t count;
    size_t capacity;
} Effects;

typedef struct
{
    int attack;
    int accuracy;
    int hp;
    int defense;
    int agility;
} Stats;

typedef enum
{
    ITEM_HELMET,
    ITEM_HAT,
    ITEM_GOGGLES,
    ITEM_SCARF,
    ITEM_CHESTPLATE,
    ITEM_CHAUSSES,
    ITEM_SHOES,
    ITEM_GLOVE,
    ITEM_SWORD,
    ITEM_SHIELD,
    ITEM_SCROLL,
    ITEM_STAFF,
    __item_types_count
} ItemType;

#define ITEM_NAME_MAX_LEN 31
typedef struct
{
    ItemType type;
    char name[ITEM_NAME_MAX_LEN + 1];
    int durability;
    Stats stats;

    Effects effects;
} Item;

typedef struct
{
    ItemType type;
    Item item;
} ItemSlot;

typedef struct
{
    ItemSlot *items;
    size_t count;
    size_t capacity;
} Equipment;

typedef struct
{
    Item *items; 
    size_t count;
    size_t capacity;
} Inventory;

typedef enum
{
    DIRECTION_UP,
    DIRECTION_DOWN,
    DIRECTION_LEFT,
    DIRECTION_RIGHT,
    __directions_count
} Direction;

V2i direction_vector(Direction dir)
{
    switch (dir)
    {
    case DIRECTION_UP:    return (V2i){ 0, -1};
    case DIRECTION_DOWN:  return (V2i){ 0,  1};
    case DIRECTION_LEFT:  return (V2i){-1,  0};
    case DIRECTION_RIGHT: return (V2i){ 1,  0};

    case __directions_count:
    default:
        print_error_and_exit("Unreachable direction %u in direction_vector", dir);
    }
}

char get_direction_char(Direction dir)
{
    switch (dir)
    {
    case DIRECTION_UP:    return '^';
    case DIRECTION_DOWN:  return 'v';
    case DIRECTION_LEFT:  return '<';
    case DIRECTION_RIGHT: return '>';

    case __directions_count:
    default:
        print_error_and_exit("Unreachable direction %u in get_direction_char", dir);
    }
}

typedef enum
{
    ENTITY_PLAYER = -1,
    ENTITY_GENERIC = 0, // TODO: just to distinguish from the player (it will not exist later)
    __entity_types_count
} EntityType;

typedef struct
{
    uint64_t id;
    char name[32];
    size_t members; // If members is 0 the faction dies
    // TODO: what else?
} Faction;

typedef struct
{
    Faction *items;
    size_t count;
    size_t capacity;
} Factions;

typedef enum
{
    __power_types_count
} PowerType;

typedef struct
{
    PowerType type;
} Power;

typedef Power Powers[__power_types_count]; // TODO

// TODO:
#define ENTITY_NAME_MAX_LEN 31
typedef struct Entity
{
    uint64_t id;
    EntityType type;
    char name[ENTITY_NAME_MAX_LEN + 1];
    uint64_t faction;
    V2i pos;
    Direction direction;
    bool dead;
    EntityRank rank;
    size_t level;
    float movement_timer;

    Stats stats;

    Equipment equipment;
    Effects effects; 

    union {
        struct { // Player
            size_t xp;
            Inventory inventory;
        };
    };
} Entity;

typedef struct
{
    Entity *items;
    size_t count;
    size_t capacity;
} Entities;

typedef struct
{
    uint64_t *items;
    size_t count;
    size_t capacity;
} EntitiesIds;

char get_entity_char(const Entity *e)
{
    switch (e->rank)
    {
    case RANK_CIVILIAN:  return 'c';
    case RANK_WARRIOR:   return 'w';
    case RANK_NOBLE:     return 'N';
    case RANK_KING:      return 'K';
    case RANK_EMPEROR:   return 'E';
    case RANK_WORLDLORD: return 'W';

    case __entity_ranks_count:
    default: print_error_and_exit("Unreachable entity rank %u in get_entity_char", e->rank);
    }
}

char get_tile_char(const Tile *tile)
{
    switch (tile->type)
    {
    case TILE_FLOOR:  return ' ';
    case TILE_WALL:   return '#';
    case TILE_DOOR:   return tile->open ? 'O' : '0';
    
    case __tile_types_count:
    default: print_error_and_exit("Unreachable tile type %u in get_tile_char", tile->type);
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

typedef struct Room
{
    size_t index;
    TileMap tilemap;
    Entities entities;
    EntitiesIds *entities_map;
    //Items items; // TODO
    //ItemsIds *items_map;
} Room;

typedef struct
{
    Room *items;
    size_t count;
    size_t capacity;
} Rooms;

static inline size_t index_in_room(Room *room, size_t x, size_t y) { return y*room->tilemap.width + x; }
static inline V2i pos_in_room(Room *room, size_t i)
{
    return (V2i){i%room->tilemap.width, (size_t)(i/room->tilemap.height)};
}
static inline Tile *tile_at(Room *room, size_t x, size_t y)
{
    return &room->tilemap.tiles[index_at(x, y, room->tilemap.width)];
}
static inline size_t room_tiles_count(Room *room) { return room->tilemap.width*room->tilemap.height; }
static inline EntitiesIds *entities_at(Room *room, size_t x, size_t y) 
{
    return &room->entities_map[index_in_room(room, x, y)];
}

typedef struct
{
    Entity player;

    size_t current_room_index;
    float total_time;
    uint64_t rng_seed;
    RNG rooms_rng;
    RNG entities_rng;
    RNG items_rng;
    RNG combat_rng;

    Factions factions;
    Rooms rooms;
} Data;

#define MAX_MESSAGES 25
typedef struct
{
    Data data;

    struct {
        char buffer[1024];
        char *lines[MAX_MESSAGES]; 
        size_t head;
        size_t count;
    } messages;


    float save_timer;
    float switch_timer;

    bool looking;
    bool showing_general_info;
    struct {
        bool enabled;
        size_t index;
        EntitiesIds *entities;
    } show_entities_info;
} Game;
static Game game = {0};

#define CURRENT_ROOM (&game.data.rooms.items[game.data.current_room_index])
#define PLAYER (&game.data.player)
static inline bool entity_is_player(Entity *e) { return e == &game.data.player; }
static inline bool entity_is_dead(Entity *entity) { return entity->stats.hp <= 0 || entity->dead; }

static inline Tile *get_tile_under_player(void)
{
    V2i pos = PLAYER->pos;
    return tile_at(CURRENT_ROOM, pos.x, pos.y);
}

static inline EntitiesIds *get_entities_under_player(void)
{
    V2i pos = PLAYER->pos;
    return entities_at(CURRENT_ROOM, pos.x, pos.y);
}

static inline Tile *get_looking_tile(void)
{
    V2i dir = direction_vector(PLAYER->direction);
    V2i pos = {
        .x = PLAYER->pos.x + dir.x,
        .y = PLAYER->pos.y + dir.y,
    };
    return tile_at(CURRENT_ROOM, pos.x, pos.y);
}

static inline EntitiesIds *get_looking_entities(void)
{
    V2i dir = direction_vector(PLAYER->direction);
    V2i pos = {
        .x = PLAYER->pos.x + dir.x,
        .y = PLAYER->pos.y + dir.y,
    };
    return entities_at(CURRENT_ROOM, pos.x, pos.y);
}

static inline uint64_t rooms_rng_generate   (void) { return rng_generate(&game.data.rooms_rng); }
static inline uint64_t entities_rng_generate(void) { return rng_generate(&game.data.entities_rng); }
static inline uint64_t items_rng_generate   (void) { return rng_generate(&game.data.items_rng); }
static inline uint64_t combat_rng_generate   (void) { return rng_generate(&game.data.combat_rng); }

void rng_log(RNG rng)
{
    log_this("RNG seed: %016llx", game.data.rng_seed); 
    log_this("RNG current internal state: %016llx - %016llx - %016llx - %016llx",
           (unsigned long long)rng.state[0], 
           (unsigned long long)rng.state[1], 
           (unsigned long long)rng.state[2], 
           (unsigned long long)rng.state[3]);
    log_this("-----------------------------\n");
}

static inline void add_effect_to_entity(Effect effect, Entity *entity) { da_push(&entity->effects, effect); }

#define WALL_IS_DESTRUCTIBLE true
static inline void set_tile_wall(Tile *tile, bool destructible)
{
    tile->type = TILE_WALL;
    tile->destructible = destructible;
}

static inline void set_tile_wall_random(Tile *tile)
{
    bool destructible = rooms_rng_generate()%2;
    set_tile_wall(tile, destructible);
}

#define DOOR_IS_OPEN true
#define DOOR_IS_HEAVY true
#define DOOR_LEADS_TO_NEW_ROOM -1
static inline void set_tile_door(Tile *tile, bool open, bool heavy, int leads_to)
{
    
    tile->type = TILE_DOOR;
    tile->open = open;
    tile->heavy = heavy;
    tile->leads_to = leads_to;
}

static inline void set_tile_door_random(Tile *tile)
{
    bool open = rooms_rng_generate()%2;
    bool heavy = open ? false : rooms_rng_generate()%2;
    int leads_to = DOOR_LEADS_TO_NEW_ROOM; // TODO
    set_tile_door(tile, open, heavy, leads_to);
}

void shuffle_tiles_array(size_t *tiles_indices, size_t tiles_count)
{
    size_t tmp;
    for (size_t i = tiles_count-1; i >= 1; i--) {
        size_t j = rooms_rng_generate() % i;
        tmp = tiles_indices[i];
        tiles_indices[i] = tiles_indices[j];
        tiles_indices[j] = tmp;
    }
}

void shuffle_entities_array(size_t *entities_indices, size_t entities_count)
{
    size_t tmp;
    for (size_t i = entities_count-1; i >= 1; i--) {
        size_t j = entities_rng_generate() % i;
        tmp = entities_indices[i];
        entities_indices[i] = entities_indices[j];
        entities_indices[j] = tmp;
    }
}

typedef bool (* TilePredicate)(Tile *tile, void *_args);

Tile *get_random_tile_predicate(Room *room, TilePredicate predicate, void *args)
{
    size_t tiles_count = room_tiles_count(room);
    size_t *tiles_indices = malloc(sizeof(size_t)*tiles_count);
    if (!tiles_indices) return NULL;
    for (size_t i = 0; i < tiles_count; i++) tiles_indices[i] = i;
    shuffle_tiles_array(tiles_indices, tiles_count);

    Tile *tile = NULL;
    for (size_t i = 0; i < tiles_count; i++) {
        size_t index = tiles_indices[i];
        Tile *candidate = &room->tilemap.tiles[index];
        if (predicate(candidate, args)) {
            tile = candidate;
            break;
        }
    }
    free(tiles_indices);
    return tile;
}

bool predicate_tile_all(Tile *tile, void *_args) { (void)tile; (void)_args; return true; }
static inline Tile *get_random_tile(Room *room) { return get_random_tile_predicate(room, predicate_tile_all, NULL); }

bool predicate_tile_is_floor(Tile *tile, void *_args) { (void)_args; return tile->type == TILE_FLOOR; }
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
    size_t width = args.room->tilemap.width;
    size_t height = args.room->tilemap.height;

    bool tile_on_vertical_edge = (y == 0 || y == height-1);
    bool tile_on_horizontal_edge = (x == 0 || x == width-1);
    bool result = tile->type == TILE_WALL && (tile_on_vertical_edge != tile_on_horizontal_edge);
    return result;
}
static inline Tile *get_random_perimeter_wall(Room *room)
{
    return get_random_tile_predicate(room, predicate_tile_is_perimeter_wall,
            &(__TilePredicateArgs_PerimeterWall){room});
}

bool get_random_entity_slot_as_vector(Room *room, V2i *pos)
{
    //size_t tiles_count = room_tiles_count(room);
    //size_t *tiles_indices = malloc(sizeof(size_t)*tiles_count);
    //for (size_t i = 0; i < tiles_count; i++) tiles_indices[i] = i;
    //shuffle_entities_array(tiles_indices, tiles_count);
    //Tile *tile = NULL;
    //for (size_t i = 0; i < tiles_count; i++) {
    //    tile = &room->tilemap.tiles[tiles_indices[i]];
    //    if (predicate(tile)) break;
    //    else tile = NULL;
    //}

    //free(tiles_indices);
    //return tile;

    int tries = 10;
    size_t x;
    size_t y;
    while (tries > 0) {
        x = rooms_rng_generate() % (room->tilemap.width-1) + 1;
        y = rooms_rng_generate() % (room->tilemap.height-1) + 1;
        const Tile *tile = tile_at(room, x, y);
        if (tile->type != TILE_WALL) {
            *pos = (V2i){x, y};
            return true;
        } else tries--;
    }
    for (size_t y = 1; y < room->tilemap.height-1; y++) {
        for (size_t x = 1; x < room->tilemap.width-1; x++) {
            const Tile *tile = tile_at(room, x, y);
            if (tile->type != TILE_WALL) {
                *pos = (V2i){x, y};
                return true;
            }
        }
    }
    return false;
}

void add_message(const char *message)
{
    if (game.messages.lines[game.messages.head]) free(game.messages.lines[game.messages.head]);

    game.messages.lines[game.messages.head] = strdup(message);
    game.messages.head = (game.messages.head + 1) % MAX_MESSAGES;

    if (game.messages.count < MAX_MESSAGES) game.messages.count++;
}

void write_message(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    memset(game.messages.buffer, 0, sizeof(game.messages.buffer));
    vsnprintf(game.messages.buffer, sizeof(game.messages.buffer), fmt, ap);
    va_end(ap);
    log_this("> %s", game.messages.buffer);
    add_message(game.messages.buffer);
}
static inline void write_string_to_message(String string) { write_message(S_FMT, S_ARG(string)); }

#define NO_FACTION 0
static uint64_t faction_id_count = 1;
uint64_t get_random_faction_id(void)
{
    size_t index = entities_rng_generate() % (game.data.factions.count+1);
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
        .direction = entities_rng_generate() % __directions_count,
        .rank      = entities_rng_generate() % __entity_ranks_count,
        .level     = entities_rng_generate() % (10*(e.rank+1)) + 1,
        .stats = (Stats){
            .hp      = entities_rng_generate() % (100*(e.rank+1)),
            .defense = entities_rng_generate() % (10*(e.rank+1)),
            .accuracy = entities_rng_generate() % (100*(e.rank+1)),
            .attack  = entities_rng_generate() % (100*(e.rank+1)),
            .agility = entities_rng_generate() % (10*(e.rank+1))
        },
        .movement_timer = entities_rng_generate() % 10 + 2
    };

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

static inline Entity make_entity_random(size_t x_low, size_t x_high, size_t y_low, size_t y_high)
{
    size_t x = (entities_rng_generate() % (x_high - x_low)) + x_low;
    size_t y = (entities_rng_generate() % (y_high - y_low)) + y_low;
    return make_entity_random_at(x, y);
}

void spawn_random_entity(Room *room)
{
    V2i pos;
    if (!get_random_entity_slot_as_vector(room, &pos)) return;
    Entity e = make_entity_random_at(pos.x, pos.y);
    da_push(&room->entities, e);
    EntitiesIds *entities = entities_at(room, pos.x, pos.y);
    da_push(entities, e.id);
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

Room *generate_room(size_t width, size_t height) // TODO: add a from Room to ensure that there is one door
                                                 //       that leads to the previous room (except for the initial room)
{
    Room room = {
        .tilemap = (TileMap){
            .width = width,
            .height = height,
            .tiles = create_tiles(width, height)
        },
        .entities = (Entities){0},
        .entities_map = malloc(sizeof(EntitiesIds)*width*height) // TODO: handle malloc fail
    };

    // TODO: si puo' migliorare questo loop
    for (size_t y = 0; y < height; y++) {
        for (size_t x = 0; x < width; x++) {
            if (x == 0 || y == 0 || x == width-1 || y == height-1) {
                set_tile_wall(&room.tilemap.tiles[index_at(x, y, width)], !WALL_IS_DESTRUCTIBLE);
            }
        }
    }

    Tile *sure_door = get_random_perimeter_wall(&room);
    set_tile_door(sure_door, DOOR_IS_OPEN, !DOOR_IS_HEAVY, DOOR_LEADS_TO_NEW_ROOM);

    size_t doors_count = rooms_rng_generate() % 3;
    for (size_t i = 0; i < doors_count; i++) {
        Tile *door = get_random_perimeter_wall(&room);
        set_tile_door_random(door);
    }

    size_t entities_count = (rooms_rng_generate() % 10) + 1;
    for (size_t i = 0; i < entities_count; i++) {
        spawn_random_entity(&room);
    }

    room.index = game.data.rooms.count;
    da_push(&game.data.rooms, room);

    return &game.data.rooms.items[room.index];
}

#define EFFECTACTION_PARAMETERS Effect *effect, Entity *actor
typedef void (* EffectAction)(EFFECTACTION_PARAMETERS);
typedef struct
{
    const char *name;
    EffectAction action;
} EffectDefinition;

#define UNUSED_EFFECTACTION_PARAMETERS \
    (void)effect;                      \
    (void)actor;                       \

void effect_heal(EFFECTACTION_PARAMETERS)
{
    UNUSED_EFFECTACTION_PARAMETERS;

    write_message("Heal!");
}

void effect_poison(EFFECTACTION_PARAMETERS)
{
    UNUSED_EFFECTACTION_PARAMETERS;

    write_message("Poison!");
}

void effect_fire(EFFECTACTION_PARAMETERS)
{
    UNUSED_EFFECTACTION_PARAMETERS;

    write_message("Fire!");
}

static_assert(__effect_types_count == 3, "Add all effects to effects_definitions");
static EffectDefinition effects_definitions[__effect_types_count] = {
    [EFFECT_HEAL]   = { "Heal",   effect_heal },
    [EFFECT_POISON] = { "Poison", effect_poison },
    [EFFECT_FIRE]   = { "Fire",   effect_fire }
};

EffectDefinition *get_effect(EffectType type)
{
    if (type >= 0 && type < __effect_types_count) return &effects_definitions[type];
    else print_error_and_exit("Unreachable effect type %u in get_effect", type);
}

static inline void advance_switch_timer(float dt) { game.switch_timer += dt; }

float get_time_in_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (float)ts.tv_sec + ((float)ts.tv_nsec / 1e9);
}

/* Colors */
typedef enum
{
    R_COLOR_BACKGROUND = 10,
    R_COLOR_FOREGROUND,
    R_COLOR_YELLOW,
    R_COLOR_RED,
    R_COLOR_BLUE
} R_Color;

typedef enum
{
    KEY_NULL  = 0,
    TAB       = 9,
    ENTER     = 13,
    ESC       = 27,

    ALT_0     = 1000,
    ALT_1,
    ALT_2,
    ALT_3,
    ALT_4,
    ALT_5,
    ALT_6,
    ALT_7,
    ALT_8,
    ALT_9, // NOTE: ALT_digit sequences must be consecutive

    ALT_i,
    ALT_k,
    ALT_j,
    ALT_h,
    ALT_l,

    ALT_c,
    ALT_C,
    ALT_K,
    ALT_J,
    ALT_H,
    ALT_L,

    ALT_m,
    ALT_n,
    ALT_p,

    CTRL_ALT_C,
    CTRL_ALT_D,
    CTRL_ALT_E,
    CTRL_ALT_K,
    CTRL_ALT_J,
    CTRL_ALT_H,
    CTRL_ALT_L,

    ALT_BACKSPACE,
    ALT_COLON,
} Key;

typedef void (* UpdateWindowFunction) (void);

typedef struct
{
    WINDOW *win;
    UpdateWindowFunction update;
    size_t height;
    size_t width;
} Window;

static Window win_main = {0};
static Window win_bottom = {0};
static Window win_right = {0};
static Window *windows[] = {
    &win_main,
    &win_bottom,
    &win_right,
};
const size_t windows_count = sizeof(windows)/sizeof(*windows);

/* Pairs */
typedef enum
{
    R_PAIR = 1,
    R_PAIR_INV,
} ColorPair;

/* Windows */
static size_t terminal_height;
static size_t terminal_width;
static inline void get_terminal_size(void) { getmaxyx(stdscr, terminal_height, terminal_width); }

Window create_window(int x, int y, int w, int h, int color_pair, UpdateWindowFunction update)
{
    Window win = {0};
    win.win = newwin(h, w, y, x);
    assert(update);
    win.update = update;
    win.height = h;
    win.width = w;
    if (has_colors() && can_change_color()) wbkgd(win.win, COLOR_PAIR(color_pair));
    return win;
}

void update_window_main(void)
{
    for (size_t y = 0; y < CURRENT_ROOM->tilemap.height; y++) {
        for (size_t x = 0; x < CURRENT_ROOM->tilemap.width; x++) {
            const Tile *tile = tile_at(CURRENT_ROOM, x, y);
            EntitiesIds *entities = entities_at(CURRENT_ROOM, x, y);
            char c;
            if (da_is_empty(entities)) c = get_tile_char(tile);
            else {
                if (tile->type == TILE_FLOOR) {
                    Entity *e = get_entity_by_id(CURRENT_ROOM, entities->items[(size_t)game.switch_timer%entities->count]);
                    if (!e || entity_is_dead(e)) continue;
                    c = get_entity_char(e);
                } else {
                    size_t index = (size_t)game.switch_timer % (entities->count+1);
                    if (index == entities->count) c = get_tile_char(tile);
                    else {
                        Entity *e = get_entity_by_id(CURRENT_ROOM, entities->items[index]);
                        if (!e || entity_is_dead(e)) continue;
                        c = get_entity_char(e);
                    }
                }
            }
            mvwaddch(win_main.win, y, x, c);
        }
    }

    Entity *pe = PLAYER;
    mvwaddch(win_main.win, pe->pos.y, pe->pos.x, '@');
}

void update_window_bottom(void)
{
    werase(win_bottom.win);
    //box(win_bottom.win, 0, 0); // TODO: just to try

    // --- SECTION 1: MESSAGE LOG ---
    // We reserve lines 1 to messages_display_height for text
    const size_t messages_display_height = 5; // Adjust based on window size
    const size_t start_y = 0;
    const size_t start_x = 0;

    // Iterate backwards from the newest message
    size_t count_printed = 0;
    for (size_t i = 0; i < game.messages.count && count_printed < (size_t)messages_display_height; i++) {
        // Calculate ring buffer index walking backwards
        // (head - 1 - i) with wrap-around handling
        size_t idx = (game.messages.head - 1 - i + MAX_MESSAGES) % MAX_MESSAGES;
        
        char *line = game.messages.lines[idx];
        if (!line) continue;

        // Visual flair: Newest message is bright, older ones are dim
        if (i == 0) wattron(win_bottom.win, A_BOLD);
        else wattron(win_bottom.win, A_DIM);

        // Print lines from bottom-up within the allocated space
        mvwprintw(win_bottom.win, start_y + (messages_display_height - 1) - count_printed, start_x, "> %s", line);
        
        if (i == 0) wattroff(win_bottom.win, A_BOLD);
        else wattroff(win_bottom.win, A_DIM);

        count_printed++;
    }

    // Separator line between Log and Tile Info
    mvwhline(win_bottom.win, start_y + messages_display_height, 1, ACS_HLINE, win_bottom.width - 1);

    // --- SECTION 2: TILE INSPECTION ---
    Tile *tile = game.looking ? get_looking_tile() : get_tile_under_player();
    EntitiesIds *entities = game.looking ? get_looking_entities() : get_entities_under_player();

    size_t line = start_y + messages_display_height + 1; // Start below separator

    wmove(win_bottom.win, line++, start_x);
    switch (tile->type)
    {
    case TILE_FLOOR: wprintw(win_bottom.win, "Floor."); break;
    case TILE_WALL:  wprintw(win_bottom.win, "Wall."); break;
    case TILE_DOOR:
        if (tile->open) {
            wprintw(win_bottom.win, "Open door (leads to room %d).", tile->leads_to >= 0 ? tile->leads_to : -1);
        } else {
            wprintw(win_bottom.win, "Closed door (%s).", tile->heavy ? "Heavy" : "Normal");
        }
        break;

    case __tile_types_count:
    default: break;
    }

    if (!da_is_empty(entities)) {
        mvwprintw(win_bottom.win, line++, start_x, "Here: ");
        for (size_t i = 0; i < entities->count; i++) {
            Entity *e = get_entity_by_id(CURRENT_ROOM, entities->items[i]);
            char entity_marker = (game.show_entities_info.enabled && i == game.show_entities_info.index) ? '*' : '-';
            
            // Comma separation logic
            if (i > 0) wprintw(win_bottom.win, ", ");
            
            wprintw(win_bottom.win, "%c%s (Lvl %zu)", entity_marker, e->name, e->level);
        }
    }
}

void update_window_bottom2(void)
{
    Tile *tile = get_tile_under_player();
    EntitiesIds *entities = get_entities_under_player();

    box(win_bottom.win, 0, 0);

    size_t line = 1;
    wmove(win_bottom.win, line++, 1);
    switch (tile->type)
    {
    case TILE_FLOOR: wprintw(win_bottom.win, "Same old boring floor"); break;
    case TILE_WALL:  wprintw(win_bottom.win, "A wall... wait, how'd I get up here?"); break;
    case TILE_DOOR:
        if (tile->open) {
            wprintw(win_bottom.win, "An open door that leads to ");
            if (tile->leads_to >= 0) wprintw(win_bottom.win, "room %d", tile->leads_to);
            else wprintw(win_bottom.win, "a new room");
        } else {
            wprintw(win_bottom.win, "A closed door. ");
            if (tile->heavy) wprintw(win_bottom.win,
                    "It's massive. It requires an extraordinary act of strength to open it.");
            else wprintw(win_bottom.win, "It seems that it can be opened, I wonder how, though.");
        }
        break;

    case __tile_types_count:
    default: print_error_and_exit("Unreachable tile type %u in update_window_bottom", tile->type);
    }

    if (!da_is_empty(entities)) {
        mvwprintw(win_bottom.win, line++, 1, "with the welcoming presence of:");
        for (size_t i = 0; i < entities->count; i++) {
            Entity *e = &CURRENT_ROOM->entities.items[entities->items[i]];
            char entity_selected_char = game.show_entities_info.enabled
                && i == game.show_entities_info.index ? '+' : '-';
            mvwprintw(win_bottom.win, line++, 1, "%c %s, %s level %zu", entity_selected_char, e->name,
                    entity_rank_to_string(e->rank), e->level);
        }
    }
}

void show_entity_info(Entity *e)
{
    size_t line = 1;
    mvwprintw(win_right.win, line++, 1, "%s", e->name);
    mvwprintw(win_right.win, line++, 1, "%s level %zu ", entity_rank_to_string(e->rank), e->level);
    if (entity_is_player(e)) wprintw(win_right.win, "(%zu exp)", game.data.player.xp);
    mvwprintw(win_right.win, line++, 1, "Health: %d", e->stats.hp);
    mvwprintw(win_right.win, line++, 1, "Defense: %d", e->stats.defense);
    mvwprintw(win_right.win, line++, 1, "Attack: %d (%d%%)", e->stats.attack, e->stats.accuracy);
    mvwprintw(win_right.win, line++, 1, "Agility: %d", e->stats.agility);
    mvwprintw(win_right.win, line++, 1, "Effects: ");
    if (da_is_empty(&e->effects)) {
        waddstr(win_right.win, "none");
    } else {
        da_foreach(e->effects, Effect, effect) {
            EffectDefinition *effect_definition = get_effect(effect->type);
            mvwprintw(win_right.win, line++, 1, "- %s", effect_definition->name);
        }
    }
}

#define SECONDS_IN_DAY    (60*60*24)
#define SECONDS_IN_HOUR   (60*60)
#define SECONDS_IN_MINUTE (60)
void update_window_right(void)
{
    box(win_right.win, 0, 0);

    if (game.showing_general_info) {
        size_t line = 1;
        mvwprintw(win_right.win, line++, 1, "Seed: %016llx", (unsigned long long)game.data.rng_seed);

        mvwprintw(win_right.win, line++, 1, "Total time: ");

        float time = game.data.total_time;
        unsigned long time_days = (unsigned long)time / SECONDS_IN_DAY;
        time -= time_days * SECONDS_IN_DAY;
        unsigned long time_hours = (unsigned long)time / SECONDS_IN_HOUR;
        time -= time_hours * SECONDS_IN_HOUR;
        unsigned long time_minutes = (unsigned long)time / SECONDS_IN_MINUTE;
        time -= time_minutes * SECONDS_IN_MINUTE;
        unsigned long time_seconds = (unsigned long)time;
        wprintw(win_right.win, "%lud %luh %lum %lus", time_days, time_hours, time_minutes, time_seconds);

    } else if (game.show_entities_info.enabled) {
        show_entity_info(&CURRENT_ROOM->entities.items[game.show_entities_info.entities->items[game.show_entities_info.index]]);
    } else {
        show_entity_info(&game.data.player);
    }
}

void create_windows(void)
{
    get_terminal_size();
    win_main   = create_window(0, 0,
                               3*terminal_width/4, 3*terminal_height/4,
                               R_PAIR, update_window_main);
    win_bottom = create_window(0, 3*terminal_height/4, 
                               terminal_width, terminal_height/4+1,
                               R_PAIR, update_window_bottom);
    win_right  = create_window(3*terminal_width/4, 0,
                               terminal_width/4+1, 3*terminal_height/4,
                               R_PAIR, update_window_right);
}

void destroy_windows(void)
{
    delwin(win_main.win);
}

void ncurses_end(void)
{
    // TODO:
    // - restore original colors
    // - restore original terminal options (maybe not needed)
    curs_set(1);
    clear();
    refresh();
    endwin();
    log_this("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
}

void cleanup_on_terminating_signal(int sig)
{
    log_this("Program received signal %d: %s", sig, strsignal(sig));
    ncurses_end();
    exit(1);
}

void ncurses_init(void)
{
    log_this("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~");

    initscr();

    curs_set(0);
    raw();
    noecho();
    nonl();
    nodelay(stdscr, TRUE);
    set_escdelay(25);
    keypad(stdscr, TRUE);

    signal(SIGINT, cleanup_on_terminating_signal);
    signal(SIGTERM, cleanup_on_terminating_signal);
    signal(SIGSEGV, cleanup_on_terminating_signal);
}

#define COLOR_VALUE_TO_NCURSES(value) ((value*1000)/255)
#define RGB_TO_NCURSES(r, g, b) COLOR_VALUE_TO_NCURSES(r), COLOR_VALUE_TO_NCURSES(g), COLOR_VALUE_TO_NCURSES(b)

void colors_init(void)
{
    if (has_colors()) {
        start_color();
        if (can_change_color()) {
            init_color(R_COLOR_BACKGROUND, RGB_TO_NCURSES(18, 18, 18));
            init_color(R_COLOR_FOREGROUND, RGB_TO_NCURSES(150, 200, 150));
            init_color(R_COLOR_YELLOW, RGB_TO_NCURSES(178, 181, 0));
            init_color(R_COLOR_RED, RGB_TO_NCURSES(150, 20, 20));
            init_color(R_COLOR_BLUE, RGB_TO_NCURSES(20, 20, 150));

            init_pair(R_PAIR,     R_COLOR_FOREGROUND, R_COLOR_BACKGROUND);
            init_pair(R_PAIR_INV, R_COLOR_BACKGROUND, R_COLOR_FOREGROUND);
        } else {
            use_default_colors();
        }
    }
}

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
    fwrite(&stats->hp, sizeof(int), 1, f);
    fwrite(&stats->defense, sizeof(int), 1, f);
    fwrite(&stats->agility, sizeof(int), 1, f);
}
bool load_stats(FILE *f, Stats *stats)
{
    if (fread(&stats->attack, sizeof(int), 1, f) != 1) return false;
    if (fread(&stats->accuracy, sizeof(int), 1, f) != 1) return false;
    if (fread(&stats->hp, sizeof(int), 1, f) != 1) return false;
    if (fread(&stats->defense, sizeof(int), 1, f) != 1) return false;
    if (fread(&stats->agility, sizeof(int), 1, f) != 1) return false;
    return true;
}

void save_item(FILE *f, Item *item)
{
    fwrite(&item->type, sizeof(ItemType), 1, f);
    fwrite(item->name, sizeof(item->name), 1, f);
    fwrite(&item->durability, sizeof(int), 1, f);
    save_stats(f, &item->stats);
    save_da(item->effects, save_effect, f); 
}
bool load_item(FILE *f, Item *item)
{
    if (fread(&item->type, sizeof(ItemType), 1, f) != 1) goto fail;
    if (fread(item->name, sizeof(item->name), 1, f) != 1) goto fail;
    if (fread(&item->durability, sizeof(int), 1, f) != 1) goto fail;
    if (!load_stats(f, &item->stats)) goto fail;
    load_da(&item->effects, load_effect, f); 
    return true;
fail:
    return false;
}

void save_item_slot(FILE *f, ItemSlot *slot)
{
    fwrite(&slot->type, sizeof(ItemType), 1, f);
    save_item(f, &slot->item);
}
bool load_item_slot(FILE *f, ItemSlot *slot)
{
    if (fread(&slot->type, sizeof(ItemType), 1, f) != 1) return false;
    if (!load_item(f, &slot->item)) return false;
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

    save_stats(f, &e->stats);
    
    save_da(e->equipment, save_item_slot, f);
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
    if (!load_stats(f, &e->stats)) goto fail;
    load_da(&e->equipment, load_item_slot, f);
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

    fwrite(&room->tilemap.width, sizeof(size_t), 1, f);
    fwrite(&room->tilemap.height, sizeof(size_t), 1, f);
    for (size_t i = 0; i < room_tiles_count(room); i++)
        save_tile(f, &room->tilemap.tiles[i]);

    save_da(room->entities, save_entity, f);

    // NOTE: no need to save entities_map
}
bool load_room(FILE *f, Room *room)
{
    if (fread(&room->index, sizeof(size_t), 1, f) != 1) goto fail;

    if (fread(&room->tilemap.width, sizeof(size_t), 1, f) != 1) goto fail;
    if (fread(&room->tilemap.height, sizeof(size_t), 1, f) != 1) goto fail;
    size_t count = room_tiles_count(room);
    // TODO: I can even avoid to save/load tiles positions, i can recalculate it here
    room->tilemap.tiles = malloc(sizeof(Tile)*count);
    if (!room->tilemap.tiles) goto fail;
    for (size_t i = 0; i < count; i++)
        if (!load_tile(f, room->tilemap.tiles + i)) goto fail;

    load_da(&room->entities, load_entity, f);

    room->entities_map = malloc(sizeof(EntitiesIds)*count);
    if (!room->entities_map) goto fail;
    memset(room->entities_map, 0, sizeof(EntitiesIds)*count);

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

#define SAVE_FILEPATH "./save.bin"
void save_game_data(void)
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

    save_da(game.data.factions, save_faction, save_file);
    save_da(game.data.rooms, save_room, save_file);

    fclose(save_file);
    write_message("saved");
}

void init_game_data(void)
{
    uint64_t seed = time(NULL);
    game.data.rng_seed = seed;
    rng_init(&game.data.rooms_rng,    seed++);
    rng_init(&game.data.entities_rng, seed++);
    rng_init(&game.data.items_rng,    seed++);
    rng_init(&game.data.combat_rng,   seed++);

    Entity player = {
        .type = ENTITY_PLAYER,
        .rank = RANK_CIVILIAN,
        .level = 1,

        .stats = (Stats) {
            .hp       = 100,
            .defense  = 5,
            .accuracy = 75,
            .attack   = 10,
            .agility  = 75
        }
    };
    memcpy(player.name, "Adventurer", 10);

    Room *initial_room = generate_room(win_main.width, win_main.height);
    game.data.current_room_index = initial_room->index;

    V2i pos;
    if (!get_random_entity_slot_as_vector(CURRENT_ROOM, &pos))
        print_error_and_exit("It should never happen");
    player.pos = pos;

    game.data.player = player;
}

void delete_and_reinit_game_data(void)
{
    // TODO: free rooms (actually this is just a debug function, so who cares)
    init_game_data();
    save_game_data();
}

bool load_game_data(void)
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

    load_da(&game.data.factions, load_faction, save_file);
    load_da(&game.data.rooms, load_room, save_file);

    fclose(save_file);
    return true;

fail:
    fclose(save_file);
    return false;
}

#define SAVE_TIME_INTERVAL 15.f
void advance_save_timer(float dt)
{
    game.save_timer += dt;
    if (game.save_timer >= SAVE_TIME_INTERVAL) {
        game.save_timer = 0.f;
        save_game_data();
    }
}


int read_key()
{
    int c = getch();
    if (c != ESC) return c;

    int first = getch();
    if (first == ERR) return ESC;

    if (first == '[') { // ESC-[-X sequence
        int second = getch();
        if (second == ERR) return ESC;
        log_this("Read ESC-[-%c sequence", first);

        return ESC; // TODO: togli

        switch (second) { default: return ESC; }
    }

    switch (first) { // ALT-X sequence
        case '0'          : return ALT_0;
        case '1'          : return ALT_1;
        case '2'          : return ALT_2;
        case '3'          : return ALT_3;
        case '4'          : return ALT_4;
        case '5'          : return ALT_5;
        case '6'          : return ALT_6;
        case '7'          : return ALT_7;
        case '8'          : return ALT_8;
        case '9'          : return ALT_9;

        case 'c'          : return ALT_c;
        case 'C'          : return ALT_C;
        case 'i'          : return ALT_i;
        case 'k'          : return ALT_k;
        case 'K'          : return ALT_K;
        case 'j'          : return ALT_j;
        case 'J'          : return ALT_J;
        case 'h'          : return ALT_h;
        case 'H'          : return ALT_H;
        case 'l'          : return ALT_l;
        case 'L'          : return ALT_L;
        case 'm'          : return ALT_m;
        case 'n'          : return ALT_n;
        case 'p'          : return ALT_p;
        case KEY_BACKSPACE: return ALT_BACKSPACE;
        case ':'          : return ALT_COLON;

        case CTRL('C'): return CTRL_ALT_C;
        case CTRL('D'): return CTRL_ALT_D;
        case CTRL('E'): return CTRL_ALT_E;
        case CTRL('K'): return CTRL_ALT_K;
        case CTRL('J'): return CTRL_ALT_J;
        case CTRL('H'): return CTRL_ALT_H;
        case CTRL('L'): return CTRL_ALT_L;

        default: return ESC;
    }
}

static inline void update_window(Window *window)
{
    werase(window->win);
    window->update();
    wnoutrefresh(window->win);
}

static inline void update_windows(void)
{
    for (size_t i = 0; i < windows_count; i++)
        update_window(windows[i]);
}

void update_cursor(void)
{
    V2i pos = PLAYER->pos;
    int cy = pos.y;
    int cx = pos.x;
    WINDOW *win = win_main.win;

    wmove(win, cy, cx);
    wnoutrefresh(win);
}

void handle_sigwinch(int signo)
{
    (void)signo;
    get_terminal_size();
    destroy_windows();
    create_windows();
    
    V2i pos = PLAYER->pos;
    if (pos.y < 0) pos.y = 0;
    else if ((size_t)pos.y >= win_main.height) pos.y = win_main.height - 1;
    if (pos.x < 0) pos.x = 0;
    else if ((size_t)pos.x >= win_main.width)  pos.x = win_main.width - 1;
}

void game_init()
{
    if (!load_game_data()) {
        write_message("Creating new save file...");
        init_game_data();
        save_game_data();
    } write_message("Save loaded!");
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

void dispatch_kill(Entity *killer, Entity *victim)
{
    bool player_is_killer = entity_is_player(killer);
    bool player_is_victim = entity_is_player(victim);

    if (player_is_killer && player_is_victim) player_killed_themselves();
    else if (player_is_killer) player_killed_entity(victim);
    else if (player_is_victim) entity_killed_player(killer);
    else if (killer == victim) entity_killed_itself(killer);
    else entity_killed_entity(killer, victim);
}

typedef enum
{
    DEATH_BY_ENTITY_ATTACK,
    DEATH_BY_EFFECT,
    __death_causes_count
} DeathCause;

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

        PLAYER->pos = (V2i){1, 1}; // just to see something
        PLAYER->stats.hp = 100*PLAYER->level; // TODO okaye, I got it:
                                                            // levels give base stats and items add them up
                                                            // so, now I just have to calculate what is the base hp
                                                            // for the level;
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
{
    entity_die(entity, DEATH_BY_ENTITY_ATTACK, attacker);
}
static inline void entity_die_from_effect(Entity *entity, Effect *effect)
{
    entity_die(entity, DEATH_BY_EFFECT, effect);
}
static inline void player_die_from_entity_attack(Entity *attacker)
{
    entity_die_from_entity_attack(PLAYER, attacker);
}
static inline void player_die_from_effect(Effect *effect) { entity_die_from_effect(PLAYER, effect); }

typedef enum
{
    ESTATUS_OK,
    ESTATUS_DEAD
} EntityStatus;

EntityStatus apply_entity_effects(Entity *entity)
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
static inline EntityStatus apply_player_effects(void) { return apply_entity_effects(PLAYER); }

EntityStatus entity_attack_entity(Entity *attacker, Entity *defender)
{
    write_message("%s is attacking %s", attacker->name, defender->name);
    if (attacker->stats.accuracy <= 0) {
        write_message("%s missed the attack, didn't even try", attacker->name);
        return ESTATUS_OK;
    }
    int multiplier = attacker->stats.accuracy / 100;
    uint64_t accuracy = attacker->stats.accuracy % 100;
    if (accuracy > 0 && (combat_rng_generate() % 100) >= accuracy) multiplier += 1;
    if (multiplier <= 0) {
        write_message("%s missed the attack, unlucky", attacker->name);
        return ESTATUS_OK;
    }
    int damage = attacker->stats.attack*multiplier;
    int total_damage = damage - defender->stats.defense;
    if (total_damage <= 0) {
        write_message("%s defended %d damage, unbothered", defender->name, damage);
        return ESTATUS_OK;
    }
    write_message("%s inflicted %u damage, ouch", attacker->name, total_damage);
    defender->stats.hp -= total_damage;
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
         && (size_t)e->pos.x + d.x < CURRENT_ROOM->tilemap.width
         && e->pos.y + d.y >= 0
         && (size_t)e->pos.y + d.y < CURRENT_ROOM->tilemap.height
         && tile_at(CURRENT_ROOM, e->pos.x + d.x, e->pos.y + d.y)->type != TILE_WALL);
}

Tile *get_door_that_leads_to(int room_index)
{
    for (size_t y = 0; y < CURRENT_ROOM->tilemap.height; y++) {
        for (size_t x = 0; x < CURRENT_ROOM->tilemap.width; x++) {
            Tile *tile = tile_at(CURRENT_ROOM, x, y);
            if (tile->type == TILE_DOOR && tile->leads_to == room_index) return tile;
        }
    }
    return NULL;
}

static inline void move_entity(Entity *e);
void set_entity_position_and_direction_entering_room(Entity *entity, Room *room, Tile *door)
{
    Direction direction;
         if (door->pos.x == 0)                              direction = DIRECTION_RIGHT;
    else if (door->pos.y == 0)                              direction = DIRECTION_DOWN;
    else if ((size_t)door->pos.y == room->tilemap.height-1) direction = DIRECTION_UP;
    else                                                    direction = DIRECTION_LEFT;

    entity->pos = door->pos;
    entity->direction = direction;
    if (!entity_is_player(entity)) move_entity(entity);
}

static inline void set_player_position_and_direction_entering_room(Room *room, Tile *door)
{
    set_entity_position_and_direction_entering_room(PLAYER, room, door);
}

void player_interact_with_door(Tile *door)
{
    if (door->open) {
        Tile *arrival_door;
        if (door->leads_to == DOOR_LEADS_TO_NEW_ROOM) {
            Room *new_room = generate_room(win_main.width, win_main.height);
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
        set_player_position_and_direction_entering_room(CURRENT_ROOM, arrival_door);
    } else if (door->heavy) {

    } else {

    }
}

void advance_movement_timers(float dt)
{
    da_foreach (CURRENT_ROOM->entities, Entity, e) {
        e->movement_timer -= dt;
        if (e->movement_timer <= 0) {
            move_entity(e);
            e->movement_timer = entities_rng_generate() % 10 + 2;
            e->direction = entities_rng_generate() % __directions_count;
        }
    }
}

void advance_all_timers(float dt)
{
    //advance_save_timer(dt); // TODO rimetti
    advance_switch_timer(dt);
    advance_movement_timers(dt);
}

void entity_interact_with_door(Entity *entity, Tile *door)
{
    if (!door->open || door->heavy || door->leads_to != DOOR_LEADS_TO_NEW_ROOM) return;

    Tile *arrival_door;
    int leaving_room_index = CURRENT_ROOM->index;
    arrival_door = get_door_that_leads_to(leaving_room_index);
    assert(arrival_door != NULL);
    // TODO: remove entity from the entities of this room and add it to the other room
    set_entity_position_and_direction_entering_room(entity, CURRENT_ROOM, arrival_door);
}

void entity_interact_with_entities(Entity *entity, EntitiesIds *entities)
{
    if (apply_entity_effects(entity) == ESTATUS_DEAD) return;

    da_foreach (*entities, uint64_t, id) {
        Entity *other = get_entity_by_id(CURRENT_ROOM, *id);
        if (!other || entity_is_dead(other)) continue;

        if (apply_entity_effects(other) == ESTATUS_DEAD) continue;

        if (entity->stats.agility >= other->stats.agility) {
            if (entity_attack_entity(entity, other) == ESTATUS_DEAD) continue;
            if (entity_attack_entity(other, entity) == ESTATUS_DEAD) return;
        } else {
            if (entity_attack_entity(other, entity) == ESTATUS_DEAD) return;
            if (entity_attack_entity(entity, entity) == ESTATUS_DEAD) continue;
        }
    }
}

static inline void move_entity(Entity *e)
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
    if (apply_player_effects() == ESTATUS_DEAD) return;

    da_foreach (*entities, uint64_t, id) {
        Entity *entity = get_entity_by_id(CURRENT_ROOM, *id);
        if (!entity || entity_is_dead(entity)) continue;

        if (apply_entity_effects(entity) == ESTATUS_DEAD) continue;

        if (PLAYER->stats.agility >= entity->stats.agility) {
            if (player_attack_entity(entity) == ESTATUS_DEAD) continue;
            if (entity_attack_player(entity) == ESTATUS_DEAD) return;
        } else {
            if (entity_attack_player(entity) == ESTATUS_DEAD) return;
            if (player_attack_entity(entity) == ESTATUS_DEAD) continue;
        }
    }
}

static_assert(__tile_types_count == 3, "Move player onto all tiles");
static inline void move_player(Direction direction)
{
    game.data.player.direction = direction;
    if (!entity_can_move(&game.data.player)) return;
    V2i *curr_pos = &PLAYER->pos;
    V2i dir = direction_vector(direction);
    V2i new_pos = {curr_pos->x + dir.x, curr_pos->y + dir.y};
    Tile *tile = tile_at(CURRENT_ROOM, new_pos.x, new_pos.y);
    if (tile->type == TILE_WALL) return;

    EntitiesIds *entities = entities_at(CURRENT_ROOM, new_pos.x, new_pos.y);

    if (da_is_empty(entities)) {
        if (tile->type == TILE_DOOR) player_interact_with_door(tile);
        else if (tile->type == TILE_FLOOR) *curr_pos = new_pos;
    } else player_interact_with_entities(entities);
}

// TODO: non funziona :)
void check_player_look_direction(void)
{
    V2i pos = game.data.player.pos;
    V2i dir = direction_vector(game.data.player.direction);
    EntitiesIds *entities = entities_at(CURRENT_ROOM, pos.x + dir.x, pos.y + dir.y);
    // TODO: show options, but for now:
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

_Noreturn void quit(void)
{
    ncurses_end();
    exit(0);
}

void process_pressed_key(void)
{
    int key = read_key();
    if (key == ERR) return;

    switch (key)
    {
        case 'w':
        case KEY_UP:
            move_player(DIRECTION_UP);
            break;

        case 's':
        case KEY_DOWN: move_player(DIRECTION_DOWN); break;

        case 'a':
        case KEY_LEFT: move_player(DIRECTION_LEFT); break;

        case 'd':
        case KEY_RIGHT: move_player(DIRECTION_RIGHT); break;

        case CTRL('E'):
            spawn_random_entity(CURRENT_ROOM);
            break;

        case CTRL('I'):
            game.showing_general_info = !game.showing_general_info;
            break;

        case CTRL_ALT_E:
            // TODO: I have to free all the entities
            write_message("TODO: clear all entities");
            break;

        case 'l': game.looking = !game.looking; break;

        case CTRL('S'): save_game_data(); break;

        case CTRL_ALT_D: delete_and_reinit_game_data(); break;

        case CTRL('Q'):
            save_game_data();
            quit();

        case ESC:
            if (game.show_entities_info.enabled) {
                game.show_entities_info.enabled = false;
                game.show_entities_info.index = 0;
            }
            break;

        //case ALT_0:
        //case ALT_1:
        //case ALT_2:
        //case ALT_3:
        //case ALT_4:
        //case ALT_5:
        //case ALT_6:
        //case ALT_7:
        //case ALT_8:
        //case ALT_9:
        //case KEY_UP:
        //case ALT_k:
        //case KEY_DOWN:
        //case ALT_j:
        //case KEY_LEFT:
        //case ALT_h:
        //case KEY_RIGHT:
        //case ALT_l:
        //case ALT_K:
        //case ALT_J:
        //case ALT_H:
        //case ALT_L:
        //case ALT_m:
        //case ALT_p:
        //case ALT_n:
        //case ALT_c:
        //case ALT_C:
        //case CTRL_ALT_C:
        //case KEY_PPAGE:
        //case CTRL_ALT_K:
        //case KEY_NPAGE:
        //case CTRL_ALT_J:
        //case ALT_COLON:
        //case ALT_BACKSPACE:
        //case TAB:
        //case KEY_BTAB:

        default:
            if (isprint(key)) log_this("Unprocessed key '%c'", key);
            else log_this("Unprocessed key %d", key);
    }
}

void clear_and_populate_entities_map(void)
{
    for (size_t i = 0; i < room_tiles_count(CURRENT_ROOM); i++)
        da_clear(&CURRENT_ROOM->entities_map[i]);

    size_t i = 0;
    while (i < CURRENT_ROOM->entities.count) {
        Entity *e = &CURRENT_ROOM->entities.items[i];
        if (entity_is_dead(e)) {
            // TODO: free entity fields
            da_remove(&CURRENT_ROOM->entities, i);
        } else {
            size_t index = index_in_room(CURRENT_ROOM, e->pos.x, e->pos.y);
            da_push(&CURRENT_ROOM->entities_map[index], e->id);
            i++;
        }
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    signal(SIGWINCH, handle_sigwinch);
    ncurses_init();
    colors_init();
    create_windows();
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

        clear_and_populate_entities_map();

        napms(16); // TODO: do it with the calculated dt
    }

    return 0;
}
