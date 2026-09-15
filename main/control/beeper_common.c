/**
 * @file beeper_common.c
 *
 * See beeper_common.h. Pure arithmetic, no clock and no hardware.
 */
#include "beeper_common.h"

uint16_t beeper_level_permille(uint8_t level, uint8_t master)
{
    if (master > 100)
        master = 100;

    return (uint16_t)(((uint32_t)level * (uint32_t)master * 10u) / 255u);
}
