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
 * - make entities_map a map to a da of pointer to entities so that many entities can exist in the same tile
 * - when hovering on entities pressing 'i' shows their stats in the right window
 * - since now entities can stack, there's no need to check if two of them "collide" they are just put in the same tile
 * - monsters drop key to open doors
 *   > non openable doors can be opened by defeating a King in the room and lead to special rooms (?)
 * - invetory to store/equip items
 * - each step increments a "timer" and after some time some actions are performed (a monster moves, a new monster spawns, something good/bad happens)
 * - clear entities_map at the end of the iteration and repopulate it at the beginning?
 * - monsters in a new room spawn accordingly to player's level
*/

#include <string.h>
#include <signal.h>
#include <stdbool.h>
#include <termios.h>
#include <ncurses.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include "dynamic_arrays.h"

#define DEBUG true

static inline bool streq(const char *s1, const char *s2) { return strcmp(s1, s2) == 0; }
static inline bool strneq(const char *s1, const char *s2, size_t n) { return strncmp(s1, s2, n) == 0; }

#define BOOL_AS_CSTR(value) ((value) ? "true" : "false")

#define index(x, y, width) ((y)*(width) + (x))
#define at(A, x, y, width) ((A)[index(x, y, width)])
#define index_in_room(room, x, y) ((y)*(room)->tilemap.width + (x))
#define tile_at(room, x, y) (at((room)->tilemap.tiles, (x), (y), (room)->tilemap.width))
#define room_tiles_count(room) ((room)->tilemap.width*(room)->tilemap.height)
#define entities_at(room, x, y) (&(at((room)->entities_map, (x), (y), (room)->tilemap.width)))

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

typedef struct
{
    size_t x;
    size_t y;
} Cursor;

typedef enum
{
    RANK_CIVILIAN,
    RANK_WARRIOR,
    RANK_NOBLE,
    RANK_KING,
    RANK_EMPEROR,
    RANK_WORLDLORD,
    ENTITY_RANKS_COUNT
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

    case ENTITY_RANKS_COUNT:
    default: print_error_and_exit("Unreachable entity rank %u in entity_rank_to_string", rank);
    }
}

typedef enum
{
    TILE_FLOOR,
    TILE_WALL,
    TILE_DOOR,
    TILES_COUNT
} TileType;

