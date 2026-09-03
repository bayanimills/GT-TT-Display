#pragma once
/* esp_app_desc_t and esp_app_get_description() already live in the
 * esp_ota_ops.h stub beside this file, so this header only has to exist and
 * point at them. main/ota_update.c includes both, as real ESP-IDF allows.
 *
 * It is here so that main/ota_update.c, which the simulator excludes from its
 * build because flashing a partition is not something a workstation can
 * usefully pretend to do, can still be syntax-checked on a host rather than
 * only by a three minute round trip through CI. */
#include "esp_ota_ops.h"
