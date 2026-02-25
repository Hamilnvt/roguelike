#include "game.h"

#define UNUSED_EFFECTACTION_PARAMETERS \
    UNUSED(effect);                    \
    UNUSED(actor);                     \

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

