#include <assert.h>
#include <stdbool.h>

#include "display_schedule.h"

static void test_disabled_and_invalid_windows(void)
{
    assert(!display_schedule_should_be_off(false, 23U * 60U, 22U * 60U, 7U * 60U));
    assert(!display_schedule_should_be_off(true, 12U * 60U, 8U * 60U, 8U * 60U));
    assert(!display_schedule_should_be_off(true, 1440U, 8U * 60U, 9U * 60U));
    assert(!display_schedule_should_be_off(true, 8U * 60U, 1440U, 9U * 60U));
    assert(!display_schedule_should_be_off(true, 8U * 60U, 7U * 60U, 1440U));
}

static void test_overnight_window(void)
{
    assert(!display_schedule_should_be_off(true, 21U * 60U + 59U, 22U * 60U, 7U * 60U));
    assert(display_schedule_should_be_off(true, 22U * 60U, 22U * 60U, 7U * 60U));
    assert(display_schedule_should_be_off(true, 23U * 60U + 30U, 22U * 60U, 7U * 60U));
    assert(display_schedule_should_be_off(true, 6U * 60U + 59U, 22U * 60U, 7U * 60U));
    assert(!display_schedule_should_be_off(true, 7U * 60U, 22U * 60U, 7U * 60U));
}

static void test_same_day_window(void)
{
    assert(!display_schedule_should_be_off(true, 12U * 60U + 59U, 13U * 60U, 15U * 60U));
    assert(display_schedule_should_be_off(true, 13U * 60U, 13U * 60U, 15U * 60U));
    assert(display_schedule_should_be_off(true, 14U * 60U + 59U, 13U * 60U, 15U * 60U));
    assert(!display_schedule_should_be_off(true, 15U * 60U, 13U * 60U, 15U * 60U));
}

int main(void)
{
    test_disabled_and_invalid_windows();
    test_overnight_window();
    test_same_day_window();
    return 0;
}
