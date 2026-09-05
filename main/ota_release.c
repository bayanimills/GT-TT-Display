#include "ota_release.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *find_bounded(const char *s, size_t len, const char *needle)
{
    size_t n = strlen(needle);
    if (!s || n == 0 || n > len) return NULL;
    for (size_t i = 0; i + n <= len; i++) {
        if (memcmp(s + i, needle, n) == 0) return s + i;
    }
    return NULL;
}

static bool json_string(const char *json, size_t len, const char *key,
                        char *out, size_t out_n)
{
    char quoted[64];
    int qn = snprintf(quoted, sizeof(quoted), "\"%s\"", key);
    if (qn <= 0 || (size_t) qn >= sizeof(quoted)) return false;

    const char *p = find_bounded(json, len, quoted);
    if (!p) return false;
    const char *end = json + len;
    p += (size_t) qn;
    while (p < end && isspace((unsigned char)*p)) p++;
    if (p >= end || *p++ != ':') return false;
    while (p < end && isspace((unsigned char)*p)) p++;
    if (p >= end || *p++ != '"') return false;

    size_t used = 0;
    bool escape = false;
    while (p < end) {
        char c = *p++;
        if (escape) {
            /* Expected release fields contain no escapes. Supporting the JSON
             * single-character escapes still avoids accepting a truncated
             * string or silently retaining a backslash. */
            switch (c) {
                case '"': case '\\': case '/': break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                default: return false; /* \u is not valid in these fields */
            }
            escape = false;
        } else if (c == '\\') {
            escape = true;
            continue;
        } else if (c == '"') {
            if (out_n == 0 || used >= out_n) return false;
            out[used] = '\0';
            return true;
        }
        if (used + 1 >= out_n) return false;
        out[used++] = c;
    }
    return false;
}

static bool json_size(const char *json, size_t len, const char *key, size_t *out)
{
    char quoted[64];
    int qn = snprintf(quoted, sizeof(quoted), "\"%s\"", key);
    if (qn <= 0 || (size_t) qn >= sizeof(quoted)) return false;
    const char *p = find_bounded(json, len, quoted);
    if (!p) return false;
    const char *end = json + len;
    p += (size_t) qn;
    while (p < end && isspace((unsigned char)*p)) p++;
    if (p >= end || *p++ != ':') return false;
    while (p < end && isspace((unsigned char)*p)) p++;
    if (p >= end || !isdigit((unsigned char)*p)) return false;

    size_t value = 0;
    do {
        unsigned digit = (unsigned)(*p++ - '0');
        if (value > (SIZE_MAX - digit) / 10U) return false;
        value = value * 10U + digit;
    } while (p < end && isdigit((unsigned char)*p));
    *out = value;
    return true;
}

static bool next_object(const char *json, size_t len, size_t *cursor,
                        const char **object, size_t *object_len)
{
    bool in_string = false, escape = false;
    int depth = 0;
    size_t start = 0;
    for (size_t i = *cursor; i < len; i++) {
        char c = json[i];
        if (in_string) {
            if (escape) escape = false;
            else if (c == '\\') escape = true;
            else if (c == '"') in_string = false;
            continue;
        }
        if (c == '"') { in_string = true; continue; }
        if (c == '{') {
            if (depth++ == 0) start = i;
        } else if (c == '}' && depth > 0) {
            if (--depth == 0) {
                *object = json + start;
                *object_len = i - start + 1;
                *cursor = i + 1;
                return true;
            }
        } else if (c == ']' && depth == 0) {
            *cursor = len;
            return false;
        }
    }
    *cursor = len;
    return false;
}

static bool allowed_repo_url(const char *url, const char *owner, const char *repo)
{
    char prefix[192];
    int n = snprintf(prefix, sizeof(prefix),
                     "https://github.com/%s/%s/releases/download/", owner, repo);
    return n > 0 && (size_t)n < sizeof(prefix) &&
           strncmp(url, prefix, (size_t)n) == 0 &&
           url[n] != '\0' && strstr(url + n, "../") == NULL &&
           strstr(url + n, "..\\") == NULL;
}

static bool selected_asset_url(const char *url, const char *owner, const char *repo,
                               const char *tag, const char *name)
{
    char expected[OTA_RELEASE_URL_MAX];
    int n = snprintf(expected, sizeof(expected),
                     "https://github.com/%s/%s/releases/download/%s/%s",
                     owner, repo, tag, name);
    return n > 0 && (size_t)n < sizeof(expected) && strcmp(url, expected) == 0;
}

