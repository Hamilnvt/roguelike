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
 * Todos
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
 * - invetory to store/equip items
 * - each step increments a "timer" and after some time some actions are performed (a monster moves, a new monster spawns, something good/bad happens)
 * - clear entities_map at the end of the iteration and repopulate it at the beginning?
 * - monsters in a new room spawn accordingly to player's level
 * - cycle char shown if more than one entity/item on a tile
 * - items + items_map like for entities? They can stay on the ground but they're not tiles nor entities
 * - think about the level
 *   > what does it give to the entity? Does it boosts its stats in some way?
 *   > It can be the lower value for the spawned entities
 *   > but for the player?
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

#define DEBUG true

static inline bool streq(const char *s1, const char *s2) { return strcmp(s1, s2) == 0; }
static inline bool strneq(const char *s1, const char *s2, size_t n) { return strncmp(s1, s2, n) == 0; }

#define BOOL_AS_CSTR(value) ((value) ? "true" : "false")

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

#define index(x, y, width) ((y)*(width) + (x))
#define at(A, x, y, width) ((A)[index(x, y, width)])
#define index_in_room(room, x, y) ((y)*(room)->tilemap.width + (x))
#define pos_in_room(room, i) ((V2i){(i)%(room)->tilemap.width, (size_t)((i)/(room)->tilemap.height)})
#define tile_at(room, x, y) (at((room)->tilemap.tiles, (x), (y), (room)->tilemap.width))
#define room_tiles_count(room) ((room)->tilemap.width*(room)->tilemap.height)
#define entities_at(room, x, y) (&(at((room)->entities_map, (x), (y), (room)->tilemap.width)))

typedef struct
{
    uint64_t state[4];
} RNG;

