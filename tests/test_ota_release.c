#include "ota_release.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void parse_selects_versioned(void)
{
    const char *json =
        "{\"tag_name\":\"v1.1.2\",\"assets\":["
        "{\"name\":\"esp-display-v1.1.2.bin\",\"state\":\"uploaded\",\"digest\":\"sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"size\":3000000,"
          "\"browser_download_url\":\"https://github.com/bitaxeorg/BAP-GT-TOUCH/releases/download/v1.1.2/esp-display-v1.1.2.bin\"},"
        "{\"name\":\"esp-display-ota.bin\",\"state\":\"uploaded\",\"digest\":\"sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\",\"size\":2074048,"
          "\"browser_download_url\":\"https://github.com/bitaxeorg/BAP-GT-TOUCH/releases/download/v1.1.2/esp-display-ota.bin\"},"
        "{\"name\":\"esp-display-ota-v1.1.2.bin\",\"state\":\"uploaded\",\"size\":2074048,"
          "\"digest\":\"sha256:c79729dba0aed3b722025e51c894dfdae878a2b46b50239cb42de60d56ee73b8\","
          "\"browser_download_url\":\"https://github.com/bitaxeorg/BAP-GT-TOUCH/releases/download/v1.1.2/esp-display-ota-v1.1.2.bin\"}]}";
    ota_release_t r;
    assert(ota_release_parse_github(json, strlen(json), "bitaxeorg", "BAP-GT-TOUCH",
                                    4U * 1024U * 1024U, &r));
    assert(strcmp(r.tag, "v1.1.2") == 0);
    assert(strstr(r.url, "esp-display-ota-v1.1.2.bin") != NULL);
    assert(strcmp(r.digest, "sha256:c79729dba0aed3b722025e51c894dfdae878a2b46b50239cb42de60d56ee73b8") == 0);
    assert(r.size == 2074048U);
}

static void parse_accepts_beta_release_list(void)
{
    const char *json =
        "[{\"tag_name\":\"v1.6.0-beta.1\",\"prerelease\":true,\"assets\":["
        "{\"name\":\"esp-display-ota-v1.6.0-beta.1.bin\",\"state\":\"uploaded\","
        "\"digest\":\"sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
        "\"size\":100,\"browser_download_url\":\"https://github.com/bayanimills/GT-TT-Display/releases/download/v1.6.0-beta.1/esp-display-ota-v1.6.0-beta.1.bin\"}]}]";
    ota_release_t r;
    assert(ota_release_parse_github(json, strlen(json), "bayanimills", "GT-TT-Display",
                                    1000, &r));
    assert(strcmp(r.tag, "v1.6.0-beta.1") == 0);
}

static void rejects_bad_assets(void)
{
    const char *wrong_repo =
        "{\"tag_name\":\"v1.1.2\",\"assets\":[{\"name\":\"esp-display-ota.bin\",\"state\":\"uploaded\","
        "\"digest\":\"sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
        "\"size\":100,\"browser_download_url\":\"https://github.com/bitaxeorg/BAP-GT-TOUCH.evil/releases/download/v1.1.2/esp-display-ota.bin\"}]}";
    ota_release_t r;
    assert(!ota_release_parse_github(wrong_repo, strlen(wrong_repo), "bitaxeorg", "BAP-GT-TOUCH", 1000, &r));

    const char *oversize =
        "{\"tag_name\":\"v1.1.2\",\"assets\":[{\"name\":\"esp-display-ota.bin\",\"state\":\"uploaded\","
        "\"digest\":\"sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
        "\"size\":4194305,\"browser_download_url\":\"https://github.com/bitaxeorg/BAP-GT-TOUCH/releases/download/v1.1.2/esp-display-ota.bin\"}]}";
    assert(!ota_release_parse_github(oversize, strlen(oversize), "bitaxeorg", "BAP-GT-TOUCH", 4194304, &r));

    const char *factory_only =
        "{\"tag_name\":\"v1.1.2\",\"assets\":[{\"name\":\"esp-display-v1.1.2.bin\",\"state\":\"uploaded\","
        "\"digest\":\"sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
        "\"size\":100,\"browser_download_url\":\"https://github.com/bitaxeorg/BAP-GT-TOUCH/releases/download/v1.1.2/esp-display-v1.1.2.bin\"}]}";
    assert(!ota_release_parse_github(factory_only, strlen(factory_only), "bitaxeorg", "BAP-GT-TOUCH", 1000, &r));

    const char *tag_mismatch =
        "{\"tag_name\":\"v1.1.2\",\"assets\":[{\"name\":\"esp-display-ota.bin\",\"state\":\"uploaded\","
        "\"digest\":\"sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
        "\"size\":100,\"browser_download_url\":\"https://github.com/bitaxeorg/BAP-GT-TOUCH/releases/download/v1.1.1/esp-display-ota.bin\"}]}";
    assert(!ota_release_parse_github(tag_mismatch, strlen(tag_mismatch), "bitaxeorg", "BAP-GT-TOUCH", 1000, &r));

    const char *missing_digest =
        "{\"tag_name\":\"v1.1.2\",\"assets\":[{\"name\":\"esp-display-ota.bin\",\"state\":\"uploaded\","
        "\"size\":100,\"browser_download_url\":\"https://github.com/bitaxeorg/BAP-GT-TOUCH/releases/download/v1.1.2/esp-display-ota.bin\"}]}";
    assert(!ota_release_parse_github(missing_digest, strlen(missing_digest), "bitaxeorg", "BAP-GT-TOUCH", 1000, &r));

    const char *url_suffix =
        "{\"tag_name\":\"v1.1.2\",\"assets\":[{\"name\":\"esp-display-ota.bin\",\"state\":\"uploaded\","
        "\"digest\":\"sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
        "\"size\":100,\"browser_download_url\":\"https://github.com/bitaxeorg/BAP-GT-TOUCH/releases/download/v1.1.2/esp-display-ota.bin?raw=1\"}]}";
    assert(!ota_release_parse_github(url_suffix, strlen(url_suffix), "bitaxeorg", "BAP-GT-TOUCH", 1000, &r));
}

static void url_and_version_policy(void)
{
    assert(ota_release_url_allowed("https://github.com/bitaxeorg/BAP-GT-TOUCH/releases/download/v1.1.2/esp-display-ota.bin"));
    assert(ota_release_url_allowed("https://github.com/bayanimills/GT-TT-Display/releases/download/v1.3.1/esp-display-ota-v1.3.1.bin"));
    assert(!ota_release_url_allowed("http://github.com/bitaxeorg/BAP-GT-TOUCH/releases/download/v1/a.bin"));
    assert(!ota_release_url_allowed("https://github.com/bitaxeorg/BAP-GT-TOUCH.evil/releases/download/v1/a.bin"));
    assert(!ota_release_url_allowed("https://github.com/bitaxeorg/BAP-GT-TOUCH/raw/main/app.bin"));

    assert(ota_release_version_is_newer("v1.10.0", "v1.9.255"));
    assert(ota_release_version_is_newer("v1.2.3", "dev"));
    assert(!ota_release_version_is_newer("v1.2.3", "v1.2.3"));
    assert(!ota_release_version_is_newer("v1.2.2", "v1.2.3"));
    assert(ota_release_version_is_newer("v1.2.3", "v1.2.3-beta.2"));
    assert(!ota_release_version_is_newer("v1.2.3-beta.2", "v1.2.3"));
}

int main(void)
{
    parse_selects_versioned();
    parse_accepts_beta_release_list();
    rejects_bad_assets();
    url_and_version_policy();
    puts("ota release tests passed");
    return 0;
}
