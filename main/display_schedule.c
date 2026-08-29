#include "display_schedule.h"

#define MINUTES_PER_DAY 1440U

bool display_schedule_should_be_off(bool enabled,
                                    uint16_t minute_of_day,
                                    uint16_t off_minute,
                                    uint16_t on_minute)
{
    if (!enabled || minute_of_day >= MINUTES_PER_DAY ||
        off_minute >= MINUTES_PER_DAY || on_minute >= MINUTES_PER_DAY ||
        off_minute == on_minute) {
        return false;
    }

    if (off_minute < on_minute) {
        return minute_of_day >= off_minute && minute_of_day < on_minute;
    }

    return minute_of_day >= off_minute || minute_of_day < on_minute;
}
