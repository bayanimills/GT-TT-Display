#pragma once
#include "esp_err.h"
typedef struct { char version[32]; char project_name[32]; } esp_app_desc_t;
static inline const esp_app_desc_t *esp_app_get_description(void) { static esp_app_desc_t d = {"sim", "gt-touch"}; return &d; }
