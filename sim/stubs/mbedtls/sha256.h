#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct { uint32_t opaque[32]; } mbedtls_sha256_context;
void mbedtls_sha256_init(mbedtls_sha256_context *ctx);
void mbedtls_sha256_free(mbedtls_sha256_context *ctx);
int mbedtls_sha256_starts(mbedtls_sha256_context *ctx, int is224);
int mbedtls_sha256_update(mbedtls_sha256_context *ctx, const unsigned char *input,
                          size_t ilen);
int mbedtls_sha256_finish(mbedtls_sha256_context *ctx, unsigned char output[32]);