static bool sha256_digest_valid(const char *digest)
{
    static const char prefix[] = "sha256:";
    if (strncmp(digest, prefix, sizeof(prefix) - 1U) != 0) return false;
    const char *hex = digest + sizeof(prefix) - 1U;
    for (size_t i = 0; i < 64U; i++) {
        if (!isxdigit((unsigned char)hex[i])) return false;
    }
    return hex[64] == '\0';
}

bool ota_release_url_allowed(const char *url)
{
    if (!url) return false;
    return allowed_repo_url(url, "bayanimills", "GT-TT-Display") ||
           allowed_repo_url(url, "bitaxeorg", "BAP-GT-TOUCH");
}

bool ota_release_parse_github(const char *json, size_t len,
                              const char *owner, const char *repo,
                              size_t max_size, ota_release_t *out)
{
    if (!json || !owner || !repo || !out || len == 0 || max_size == 0) return false;

    ota_release_t result = { 0 };
    if (!json_string(json, len, "tag_name", result.tag, sizeof(result.tag))) return false;

    char wanted[96];
    int wn = snprintf(wanted, sizeof(wanted), "esp-display-ota-%s.bin", result.tag);
    if (wn <= 0 || (size_t)wn >= sizeof(wanted)) return false;

    const char *assets = find_bounded(json, len, "\"assets\"");
    if (!assets) return false;
    size_t remain = len - (size_t)(assets - json);
    const char *array = find_bounded(assets, remain, "[");
    if (!array) return false;
    size_t array_len = len - (size_t)(array + 1 - json);
    array++;

    ota_release_t versioned = { 0 }, generic = { 0 };
    int versioned_count = 0, generic_count = 0;
    size_t cursor = 0;
    const char *obj;
    size_t obj_len;
    while (next_object(array, array_len, &cursor, &obj, &obj_len)) {
        char name[128] = { 0 }, url[OTA_RELEASE_URL_MAX] = { 0 };
        char digest[OTA_RELEASE_DIGEST_MAX] = { 0 };
        char state[24] = { 0 };
        size_t size = 0;
        if (!json_string(obj, obj_len, "name", name, sizeof(name)) ||
            !json_string(obj, obj_len, "browser_download_url", url, sizeof(url)) ||
            !json_string(obj, obj_len, "state", state, sizeof(state)) ||
            !json_string(obj, obj_len, "digest", digest, sizeof(digest)) ||
            !json_size(obj, obj_len, "size", &size)) continue;
        if (size == 0 || size > max_size || !allowed_repo_url(url, owner, repo)) continue;

        ota_release_t *slot = NULL;
        int *count = NULL;
        if (strcmp(name, wanted) == 0) {
            slot = &versioned; count = &versioned_count;
        } else if (strcmp(name, "esp-display-ota.bin") == 0) {
            slot = &generic; count = &generic_count;
        } else {
            continue; /* notably excludes esp-display-<tag>.bin factory images */
        }
        if (strcmp(state, "uploaded") != 0 || !sha256_digest_valid(digest) ||
            !selected_asset_url(url, owner, repo, result.tag, name)) continue;
        (*count)++;
        snprintf(slot->tag, sizeof(slot->tag), "%s", result.tag);
        snprintf(slot->url, sizeof(slot->url), "%s", url);
        snprintf(slot->digest, sizeof(slot->digest), "%s", digest);
        slot->size = size;
    }

    if (versioned_count == 1) result = versioned;
    else if (versioned_count > 1) return false;
    else if (generic_count == 1) result = generic;
    else return false;

    *out = result;
    return true;
}

static bool parse_version(const char *s, unsigned long part[3], bool *prerelease)
{
    if (!s) return false;
    if (*s == 'v' || *s == 'V') s++;
    for (int i = 0; i < 3; i++) {
        if (!isdigit((unsigned char)*s)) return false;
        unsigned long value = 0;
        do {
            unsigned digit = (unsigned)(*s++ - '0');
            if (value > (1000000UL - digit) / 10UL) return false;
            value = value * 10UL + digit;
        } while (isdigit((unsigned char)*s));
        part[i] = value;
        if (i < 2) {
            if (*s != '.') return false;
            s++;
        }
    }
    if (*s != '\0' && *s != '-' && *s != '+') return false;
    if (prerelease) *prerelease = *s == '-';
    return true;
}

bool ota_release_version_is_newer(const char *latest, const char *current)
{
    unsigned long a[3], b[3];
    bool a_pre = false, b_pre = false;
    if (!parse_version(latest, a, &a_pre)) return false;
    if (!parse_version(current, b, &b_pre)) return true;
    for (int i = 0; i < 3; i++) {
        if (a[i] != b[i]) return a[i] > b[i];
    }
    /* For the same numeric version, the final release supersedes its beta/RC.
     * A prerelease must never displace an already-installed final release. */
    return b_pre && !a_pre;
}
