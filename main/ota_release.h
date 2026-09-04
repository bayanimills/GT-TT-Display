/**
 * Small, host-testable policy/parser for GitHub OTA releases.
 *
 * Keeping this separate from the ESP HTTP task makes the security-sensitive
 * choices (which repository, which asset, and which URL) deterministic and
 * unit-testable without ESP-IDF.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_RELEASE_TAG_MAX     32
#define OTA_RELEASE_URL_MAX     256
#define OTA_RELEASE_DIGEST_MAX  80

typedef struct {
    char tag[OTA_RELEASE_TAG_MAX];
    char url[OTA_RELEASE_URL_MAX];
    char digest[OTA_RELEASE_DIGEST_MAX];
    size_t size;
} ota_release_t;

/** Parse a GitHub release response and choose the OTA application image.
 *
 * The version-pinned `esp-display-ota-<tag>.bin` is preferred. The generic
 * `esp-display-ota.bin` is accepted only as a fallback. Factory images are
 * never selected. Duplicate matching assets, non-uploaded/missing-digest
 * assets, URLs not bound to the advertised tag/name, empty files, and files
 * larger than max_size are rejected.
 */
bool ota_release_parse_github(const char *json, size_t len,
                              const char *owner, const char *repo,
                              size_t max_size, ota_release_t *out);

/** True only for HTTPS release downloads from one of the two trusted repos. */
bool ota_release_url_allowed(const char *url);

/** Semantic version comparison. A valid latest version beats an unversioned
 * development build; prerelease suffixes are ignored for numeric comparison. */
bool ota_release_version_is_newer(const char *latest, const char *current);

#ifdef __cplusplus
}
#endif