static inline uint64_t rotl(const uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

static uint64_t splitmix64(uint64_t *x)
{
    uint64_t z = (*x += 0x9e3779b97f4a7c15);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
    z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
    return z ^ (z >> 31);
}

void rng_initialize(RNG *rng, uint64_t seed)
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
    __tiles_count
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

typedef struct Room Room;
typedef void (* EffectAction) (Room *room);

typedef struct
{
    char name[32];
    EffectAction action;
} Effect;

typedef struct
{
    Effect *items; 
    size_t count;
    size_t capacity;
} Effects;

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

    int attack;
    int chance;
    int hp;
    int defense;
    int agility;
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

#define ENTITY_NAME_MAX_LEN 31
typedef struct Entity
{
    char name[ENTITY_NAME_MAX_LEN + 1];
    V2i pos;
    Direction direction;
    bool dead;

    EntityRank rank;
    size_t level;

    Equipment equipment;

    int hp;
    int defense;
    int chance;
    int attack;
    int agility;
    Effects effects; 

    float movement_timer;
} Entity;

typedef struct
{
    Entity *items;
    size_t count;
    size_t capacity;
} Entities;

typedef struct
{
    size_t *items;
    size_t count;
    size_t capacity;
} EntitiesIndices;

char get_entity_char(Entity e)
{
    switch (e.rank)
    {
    case RANK_CIVILIAN:  return 'c';
    case RANK_WARRIOR:   return 'w';
    case RANK_NOBLE:     return 'N';
    case RANK_KING:      return 'K';
    case RANK_EMPEROR:   return 'E';
    case RANK_WORLDLORD: return 'W';

    case __entity_ranks_count:
    default: print_error_and_exit("Unreachable entity rank %u in get_entity_char", e.rank);
    }
}

char get_tile_char(Tile tile)
{
    switch (tile.type)
    {
    case TILE_FLOOR:  return ' ';
    case TILE_WALL:   return '#';
    case TILE_DOOR:   return tile.open ? 'O' : '0';
    
    case __tiles_count:
    default: print_error_and_exit("Unreachable tile type %u in get_tile_char", tile.type);
    }
}

char *tile_type_to_string(TileType type)
{
    switch (type)
    {
    case TILE_FLOOR:  return "floor";
    case TILE_WALL:   return "wall";
    case TILE_DOOR:   return "door";

    case __tiles_count:
    default: print_error_and_exit("Unreachable tile type %u in tile_type_to_string", type);
    }
}

typedef struct Room
{
    size_t index;
    TileMap tilemap;
    Entities entities;
    EntitiesIndices *entities_map;
} Room;

typedef struct
{
    Room *items;
    size_t count;
    size_t capacity;
} Rooms;

typedef struct
{
    Item *items; 
    size_t count;
    size_t capacity;
} Inventory;

typedef struct
{
    Entity entity;
    size_t xp;
    Inventory inventory;
} Player;

typedef struct
{
    Player player;

    // POD
    size_t current_room_index;
    float total_time;
    uint64_t rng_seed;
    RNG rooms_rng;
    RNG entities_rng;
    RNG items_rng;

    // Dynamic
    Rooms rooms;
} Data;

typedef struct
{
    Data data;
    float save_timer;
    char message[256];

    float switch_timer;

    bool showing_general_info;
    struct {
        bool enabled;
        size_t index;
        EntitiesIndices *entities;
    } show_entities_info;
} Game;
static Game game = {0};

#define CURRENT_ROOM (&game.data.rooms.items[game.data.current_room_index])
#define PLAYER (&game.data.player)
#define PLAYER_ENTITY (&game.data.player.entity)
static inline bool entity_is_player(Entity *e) { return e == &game.data.player.entity; }

static inline Tile *get_tile_under_player(void)
{
    V2i pos = PLAYER_ENTITY->pos;
    return &tile_at(CURRENT_ROOM, pos.x, pos.y);
}

static inline EntitiesIndices *get_entities_under_player(void)
{
    V2i pos = PLAYER_ENTITY->pos;
    return entities_at(CURRENT_ROOM, pos.x, pos.y);
}

static inline uint64_t rooms_rng_generate   (void) { return rng_generate(&game.data.rooms_rng); }
static inline uint64_t entities_rng_generate(void) { return rng_generate(&game.data.entities_rng); }
static inline uint64_t items_rng_generate   (void) { return rng_generate(&game.data.items_rng); }

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

static inline Entity make_entity_random_at(size_t x, size_t y)
{
    Entity e = {
        .pos = (V2i){x, y},
        .direction = entities_rng_generate() % __directions_count,
        .rank      = entities_rng_generate() % __entity_ranks_count,
        .level     = entities_rng_generate() % (10*(e.rank+1)) + 1,
        .hp        = entities_rng_generate() % (100*(e.rank+1)),
        .defense   = entities_rng_generate() % (10*(e.rank+1)),
        .chance    = entities_rng_generate() % (100*(e.rank+1)),
        .attack    = entities_rng_generate() % (100*(e.rank+1)),
        .agility   = entities_rng_generate() % (10*(e.rank+1)),

        .movement_timer = entities_rng_generate() % 10 + 2
    };

    memcpy(e.name, "Entity", 6); // TODO: random name

    return e;
}

static inline Entity make_entity_random(size_t x_low, size_t x_high, size_t y_low, size_t y_high)
{
    size_t x = (entities_rng_generate() % (x_high - x_low)) + x_low;
    size_t y = (entities_rng_generate() % (y_high - y_low)) + y_low;
    return make_entity_random_at(x, y);
}

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

typedef bool (*TilePredicate)(Tile*, void *_args);

Tile *get_random_tile_predicate(Room *room, TilePredicate predicate, void *args)
{
    size_t tiles_count = room_tiles_count(room);
    size_t *tiles_indices = malloc(sizeof(size_t)*tiles_count);
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
        Tile tile = tile_at(room, x, y);
        if (tile.type != TILE_WALL) {
            *pos = (V2i){x, y};
            return true;
        } else tries--;
    }
    for (size_t y = 1; y < room->tilemap.height-1; y++) {
        for (size_t x = 1; x < room->tilemap.width-1; x++) {
            Tile tile = tile_at(room, x, y);
            if (tile.type != TILE_WALL) {
                *pos = (V2i){x, y};
                return true;
            }
        }
    }
    return false;
}

