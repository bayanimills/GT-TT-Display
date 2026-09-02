#pragma once
#include "freertos/FreeRTOS.h"
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack, void *arg, UBaseType_t prio, TaskHandle_t *out);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t stack, void *arg, UBaseType_t prio, TaskHandle_t *out, BaseType_t core);
void       vTaskDelay(TickType_t ticks);
void       vTaskDelete(TaskHandle_t t);
void       vTaskSuspend(TaskHandle_t t);
void       vTaskResume(TaskHandle_t t);
TickType_t xTaskGetTickCount(void);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
uint32_t   ulTaskNotifyTake(BaseType_t clear, TickType_t wait);
BaseType_t xTaskNotifyGive(TaskHandle_t t);
BaseType_t xTaskNotifyGiveFromISR(TaskHandle_t t, BaseType_t *woken);
