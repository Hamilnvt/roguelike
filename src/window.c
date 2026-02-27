#include "game.h"

static Window win_main = {0};
static Window win_bottom = {0};
static Window win_right = {0};
static Window *windows[] = {
    &win_main,
    &win_bottom,
    &win_right,
};
static const size_t windows_count = sizeof(windows)/sizeof(*windows);

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

void clamp_camera(void)
{
    int max_x = (int)CURRENT_ROOM->width - ((int)win_main.width - 2);
    int max_y = (int)CURRENT_ROOM->height - ((int)win_main.height - 2);

    if (max_x < 0) max_x = 0;
    if (max_y < 0) max_y = 0;

    if (game.camera.x < 0) game.camera.x = 0;
    if (game.camera.y < 0) game.camera.y = 0;

    if (game.camera.x > max_x) game.camera.x = max_x;
    if (game.camera.y > max_y) game.camera.y = max_y;
}

static inline void update_camera(void)
{
    game.camera.x = PLAYER->pos.x - ((win_main.width - 2) / 2);
    game.camera.y = PLAYER->pos.y - ((win_main.height - 2) / 2);
    clamp_camera();
}

void update_window_main(void)
{
    box(win_main.win, 0, 0);

    if (game.map.enabled) {
        mvwprintw(win_main.win, win_main.height/2, win_main.width/2-10, "TODO: map");
        map_render(); // TODO: put into the Map struct the information about the rooms and their connections
    } else {
        update_camera();

        for (size_t screen_y = 1; screen_y < win_main.height-1; screen_y++) {
            for (size_t screen_x = 1; screen_x < win_main.width-1; screen_x++) {
                int world_x = game.camera.x + screen_x - 1;
                int world_y = game.camera.y + screen_y - 1;

                if (world_x >= (int)CURRENT_ROOM->width || world_y >= (int)CURRENT_ROOM->height) {
                    mvwaddch(win_main.win, screen_y, screen_x, ' ');
                    continue;
                }

                const Tile *tile = tile_at(CURRENT_ROOM, world_x, world_y);
                EntitiesIds *entities = entities_at(CURRENT_ROOM, world_x, world_y);
                ItemsIndices *items = items_at(CURRENT_ROOM, world_x, world_y);

                char c;

                //if (da_is_empty(entities)) c = tile_char(tile);
                //else {
                //    if (tile->type == TILE_FLOOR) {
                //        Entity *e = get_entity_by_id(CURRENT_ROOM, entities->items[(size_t)game.switch_timer%entities->count]);
                //        if (!e || entity_is_dead(e)) continue;
                //        c = entity_rank_char(e->rank);
                //    } else {
                //        size_t index = (size_t)game.switch_timer % (entities->count+1);
                //        if (index == entities->count) c = tile_char(tile);
                //        else {
                //            Entity *e = get_entity_by_id(CURRENT_ROOM, entities->items[index]);
                //            if (!e || entity_is_dead(e)) continue;
                //            c = entity_rank_char(e->rank);
                //        }
                //    }
                //}

                // Priority: Entity > Item > Tile
                if (!da_is_empty(entities)) {
                    // ... existing entity rendering logic ...
                    Entity *e = get_entity_by_id(CURRENT_ROOM, entities->items[(size_t)game.switch_timer%entities->count]);
                    if (!e || entity_is_dead(e)) c = tile_char(tile); 
                    else c = entity_rank_char(e->rank);
                } else if (!da_is_empty(items)) c = 'i'; 
                else c = tile_char(tile);
                mvwaddch(win_main.win, screen_y, screen_x, c);
            }
        }

        mvwaddch(win_main.win,
                PLAYER->pos.y - game.camera.y + 1,
                PLAYER->pos.x - game.camera.x + 1,
                '@');
    }
}

