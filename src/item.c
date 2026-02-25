#include "game.h"

const char *item_type_to_string(ItemType type)
{
    switch (type)
    {
    case ITEM_HELMET:     return "Helmet";
    case ITEM_HAT:        return "Hat";
    case ITEM_GOGGLES:    return "Goggles";
    case ITEM_SCARF:      return "Scarf";
    case ITEM_CHESTPLATE: return "Chestplate";
    case ITEM_CHAUSSES:   return "Chausses";
    case ITEM_SHOE:       return "Shoe";
    case ITEM_GLOVE:      return "Glove";
    case ITEM_SWORD:      return "Sword";
    case ITEM_SHIELD:     return "Shield";
    case ITEM_SCROLL:     return "Scroll";
    case ITEM_STAFF:      return "Staff";

    case __item_types_count:
    default:
        print_error_and_exit("Unreachable item type %u in item_type_to_string", type);
    }
}

Item make_item_random_of_type(ItemType type)
{
    Item item = {
        .type = type,
        .durability = rng_generate(ITEMS_RNG) % 100
    };

    String item_name = {0};
    static size_t item_number = 1;
    s_push_fstr(&item_name, "%s %zu", item_type_to_string(type), item_number);
    strncpy(item.name, item_name.items, item_name.count);
    s_free(&item_name);

    switch (type)
    {
    case ITEM_HELMET:
    case ITEM_HAT:        
    case ITEM_GOGGLES:   
    case ITEM_SCARF:      
    case ITEM_CHESTPLATE: 
    case ITEM_CHAUSSES:
    case ITEM_SHOE:       
    case ITEM_GLOVE:      
    case ITEM_SWORD:      
    case ITEM_SHIELD:     
    case ITEM_SCROLL:     
    case ITEM_STAFF:      
        break; // TODO: each item type gives different stats

    case __item_types_count:
    default:
        print_error_and_exit("Unreachable item type %u in make_item_random_of_type", type);
    }

    // TODO: for now
    item.stats = (Stats) {
        .attack   = rng_generate(ITEMS_RNG) % 100,
        .accuracy = rng_generate(ITEMS_RNG) % 100,
        .health   = rng_generate(ITEMS_RNG) % 200,
        .defense  = rng_generate(ITEMS_RNG) % 100,
        .agility  = rng_generate(ITEMS_RNG) % 100,
    };

    return item;
}

Item make_item_random(void)
{ return make_item_random_of_type(rng_generate(ITEMS_RNG) % __item_types_count); }

ItemSlot make_item_slot_random(void)
{ return (ItemSlot){ .type = rng_generate(ITEMS_RNG) % __item_types_count }; }
