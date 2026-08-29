#pragma once

#include <stdbool.h>
#include <stdint.h>

bool display_schedule_should_be_off(bool enabled,
                                    uint16_t minute_of_day,
                                    uint16_t off_minute,
                                    uint16_t on_minute);
