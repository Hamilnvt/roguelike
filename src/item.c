#include "game.h"

const char *equipment_type_to_string(EquipmentType type)
{
    switch (type)
    {
    case EQUIPMENT_HELMET:     return "Helmet";
    case EQUIPMENT_HAT:        return "Hat";
    case EQUIPMENT_GOGGLES:    return "Goggles";
    case EQUIPMENT_SCARF:      return "Scarf";
    case EQUIPMENT_CHESTPLATE: return "Chestplate";
    case EQUIPMENT_CHAUSSES:   return "Chausses";
    case EQUIPMENT_SHOE:       return "Shoe";
    case EQUIPMENT_GLOVE:      return "Glove";
    case EQUIPMENT_SWORD:      return "Sword";
    case EQUIPMENT_SHIELD:     return "Shield";
    case EQUIPMENT_SCROLL:     return "Scroll";
    case EQUIPMENT_STAFF:      return "Staff";

    case __equipment_types_count:
    default:
        print_error_and_exit("Unreachable item type %u in equipment_type_to_string", type);
    }
}

const char *collectible_type_to_string(CollectibleType type)
{
    switch (type)
    {
    case COLLECTIBLE_SIMPLE_KEY:  return "Simple Key";
    case COLLECTIBLE_SPECIAL_KEY: return "Special Key";
    case __collectible_types_count:
    default:
        print_error_and_exit("Unreachable collectible type %u in collectible_type_to_string", type);
    }
}

Item make_item_equipment_random_of_type(EquipmentType type)
{
    Item item = {
        .kind = ITEM_EQUIPMENT,
        .equipment_type = type,
        .durability = rng_generate(ITEMS_RNG) % 100
    };

    String item_name = {0};
    static size_t item_number = 1;
    s_push_fstr(&item_name, "%s %zu", equipment_type_to_string(type), item_number);
    strncpy(item.name, item_name.items, item_name.count);
    s_free(&item_name);

    switch (type)
    {
    case EQUIPMENT_HELMET:
    case EQUIPMENT_HAT:        
    case EQUIPMENT_GOGGLES:   
    case EQUIPMENT_SCARF:      
    case EQUIPMENT_CHESTPLATE: 
    case EQUIPMENT_CHAUSSES:
    case EQUIPMENT_SHOE:       
    case EQUIPMENT_GLOVE:      
    case EQUIPMENT_SWORD:      
    case EQUIPMENT_SHIELD:     
    case EQUIPMENT_SCROLL:     
    case EQUIPMENT_STAFF:      
        break; // TODO: each item type gives different stats

    case __equipment_types_count:
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

Item make_item_equipment_random(void)
{ return make_item_equipment_random_of_type(rng_generate(ITEMS_RNG) % __equipment_types_count); }

EquipmentSlot make_equipment_slot_random(void)
{
    return (EquipmentSlot){ .type = rng_generate(ITEMS_RNG) % __equipment_types_count };
}
