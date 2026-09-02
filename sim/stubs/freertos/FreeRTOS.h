#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <unistd.h>
typedef int      BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t TickType_t;
#define pdTRUE   1
#define pdFALSE  0
#define pdPASS   1
#define pdFAIL   0
#define portMAX_DELAY          ((TickType_t)0xFFFFFFFFu)
#define portTICK_PERIOD_MS     1
#define configTICK_RATE_HZ     1000
#define pdMS_TO_TICKS(ms)      ((TickType_t)(ms))
#define portYIELD_FROM_ISR(x)  do { (void)(x); } while (0)
#define IRAM_ATTR
#define tskNO_AFFINITY         (-1)