void spawn_random_entity(Room *room)
{
    V2i pos;
    if (!get_random_entity_slot_as_vector(room, &pos)) return;
    Entity e = make_entity_random_at(pos.x, pos.y);
    da_push(&room->entities, e);
    EntitiesIndices *entities = &room->entities_map[index_in_room(room, pos.x, pos.y)];
    da_push(entities, room->entities.count-1);
}

Tile *create_tiles(size_t width, size_t height)
{
    Tile *tiles = malloc(sizeof(Tile)*width*height);
    for (size_t y = 0; y < height; y++) {
        for (size_t x = 0; x < width; x++) {
            size_t index = index(x, y, width);
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
        .entities_map = malloc(sizeof(EntitiesIndices)*width*height)
    };

    // TODO: si puo' migliorare questo loop
    for (size_t y = 0; y < height; y++) {
        for (size_t x = 0; x < width; x++) {
            if (x == 0 || y == 0 || x == width-1 || y == height-1) {
                set_tile_wall(&room.tilemap.tiles[index(x, y, width)], !WALL_IS_DESTRUCTIBLE);
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

void write_message(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    memset(game.message, 0, sizeof(game.message));
    vsnprintf(game.message, sizeof(game.message), fmt, ap);
    va_end(ap);
    log_this("MESSAGE: %s", game.message);
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
    size_t start_y;
    size_t start_x;
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
    win.start_y = y;
    win.start_x = x;
    if (has_colors() && can_change_color()) wbkgd(win.win, COLOR_PAIR(color_pair));
    return win;
}

void update_window_main(void)
{
    for (size_t y = 0; y < CURRENT_ROOM->tilemap.height; y++) {
        for (size_t x = 0; x < CURRENT_ROOM->tilemap.width; x++) {
            Tile tile = tile_at(CURRENT_ROOM, x, y);
            EntitiesIndices *entities = entities_at(CURRENT_ROOM, x, y);
            char c;
            if (da_is_empty(entities)) c = get_tile_char(tile);
            else {
                if (tile.type == TILE_FLOOR) {
                    Entity e = CURRENT_ROOM->entities.items[entities->items[(size_t)game.switch_timer%entities->count]];
                    c = get_entity_char(e);
                } else {
                    size_t index = (size_t)game.switch_timer % (entities->count+1);
                    if (index == entities->count) c = get_tile_char(tile);
                    else {
                        Entity e = CURRENT_ROOM->entities.items[entities->items[index]];
                        c = get_entity_char(e);
                    }
                }
            }
            mvwaddch(win_main.win, y, x, c);
        }
    }

    Entity *pe = PLAYER_ENTITY;
    mvwaddch(win_main.win, pe->pos.y, pe->pos.x, '@');
}

void update_window_bottom(void)
{
    Tile *tile = get_tile_under_player();
    EntitiesIndices *entities = get_entities_under_player();

    box(win_bottom.win, 0, 0);

    size_t line = 1;
    wmove(win_bottom.win, line++, 1);
    switch (tile->type)
    {
    case TILE_FLOOR: wprintw(win_bottom.win, "Same old boring floor"); break;
    case TILE_WALL:  wprintw(win_bottom.win, "A wall... wait, how'd I get up here?"); break;
    case TILE_DOOR:
        Tile *door = tile;
        if (door->open) {
            wprintw(win_bottom.win, "An open door that leads to ");
            if (door->leads_to >= 0) wprintw(win_bottom.win, "room %d", door->leads_to);
            else wprintw(win_bottom.win, "a new room");
        } else {
            wprintw(win_bottom.win, "A closed door. ");
            if (door->heavy) wprintw(win_bottom.win,
                    "It's massive. It requires an extraordinary act of strength to open it.");
            else wprintw(win_bottom.win, "It seems that it can be opened, I wonder how, though.");
        }
        break;

    case __tiles_count:
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
    mvwprintw(win_right.win, line++, 1, "Health: %d", e->hp);
    mvwprintw(win_right.win, line++, 1, "Defense: %d", e->defense);
    mvwprintw(win_right.win, line++, 1, "Attack: %d (%d%%)", e->attack,
            e->chance);
    mvwprintw(win_right.win, line++, 1, "Agility: %d", e->agility);
    mvwprintw(win_right.win, line++, 1, "Effects: ");
    if (da_is_empty(&e->effects)) {
        waddstr(win_right.win, "none");
    } else {
        da_foreach(e->effects, Effect, effect) {
            mvwprintw(win_right.win, line++, 1, "- ");
            waddstr(win_right.win, effect->name);
        }
    }
}

void update_window_right(void)
{
    box(win_right.win, 0, 0);

    if (game.showing_general_info) {
        size_t line = 1;
        mvwprintw(win_right.win, line++, 1, "Seed: %016llx", (unsigned long long)game.data.rng_seed);
        mvwprintw(win_right.win, line++, 1, "Total time: %.3f", game.data.total_time);
        if (strlen(game.message)) mvwprintw(win_right.win, line++, 1, "Message: %s", game.message);
    } else if (game.show_entities_info.enabled) {
        show_entity_info(&CURRENT_ROOM->entities.items[game.show_entities_info.entities->items[game.show_entities_info.index]]);
    } else {
        show_entity_info(&game.data.player.entity);
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

void initialize_colors(void)
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

void save_entity(FILE *f, Entity *e)
{
    // Name
    fwrite(e->name, sizeof(char), sizeof(e->name), f);
    
    // POD
    fwrite(&e->pos,    sizeof(V2i),     1, f);
    fwrite(&e->rank,   sizeof(EntityRank), 1, f);
    fwrite(&e->level,  sizeof(size_t),     1, f);
    fwrite(&e->chance, sizeof(int),        1, f);
    fwrite(&e->attack, sizeof(int),        1, f);
    
    // Effects TODO
    // 3. IMPORTANT: Do NOT write the 'effects' struct directly.
    // It contains pointers. For now, we skip it.
}

#define SAVE_FILEPATH "./save.bin"
bool save_game_data(void)
{
    FILE *save_file = fopen(SAVE_FILEPATH, "wb");    
    if (!save_file) return false;

    // Player
    save_entity(save_file, &game.data.player.entity);
    fwrite(&game.data.player.xp, sizeof(size_t), 1, save_file);
    //Inventory inventory;

    // POD
    fwrite(&game.data.current_room_index, sizeof(size_t),   1, save_file);
    fwrite(&game.data.total_time,         sizeof(float),    1, save_file);
    fwrite(&game.data.rng_seed,           sizeof(uint64_t), 1, save_file);
    fwrite(&game.data.rooms_rng,          sizeof(RNG),      1, save_file);
    fwrite(&game.data.entities_rng,       sizeof(RNG),      1, save_file);
    fwrite(&game.data.items_rng,          sizeof(RNG),      1, save_file);

    // Rooms TODO
    //fwrite(&game.data.entities.count, sizeof(size_t), 1, save_file);
    //da_foreach (game.data.entities, Entity*, e)
    //    save_entity(save_file, *e);

    fclose(save_file);
    write_message("saved");
    return true;
}

void init_game_data(void)
{
    uint64_t seed = time(NULL);
    game.data.rng_seed = seed;
    rng_initialize(&game.data.rooms_rng,    seed++);
    rng_initialize(&game.data.entities_rng, seed++);
    rng_initialize(&game.data.items_rng,    seed++);

    game.data.player = (Player){0};

    Entity e = {0};
    memcpy(e.name, "Adventurer", 10);
    e.rank = RANK_CIVILIAN;
    e.level = 1;
    e.hp = 100;
    e.defense = 5;
    e.chance = 75;
    e.attack = 10;
    e.agility = 75;
    e.effects = (Effects){0}; 
    game.data.player.entity = e;

    Room *initial_room = generate_room(win_main.width, win_main.height);
    game.data.current_room_index = initial_room->index;

    V2i pos;
    if (!get_random_entity_slot_as_vector(CURRENT_ROOM, &pos))
        print_error_and_exit("It should never happen");
    game.data.player.entity.pos = pos;
}

bool load_entity(FILE  *f, Entity *e)
{
    // Name
    if (fread(e->name, sizeof(char), sizeof(e->name), f) != sizeof(e->name)) return false;

    // POD
    if (fread(&e->pos,    sizeof(V2i),     1, f) != 1) return false;
    log_this("Loaded entity position (%zu, %zu)", e->pos.x, e->pos.y);
    if (fread(&e->rank,   sizeof(EntityRank), 1, f) != 1) return false;
    if (fread(&e->level,  sizeof(size_t),     1, f) != 1) return false;
    if (fread(&e->chance, sizeof(int),        1, f) != 1) return false;
    if (fread(&e->attack, sizeof(int),        1, f) != 1) return false;

    // Effects TODO
    // Reset pointers to NULL to prevent crashes
    e->effects.items = NULL;
    e->effects.count = 0;
    e->effects.capacity = 0;

    return true;
}

bool load_game_data(void)
{
    FILE *save_file = fopen(SAVE_FILEPATH, "rb");    
    if (!save_file) return false;

    // Player
    if (!load_entity(save_file, &game.data.player.entity)) goto fail;
    if (fread(&game.data.player.xp, sizeof(size_t), 1, save_file) != 1) goto fail;
    // TODO inventory

    // POD
    if (fread(&game.data.current_room_index, sizeof(size_t),   1, save_file) != 1) goto fail;
    if (fread(&game.data.total_time,         sizeof(float),    1, save_file) != 1) goto fail;
    if (fread(&game.data.rng_seed,           sizeof(uint64_t), 1, save_file) != 1) goto fail;
    if (fread(&game.data.rooms_rng,          sizeof(RNG),      1, save_file) != 1) goto fail;
    if (fread(&game.data.entities_rng,       sizeof(RNG),      1, save_file) != 1) goto fail;
    if (fread(&game.data.items_rng,          sizeof(RNG),      1, save_file) != 1) goto fail;

    // Rooms TODO
    //da_clear(&game.data.entities);
    //size_t count = 0;
    //if (fread(&count, sizeof(size_t), 1, save_file) != 1) goto fail;
    //if (count > 0) {
    //    game.data.entities.count = count;
    //    game.data.entities.capacity = count;
    //    game.data.entities.items = malloc(count * sizeof(Entity));
    //    da_foreach (game.data.entities, Entity*, e)
    //        if (!load_entity(save_file, *e)) goto fail;
    //}

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
        if (!save_game_data()) print_error_and_exit("Could not save game");
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
    V2i pos = PLAYER_ENTITY->pos;
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
    
    V2i pos = PLAYER_ENTITY->pos;
    if (pos.y < 0) pos.y = 0;
    else if ((size_t)pos.y >= win_main.height) pos.y = win_main.height - 1;
    if (pos.x < 0) pos.x = 0;
    else if ((size_t)pos.x >= win_main.width)  pos.x = win_main.width - 1;
}

void game_init()
{
    if (!load_game_data()) {
        init_game_data();
        if (!save_game_data()) print_error_and_exit("Could not save game data to %s", SAVE_FILEPATH);
    }
}

bool entity_can_move(Entity *e)
{
    V2i d = direction_vector(e->direction);
    return (e->pos.x + d.x >= 0
         && (size_t)e->pos.x + d.x <= CURRENT_ROOM->tilemap.width
         && e->pos.y + d.y >= 0
         && (size_t)e->pos.y + d.y <= CURRENT_ROOM->tilemap.height
         && tile_at(CURRENT_ROOM, e->pos.x + d.x, e->pos.y + d.y).type != TILE_WALL);
}

static inline void move_entity(Entity *e)
{
    if (entity_can_move(e)) {
        V2i dir = direction_vector(e->direction);
        e->pos.x += dir.x;
        e->pos.y += dir.y;
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

Tile *get_door_that_leads_to(int room_index)
{
    for (size_t y = 0; y < CURRENT_ROOM->tilemap.height; y++) {
        for (size_t x = 0; x < CURRENT_ROOM->tilemap.width; x++) {
            Tile *tile = &tile_at(CURRENT_ROOM, x, y);
            if (tile->type == TILE_DOOR && tile->leads_to == room_index) return tile;
        }
    }
    return NULL;
}

void set_player_position_and_direction_entering_room(Room *room, Tile *door)
{
    Direction direction;
         if (door->pos.x == 0)                              direction = DIRECTION_RIGHT;
    else if (door->pos.y == 0)                              direction = DIRECTION_DOWN;
    else if ((size_t)door->pos.y == room->tilemap.height-1) direction = DIRECTION_UP;
    else                                                    direction = DIRECTION_LEFT;

    PLAYER_ENTITY->pos = door->pos;
    PLAYER_ENTITY->direction = direction;
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

void player_kill_entity(Entity *e)
{
    PLAYER_ENTITY->level += 1;
    PLAYER->xp += e->level;
    e->dead = true;
}

void player_interact_with_entities(EntitiesIndices *entities)
{
    da_foreach (*entities, size_t, i) {
        Entity *e = &CURRENT_ROOM->entities.items[*i];
        player_kill_entity(e);
    }
}

static_assert(__tiles_count == 3, "Move player onto all tiles");
static inline void move_player(Direction direction)
{
    game.data.player.entity.direction = direction;
    if (!entity_can_move(&game.data.player.entity)) return;
    V2i *curr_pos = &PLAYER_ENTITY->pos;
    V2i dir = direction_vector(direction);
    V2i new_pos = {curr_pos->x + dir.x, curr_pos->y + dir.y};
    Tile *tile = &tile_at(CURRENT_ROOM, new_pos.x, new_pos.y);
    if (tile->type == TILE_WALL) return;

    EntitiesIndices *entities = entities_at(CURRENT_ROOM, new_pos.x, new_pos.y);

    if (da_is_empty(entities)) {
        if (tile->type == TILE_DOOR) player_interact_with_door(tile);
        else if (tile->type == TILE_FLOOR) *curr_pos = new_pos;
    } else player_interact_with_entities(entities);
}

// TODO: non funziona :)
void check_player_look_direction(void)
{
    V2i pos = game.data.player.entity.pos;
    V2i dir = direction_vector(game.data.player.entity.direction);
    EntitiesIndices *entities = entities_at(CURRENT_ROOM, pos.x + dir.x, pos.y + dir.y);
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

        case KEY_BACKSPACE:
            EntitiesIndices *entities = get_entities_under_player();
            if (!da_is_empty(entities))
                da_remove(&CURRENT_ROOM->entities, entities->items[0]);
            break;

        case CTRL('S'):
            if (!save_game_data()) print_error_and_exit("Could not save game");
            break;

        case CTRL('Q'):
            if (save_game_data()) quit();
            else print_error_and_exit("Could not save game");

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
    for (size_t i = 0; i < room_tiles_count(CURRENT_ROOM); i++) {
        da_clear(&CURRENT_ROOM->entities_map[i]);
    }
    size_t i = 0;
    while (i < CURRENT_ROOM->entities.count) {
        Entity *e = &CURRENT_ROOM->entities.items[i];
        if (e->dead) {
            // TODO: free entity fields
            da_remove(&CURRENT_ROOM->entities, i);
        } else {
            size_t index = index_in_room(CURRENT_ROOM, e->pos.x, e->pos.y);
            da_push(&CURRENT_ROOM->entities_map[index], i);
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
    initialize_colors();
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
