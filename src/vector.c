#include "game.h"

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

char direction_char(Direction dir)
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
