#ifndef GAME_H
#define GAME_H

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
#include "strings.h"

/// General Macros
#define DEBUG true
#define UNUSED(x) (void)(x)

/// Forward declaractions
typedef struct Room Room;
typedef struct Entity Entity;

/// Structs definitions

typedef struct { uint64_t state[4]; } RNG;

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
    int health;
    int defense;
    int agility;
} Stats;

typedef enum
{
    ITEM_EQUIPMENT,
    ITEM_COLLECTIBLE,
    __item_kinds_count
} ItemKind;

typedef enum
{
    EQUIPMENT_HELMET,
    EQUIPMENT_HAT,
    EQUIPMENT_GOGGLES,
    EQUIPMENT_SCARF,
    EQUIPMENT_CHESTPLATE,
    EQUIPMENT_CHAUSSES,
    EQUIPMENT_SHOE,
    EQUIPMENT_GLOVE,
    EQUIPMENT_SWORD,
    EQUIPMENT_SHIELD,
    EQUIPMENT_SCROLL,
    EQUIPMENT_STAFF,
    __equipment_types_count
} EquipmentType;

typedef enum
{
    COLLECTIBLE_SIMPLE_KEY,
    COLLECTIBLE_SPECIAL_KEY,
    __collectible_types_count
} CollectibleType;