void update_window_bottom(void)
{
    werase(win_bottom.win);

    if (game.looking) {
        Tile *tile = game.looking ? get_looking_tile() : get_tile_under_player();
        EntitiesIds *entities = game.looking ? get_looking_entities() : get_entities_under_player();

        size_t line = 0;

        switch (tile->type)
        {
    //case TILE_DOOR:

            case TILE_FLOOR: wprintw(win_bottom.win, "Same old boring floor"); break;
            //case TILE_WALL:  wprintw(win_bottom.win, "A wall... wait, how'd I get up here?"); break; // TODO: this should appear when standing on a wall
            case TILE_WALL: {
                wprintw(win_bottom.win, "A wall");
                if (tile->destructible) wprintw(win_bottom.win, "... is that a crack?");
                else wprintw(win_bottom.win, " that seems pretty solid to me");
            } break;
            case TILE_DOOR: {
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
            } break;

            case __tile_types_count:
            default: break;
        }

        if (!da_is_empty(entities)) {
            mvwprintw(win_bottom.win, line++, 0, "Here: ");
            for (size_t i = 0; i < entities->count; i++) {
                Entity *e = get_entity_by_id(CURRENT_ROOM, entities->items[i]);
                char entity_marker = (game.show_entities_info.enabled && i == game.show_entities_info.index) ? '+' : '-';
                if (i > 0) wprintw(win_bottom.win, ", ");

                wprintw(win_bottom.win, "%c %s (Lvl %zu)", entity_marker, e->name, e->level);
            }
        }
    } else {
        size_t count_printed = 0;
        for (size_t i = 0; i < game.messages.count && count_printed < win_bottom.height; i++) {
            size_t idx = (game.messages.head - 1 - i + MAX_MESSAGES) % MAX_MESSAGES;

            char *line = game.messages.lines[idx];
            if (!line) continue;

            if (i == 0) wattron(win_bottom.win, A_BOLD);
            else wattron(win_bottom.win, A_DIM);

            mvwprintw(win_bottom.win, (win_bottom.height - 1) - count_printed, 0, "> %s", line);

            if (i == 0) wattroff(win_bottom.win, A_BOLD);
            else wattroff(win_bottom.win, A_DIM);

            count_printed++;
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
    mvwprintw(win_right.win, 0, 1, "%s (%s)", e->name, entity_type_to_string(e->type));
    size_t line = 2;
    mvwprintw(win_right.win, line++, 1, "Rank: %s level %zu ", entity_rank_to_string(e->rank), e->level);
    if (entity_is_player(e)) mvwprintw(win_right.win, line, 1, "Exp: %zu", PLAYER->xp);
    
    Faction *faction = get_faction_by_id(e->faction, NULL);
    mvwprintw(win_right.win, line++, 1, "Faction: %s", faction ? faction->name : "none");

    mvwprintw(win_right.win, line++, 1, "Health: ");
    if (game.showing_tooltips) {
        wprintw(win_right.win, "%d %c %d (max)", e->base_stats.health, sign_as_char(e->extra_stats.health),
                abs(e->extra_stats.health));
    }
    else wprintw(win_right.win, "%d", e->current_health);

    mvwprintw(win_right.win, line++, 1, "Defense: ");
    if (game.showing_tooltips) wprintw(win_right.win, "%d %c %d", e->base_stats.defense,
            sign_as_char(e->extra_stats.defense), abs(e->extra_stats.defense));
    else wprintw(win_right.win, "%d", e->base_stats.defense + e->extra_stats.defense);

    mvwprintw(win_right.win, line++, 1, "Attack: ");
    if (game.showing_tooltips) wprintw(win_right.win, "%d %c %d", e->base_stats.attack,
            sign_as_char(e->extra_stats.attack), abs(e->extra_stats.attack));
    else wprintw(win_right.win, "%d", e->base_stats.attack + e->extra_stats.attack);
    waddstr(win_right.win, " (");
    if (game.showing_tooltips) wprintw(win_right.win, "%d%% %c %d%%", e->base_stats.accuracy,
            sign_as_char(e->extra_stats.accuracy), abs(e->extra_stats.accuracy));
    else wprintw(win_right.win, "%d%%", e->base_stats.accuracy + e->extra_stats.accuracy);
    waddch(win_right.win, ')');

    mvwprintw(win_right.win, line++, 1, "Agility: ");
    if (game.showing_tooltips) wprintw(win_right.win, "%d %c %d", e->base_stats.agility,
            sign_as_char(e->extra_stats.agility), abs(e->extra_stats.agility));
    else wprintw(win_right.win, "%d", e->base_stats.agility + e->extra_stats.agility);

    mvwprintw(win_right.win, line++, 1, "Effects: ");
    if (da_is_empty(&e->effects)) {
        waddstr(win_right.win, "none");
    } else {
        da_foreach(e->effects, Effect, effect) {
            EffectDefinition *effect_definition = get_effect(effect->type);
            mvwprintw(win_right.win, line++, 1, "- %s", effect_definition->name);
        }
    }

    mvwprintw(win_right.win, line++, 1, "Equipment: ");
    if (da_is_empty(&e->equipment)) {
        waddstr(win_right.win, "none");
    } else {
        da_foreach(e->equipment, EquipmentSlot, slot) {
            mvwprintw(win_right.win, line++, 1, "- %s (%s)", equipment_type_to_string(slot->type),
                    slot->occupied ? slot->item.name : "empty");
        }
    }

    switch (e->type)
    {
    case ENTITY_PLAYER:
        mvwprintw(win_right.win, line++, 1, "Inventory: ");
        if (da_is_empty(&e->inventory)) {
            waddstr(win_right.win, "empty");
        } else {
            da_foreach(e->inventory, Item, item) {
                mvwprintw(win_right.win, line++, 1, "- %s ", item->name);
                switch (item->kind)
                {
                case ITEM_EQUIPMENT:
                    wprintw(win_right.win, "(%s)", equipment_type_to_string(item->equipment_type));
                    break;
                case ITEM_COLLECTIBLE:
                    wprintw(win_right.win, "(%s)", collectible_type_to_string(item->collectible_type));
                    break;
                case __item_kinds_count:
                default:
                    print_error_and_exit("Unreachable item kind %u in show_entity_info", item->kind);
                }
            }
        }
        break;

    case ENTITY_GENERIC: break;

    case __entity_types_count:
    default:
        print_error_and_exit("Unreachable entity type %u in show_entity_info", e->type);
    }
}

static const size_t SECONDS_IN_MINUTE = 60;
static const size_t SECONDS_IN_HOUR   = 60*SECONDS_IN_MINUTE;
static const size_t SECONDS_IN_DAY    = 24*SECONDS_IN_HOUR;
void update_window_right(void)
{
    wborder(win_right.win, ACS_VLINE, ' ', ' ', ACS_HLINE, ACS_VLINE, ' ', 0, ACS_HLINE);

    if (game.showing_general_info) {
        size_t line = 0;
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
        uint64_t id = game.show_entities_info.entities->items[game.show_entities_info.index];
        Entity *entity = get_entity_by_id(CURRENT_ROOM, id);
        if (entity) show_entity_info(entity);
    } else if (game.showing_help) {
        size_t line = 0;
        mvwprintw(win_right.win, line++, 1, "Entity Ranks:");
        for (EntityRank rank = 0; rank < __entity_ranks_count; rank++)
            mvwprintw(win_right.win, line++, 1, " %c    %s", entity_rank_char(rank), entity_rank_to_string(rank));
    } else {
        show_entity_info(PLAYER);
    }
}

void create_windows(void)
{
    get_terminal_size();
    win_main   = create_window(0, 0,
                               3*terminal_width/4, 3*terminal_height/4,
                               R_PAIR, update_window_main);
    main_width = win_main.width;
    main_height = win_main.height;

    win_bottom = create_window(0, 3*terminal_height/4, 
                               terminal_width, terminal_height/4+1,
                               R_PAIR, update_window_bottom);
    win_right  = create_window(3*terminal_width/4, 0,
                               terminal_width/4+1, 3*terminal_height/4,
                               R_PAIR, update_window_right);
}

void destroy_windows(void)
{
    for (size_t i = 0; i < windows_count; i++)
        delwin(windows[i]->win);
}

static inline void update_window(Window *window)
{
    werase(window->win);
    window->update();
    wnoutrefresh(window->win);
}

void update_windows(void)
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
    UNUSED(signo);
    get_terminal_size();
    destroy_windows();
    create_windows();
    
    V2i pos = PLAYER->pos;
    if (pos.y < 0) pos.y = 0;
    else if ((size_t)pos.y >= win_main.height) pos.y = win_main.height - 1;
    if (pos.x < 0) pos.x = 0;
    else if ((size_t)pos.x >= win_main.width)  pos.x = win_main.width - 1;
}
