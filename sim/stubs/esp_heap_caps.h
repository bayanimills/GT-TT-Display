#pragma once
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include "sdkconfig.h"
static inline void *heap_caps_malloc(size_t sz, uint32_t caps) { (void) caps; return malloc(sz); }
static inline void *heap_caps_calloc(size_t n, size_t sz, uint32_t caps) { (void) caps; return calloc(n, sz); }
static inline void *heap_caps_realloc(void *p, size_t sz, uint32_t caps) { (void) caps; return realloc(p, sz); }
static inline void  heap_caps_free(void *p) { free(p); }
static inline size_t heap_caps_get_free_size(uint32_t caps) { (void) caps; return 4u * 1024 * 1024; }
static inline size_t heap_caps_get_largest_free_block(uint32_t caps) { (void) caps; return 1u * 1024 * 1024; }