typedef struct
{
    ItemKind kind;
    char name[32];
    V2i pos;
    bool picked_up;
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

typedef struct
{
    Item *items;
    size_t count;
    size_t capacity;
} Items;

typedef struct
{
    size_t *items;
    size_t count;
    size_t capacity;
} ItemsIndices;

typedef struct
{
    EquipmentType type;
    bool occupied;
    Item item;
} EquipmentSlot;

typedef struct
{
    EquipmentSlot *items;
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

struct Entity
{
    uint64_t id;
    EntityType type;
    char name[32];
    uint64_t faction;
    V2i pos;
    Direction direction;
    bool dead;
    EntityRank rank;
    size_t level;
    float movement_timer;

    int current_health;
    Stats base_stats;
    Stats extra_stats;

    Equipment equipment;
    Effects effects; 

    union {
        struct { // Player
            size_t xp;
            Inventory inventory;
        };
    };
};

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

struct Room
{
    size_t index;
    size_t width;
    size_t height;
    Tile *tiles;
    Entities entities;
    EntitiesIds *entities_map;
    Items items;
    ItemsIndices *items_map;
};

typedef struct
{
    Room *items;
    size_t count;
    size_t capacity;
} Rooms;

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

typedef struct
{
    bool enabled;
    enum {
        MAP_STATE_LIST,
        MAP_STATE_CONNECTIONS,
        __map_states_count
    } state;
} Map;

#define MAX_MESSAGES 25
typedef struct
{
    Data data;

    V2i camera;

    struct {
        char buffer[1024];
        char *lines[MAX_MESSAGES]; 
        size_t head;
        size_t count;
    } messages;

    // Timers
    float save_timer;
    float switch_timer;

    Map map;

    bool looking;
    bool showing_general_info;
    bool showing_help;
    bool showing_tooltips;
    struct {
        bool enabled;
        size_t index;
        EntitiesIds *entities;
    } show_entities_info;
} Game;

extern Game game;

#define EFFECTACTION_PARAMETERS Effect *effect, Entity *actor
typedef void (* EffectAction)(EFFECTACTION_PARAMETERS);
typedef struct
{
    const char *name;
    EffectAction action;
} EffectDefinition;

#define UNUSED_EFFECTACTION_PARAMETERS \
    UNUSED(effect);                    \
    UNUSED(actor);                     \

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

typedef enum
{
    R_PAIR = 1,
    R_PAIR_INV,
} ColorPair;

typedef enum
{
    DEATH_BY_ENTITY_ATTACK,
    DEATH_BY_EFFECT,
    __death_causes_count
} DeathCause;

typedef enum
{
    ESTATUS_OK,
    ESTATUS_DEAD
} EntityStatus;

/// Functions declaractions

// utils.c
bool streq(const char *s1, const char *s2);
bool strneq(const char *s1, const char *s2, size_t n);
const char *bool_to_string(bool value);
size_t index_at(size_t x, size_t y, size_t width);
float get_time_in_seconds(void);
char sign_as_char(int value);
_Noreturn void print_error_and_exit(const char *fmt, ...);
void log_this(char *format, ...);

// game.c
void game_init(void);
void process_pressed_key(void);
void write_message(const char *fmt, ...);

// ncurses.c
#define COLOR_VALUE_TO_NCURSES(value) ((value*1000)/255)
#define RGB_TO_NCURSES(r, g, b) COLOR_VALUE_TO_NCURSES(r), COLOR_VALUE_TO_NCURSES(g), COLOR_VALUE_TO_NCURSES(b)

extern size_t main_width;
extern size_t main_height;

void ncurses_init(void);
void ncurses_end(void);

// window.c
void handle_sigwinch(int signo);
void create_windows(void);
void update_windows(void);
void update_cursor(void);

// save.c
void init_data(void);
void save_data(void);
bool load_data(void);

// room.c
#define CURRENT_ROOM (&game.data.rooms.items[game.data.current_room_index])

#define DOOR_IS_OPEN true
#define DOOR_IS_HEAVY true
#define DOOR_LEADS_TO_NEW_ROOM -1
#define WALL_IS_DESTRUCTIBLE true

Room *generate_initial_room(void);
Room *generate_room(void);
char tile_char(const Tile *tile);
size_t room_tiles_count(Room *room);
Tile *tile_at(Room *room, size_t x, size_t y);
EntitiesIds *entities_at(Room *room, size_t x, size_t y);
ItemsIndices *items_at(Room *room, size_t x, size_t y);
Tile *get_tile_under_player(void);
EntitiesIds *get_entities_under_player(void);
Tile *get_looking_tile(void);
EntitiesIds *get_looking_entities(void);
Tile *get_random_perimeter_wall(Room *room);
Tile *get_door_that_leads_to(int room_index);
void set_tile_door(Tile *tile, bool open, bool heavy, int leads_to);
void spawn_random_entity(Room *room);
void room_update(Room *room);

// entity.c
#define PLAYER (&game.data.player)

const char *entity_rank_to_string(EntityRank rank);
const char *entity_type_to_string(EntityType type);
char entity_rank_char(EntityRank rank);
bool entity_is_player(Entity *e);
bool entity_is_dead(Entity *entity);
Stats entity_extra_stats_from_equipment(Entity *e);
Entity make_entity_random_at(size_t x, size_t y);
Entity *get_entity_by_id(Room *room, uint64_t id);
Faction *get_faction_by_id(uint64_t id, size_t *index);
Entity make_entity_random(size_t x_low, size_t x_high, size_t y_low, size_t y_high);
void entity_add_effect(Entity *entity, Effect effect);
void entity_move(Entity *e);
void player_move(Direction direction);
void player_equip_all(void);
void check_player_look_direction(void);

// item.c
const char *equipment_type_to_string(EquipmentType type);
const char *collectible_type_to_string(CollectibleType type);
Item make_item_equipment_random_of_type(EquipmentType type);
Item make_item_equipment_random(void);
EquipmentSlot make_equipment_slot_random(void);

// effect.c
EffectDefinition *get_effect(EffectType type);

// timer.c
void advance_all_timers(float dt);

// vector.c
V2i direction_vector(Direction dir);

// rng.c
void rng_init(RNG *rng, uint64_t seed);
uint64_t rng_generate(RNG *rng);
int64_t rng_range(RNG *rng, int64_t begin, int64_t end);
double rng_generate_double(RNG *rng);
bool rng_bernoulli(RNG *rng, double p);
void shuffle_indices(size_t *indices, size_t count, RNG *rng);

#define ROOMS_RNG    (&game.data.rooms_rng)
#define ENTITIES_RNG (&game.data.entities_rng)
#define ITEMS_RNG    (&game.data.items_rng)
#define COMBAT_RNG   (&game.data.combat_rng)

#endif // GAME_H
