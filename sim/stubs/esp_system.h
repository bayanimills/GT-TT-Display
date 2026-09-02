#pragma once
#include "esp_err.h"
static inline void esp_restart(void) { }
static inline uint32_t esp_get_free_heap_size(void) { return 4u * 1024 * 1024; }