typedef struct
{
    TileType type;
    union {
       bool destructible; // Wall
       struct {           // Door
           bool openable;
           bool open;
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
    ITEM_TYPES_COUNT
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

#define ENTITY_NAME_MAX_LEN 31
typedef struct Entity
{
    char name[ENTITY_NAME_MAX_LEN + 1];
    Cursor pos;
    Cursor direction;

    EntityRank rank;
    size_t level;

    Equipment equipment;

    int hp;
    int defense;
    int chance;
    int attack;
    int agility;
    Effects effects; 
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

    case ENTITY_RANKS_COUNT:
    default: print_error_and_exit("Unreachable entity rank %u in get_entity_char", e.rank);
    }
}

char get_tile_char(Tile tile)
{
    switch (tile.type)
    {
    case TILE_FLOOR:  return ' ';
    case TILE_WALL:   return '#';
    case TILE_DOOR:   return 'O';

    case TILES_COUNT:
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

    case TILES_COUNT:
    default: print_error_and_exit("Unreachable tile type %u in tile_type_to_string", type);
    }
}

static inline Entity make_entity_random_at(size_t x, size_t y)
{
    Entity e = {0};
    memcpy(e.name, "Entity", 6); // TODO: random name based on rank
    e.pos = (Cursor){x, y};
    // TODO:
       // > if an entity spawns on top of another entity it triggers some event:
       //   - on player: ambush
       //   - on another entity: combat
       // > maybe it's better to have a fixed grid rather than a dynamic array, so that I can easily check if the slot is already occupied
       //   - I can then create rooms with walls and whatnot (screen is then a graphical representation of the grid)
       //   - If slot is, say, -1 then it's not possible to spawn an entity there, try again...
    e.rank = rand() % ENTITY_RANKS_COUNT;
    e.level = rand() % (10*(e.rank+1)) + 1; // TODO: think about the level, what does it give to the entity?
                                            //       Does it boosts its stats in some way?
                                            //       It can be the lower value for the spawned entities
    e.hp = rand() % (100*(e.rank+1));
    e.defense = rand() % (10*(e.rank+1));
    e.chance = rand() % (100*(e.rank+1));
    e.attack = rand() % (100*(e.rank+1));
    e.agility = rand() % (10*(e.rank+1));

    return e;
}

static inline Entity make_entity_random(size_t x_low, size_t x_high, size_t y_low, size_t y_high)
{
    size_t x = (rand() % (x_high - x_low)) + x_low;
    size_t y = (rand() % (y_high - y_low)) + y_low;
    return make_entity_random_at(x, y);
}

static inline Tile make_tile_wall(bool destructible)
{
    return (Tile){
        .type = TILE_WALL,
        .destructible = destructible,
    };
}

static inline Tile make_tile_wall_random(void)
{
    bool destructible = rand()%2;
    return make_tile_wall(destructible);
}

static inline Tile make_tile_door(bool openable, bool open, int leads_to)
{
    return (Tile){
        .type = TILE_DOOR,
        .openable = openable,
        .open = open,
        .leads_to = leads_to
    };
}

static inline Tile make_tile_door_random(void)
{
    bool openable = true;
    bool open = rand()%2;
    int leads_to = -1; // TODO
    return make_tile_door(openable, open, leads_to);
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
    float total_time;
    Rooms rooms;
} Data;

typedef struct
{
    Data data;
    Room *current_room;
    float save_timer;
    char message[256];

    bool showing_general_info;
    struct {
        bool enabled;
        size_t index;
        EntitiesIndices *entities;
    } show_entities_info;
} Game;
static Game game = {0};

static inline Cursor *get_player_pos(void) { return &game.data.player.entity.pos; }
static inline bool entity_is_player(Entity *e) { return e == &game.data.player.entity; }
static inline Tile *get_tile_under_player(void)
{
    Cursor *pos = get_player_pos();
    return &tile_at(game.current_room, pos->x, pos->y);
}

static inline EntitiesIndices *get_entities_under_player(void)
{
    Cursor *pos = get_player_pos();
    return entities_at(game.current_room, pos->x, pos->y);
}

Tile *get_random_floor_tile(Room *room)
{
    int tries = 10;
    size_t x;
    size_t y;
    while (tries > 0) {
        x = rand() % (room->tilemap.width-1) + 1;
        y = rand() % (room->tilemap.height-1) + 1;
        Tile *tile = &tile_at(room, x, y);
        if (tile->type == TILE_FLOOR) return tile;
        else tries--;
    }
    for (size_t y = 1; y < room->tilemap.height-1; y++) {
        for (size_t x = 1; x < room->tilemap.width-1; x++) {
            Tile *tile = &tile_at(room, x, y);
            if (tile->type == TILE_FLOOR) return tile;
        }
    }
    return NULL;
}

bool get_random_empty_entity_slot_as_cursor(Room *room, Cursor *pos)
{
    int tries = 10;
    size_t x;
    size_t y;
    while (tries > 0) {
        x = rand() % (room->tilemap.width-1) + 1;
        y = rand() % (room->tilemap.height-1) + 1;
        Tile tile = tile_at(room, x, y);
        if (tile.type != TILE_WALL) {
            *pos = (Cursor){x, y};
            return true;
        } else tries--;
    }
    for (size_t y = 1; y < room->tilemap.height-1; y++) {
        for (size_t x = 1; x < room->tilemap.width-1; x++) {
            Tile tile = tile_at(room, x, y);
            if (tile.type != TILE_WALL) {
                *pos = (Cursor){x, y};
                return true;
            }
        }
    }
    return false;
}

void spawn_random_entity(Room *room)
{
    Cursor pos;
    if (!get_random_empty_entity_slot_as_cursor(room, &pos)) return;
    Entity e = make_entity_random_at(pos.x, pos.y);
    da_push(&room->entities, e);
    EntitiesIndices *entities = &room->entities_map[index_in_room(room, pos.x, pos.y)];
    da_push(entities, room->entities.count-1);
}

Room generate_room(size_t width, size_t height)
{
    Room room = {
        .tilemap = (TileMap){
            .tiles = malloc(sizeof(Tile)*width*height),
            .width = width,
            .height = height
        },
        .entities = (Entities){0},
        .entities_map = malloc(sizeof(EntitiesIndices)*width*height)
    };

    // TODO: si puo' migliorare questo loop
    for (size_t y = 0; y < height; y++) {
        for (size_t x = 0; x < width; x++) {
            size_t index = y*width + x;
            if (x == 0 || y == 0 || x == width-1 || y == height-1) {
                room.tilemap.tiles[index] = (Tile){
                    .type = TILE_WALL,
                    .destructible = false
                };
            }
        }
    }

    size_t doors_count = (rand() % 2) + 1;
    for (size_t i = 0; i < doors_count; i++) {
        Tile *tile = get_random_floor_tile(&room);
        if (!tile) break;
        *tile = make_tile_door_random();
    }

    size_t entities_count = (rand() % 10) + 1;
    for (size_t i = 0; i < entities_count; i++) {
        spawn_random_entity(&room);
    }

    return room;
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

void write_message(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    memset(game.message, 0, sizeof(game.message));
    vsnprintf(game.message, sizeof(game.message), fmt, ap);
    va_end(ap);
    log_this("MESSAGE: %s", game.message);
}

void save_entity(FILE *f, Entity *e)
{
    // Name
    fwrite(e->name, sizeof(char), sizeof(e->name), f);
    
    // POD
    fwrite(&e->pos,    sizeof(Cursor),     1, f);
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

    // POD
    fwrite(&game.data.total_time, sizeof(float), 1, save_file);

    // Rooms TODO
    //fwrite(&game.data.entities.count, sizeof(size_t), 1, save_file);
    //da_foreach (game.data.entities, Entity*, e)
    //    save_entity(save_file, *e);

    fclose(save_file);
    write_message("saved");
    return true;
}

bool load_entity(FILE  *f, Entity *e)
{
    // Name
    if (fread(e->name, sizeof(char), sizeof(e->name), f) != sizeof(e->name)) return false;

    // POD
    if (fread(&e->pos,    sizeof(Cursor),     1, f) != 1) return false;
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
    if (!save_file) {
        if (errno == ENOENT) {
            // TODO: first initialization of all the things (player, rooms...) should happen here
            return save_game_data();
        } else return false;
    }

    // Player
    if (!load_entity(save_file, &game.data.player.entity)) goto fail;
    log_this("it was player's position");
    if (fread(&game.data.player.xp, sizeof(size_t), 1, save_file) != 1) goto fail;

    // POD
    if (fread(&game.data.total_time, sizeof(float), 1, save_file) != 1) goto fail;

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
    for (size_t y = 0; y < game.current_room->tilemap.height; y++) {
        for (size_t x = 0; x < game.current_room->tilemap.width; x++) {
            Tile tile = game.current_room->tilemap.tiles[y*game.current_room->tilemap.width + x];
            mvwaddch(win_main.win, y, x, get_tile_char(tile));
        }
    }

    for(size_t i = 0; i < game.current_room->entities.count; i++) {
        Entity e = game.current_room->entities.items[i];
        mvwaddch(win_main.win, e.pos.y, e.pos.x, get_entity_char(e));
    }

    mvwaddch(win_main.win, get_player_pos()->y, get_player_pos()->x, '@');
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
            if (door->openable) wprintw(win_bottom.win, "It seems that it can be opened, I wonder how, though.");
            else wprintw(win_bottom.win, "It's so massive that I cannot open it.");
        }
        break;

    case TILES_COUNT:
    default: print_error_and_exit("Unreachable tile type %u in update_window_bottom", tile->type);
    }

    if (!da_is_empty(entities)) {
        mvwprintw(win_bottom.win, line++, 1, "with the welcoming presence of:");
        for (size_t i = 0; i < entities->count; i++) {
            Entity *e = &game.current_room->entities.items[entities->items[i]];
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

void update_window_left(void)
{
    box(win_right.win, 0, 0);

    if (game.showing_general_info) {
        size_t line = 1;
        mvwprintw(win_right.win, line++, 1, "Total time: %.3f", game.data.total_time);
        if (strlen(game.message)) mvwprintw(win_right.win, line++, 1, "Message: %s", game.message);
    } else if (game.show_entities_info.enabled) {
        show_entity_info(&game.current_room->entities.items[game.show_entities_info.entities->items[game.show_entities_info.index]]);
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
                               R_PAIR, update_window_left);
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
    Cursor *player_pos = get_player_pos();
    size_t cy = player_pos->y;
    size_t cx = player_pos->x;
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
    
    Cursor *player_pos = get_player_pos();
    if (player_pos->y >= win_main.height) player_pos->y = win_main.height - 1;
    if (player_pos->x >= win_main.width)  player_pos->x = win_main.width - 1;
}

void game_init()
{
    //load_game_data(); // TODO rimetti

    // TODO: poi verranno caricati dal file di salvataggio
    {
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

        Room initial_room = generate_room(win_main.width, win_main.height);
        initial_room.index = 0;
        da_push(&game.data.rooms, initial_room);
        game.current_room = &game.data.rooms.items[0];

        Cursor pos;
        if (!get_random_empty_entity_slot_as_cursor(game.current_room, &pos)) {
            print_error_and_exit("E' un bel problema: TODO");
        }
        game.data.player.entity.pos = pos;

    }
}

bool entity_can_move(Entity *e, int dx, int dy)
{
    return (e->pos.x + dx > 0
         && e->pos.x + dx < game.current_room->tilemap.width
         && e->pos.y + dy > 0
         && e->pos.y + dy < game.current_room->tilemap.height
         && tile_at(game.current_room, e->pos.x + dx, e->pos.y + dy).type != TILE_WALL);
}

static inline void move_entity(Entity *e, int dx, int dy)
{
    if (entity_can_move(e, dx, dy)) {
        e->pos.x += dx;
        e->pos.y += dy;
        e->direction = (Cursor){dx, dy};
    }
}

void player_interact_with_door(Tile *door)
{
    if (door->open) {
        if (door->leads_to >= 0) {
            game.current_room = &game.data.rooms.items[door->leads_to];
            Cursor pos;
            if (!get_random_empty_entity_slot_as_cursor(game.current_room, &pos)) {
                print_error_and_exit("E' un bel problema: TODO");
            }
            game.data.player.entity.pos = pos;
        } else {
            Room room = generate_room(win_main.width, win_main.height);
            room.index = game.data.rooms.count;
            da_push(&game.data.rooms, room);
            int previous_room_index = game.current_room->index;
            game.current_room = &game.data.rooms.items[game.data.rooms.count-1];
            door->leads_to = game.data.rooms.count-1;
            // TODO: player position when coming back should be the one of the door from which they exited through;
            // (sono stanco, non so cosa ho scritto, buona notte)

            Cursor pos;
            if (!get_random_empty_entity_slot_as_cursor(game.current_room, &pos)) {
                print_error_and_exit("E' un bel problema: TODO");
            }
            game.data.player.entity.pos = pos;
            Tile *arrival = &tile_at(game.current_room, pos.x, pos.y);
            *arrival = make_tile_door(true, true, previous_room_index);
        }
    } else if (door->openable) {

    } else {

    }
}

void player_interact_with_entities(EntitiesIndices *entities)
{
    (void)entities;
}

static_assert(TILES_COUNT == 3, "Move player onto all tiles");
static inline void move_player(int dx, int dy)
{
    if (!entity_can_move(&game.data.player.entity, dx, dy)) return;
    Cursor *player_pos = get_player_pos();
    Cursor new_pos = {player_pos->x + dx, player_pos->y + dy};
    Tile *tile = &tile_at(game.current_room, new_pos.x, new_pos.y);
    if (tile->type == TILE_WALL) return;

    EntitiesIndices *entities = get_entities_under_player();
    if (da_is_empty(entities)) {
        if (tile->type == TILE_DOOR) player_interact_with_door(tile);
        else if (tile->type == TILE_FLOOR) {
            game.data.player.entity.pos = new_pos;
            game.data.player.entity.direction = (Cursor){dx, dy};
        }
    } else player_interact_with_entities(entities);
}

// TODO: non funziona :)
void check_player_look_direction(void)
{
    Cursor pos       = game.data.player.entity.pos;
    Cursor direction = game.data.player.entity.direction;
    EntitiesIndices *entities = entities_at(game.current_room, pos.x + direction.x, pos.y + direction.y);
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
            move_player(0, -1);
            break;

        case 's':
        case KEY_DOWN: move_player(0, 1); break;

        case 'a':
        case KEY_LEFT: move_player(-1, 0); break;

        case 'd':
        case KEY_RIGHT: move_player(1, 0); break;

        case CTRL('E'):
            spawn_random_entity(game.current_room);
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
                da_remove(&game.current_room->entities, entities->items[0]);
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
    for (size_t i = 0; i < room_tiles_count(game.current_room); i++) {
        da_clear(&game.current_room->entities_map[i]);
    }
    da_enumerate (game.current_room->entities, Entity, i, e) {
        size_t index = index_in_room(game.current_room, e->pos.x, e->pos.y);
        da_push(&game.current_room->entities_map[index], i);
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    srand(time(NULL));
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

        //advance_save_timer(dt); // TODO rimetti

        clear_and_populate_entities_map();

        napms(16); // TODO: do it with the calculated dt
    }

    return 0;
}
