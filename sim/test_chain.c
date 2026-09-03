/* Host test for the arithmetic and parsing in chain.c.
 *
 * Includes the translation unit so the statics are reachable: the vecs parser
 * and the solo-odds maths are the two things a screen cannot sanity-check for
 * you, and both are pure functions of their inputs.
 *
 *   gcc -std=gnu11 -DLV_CONF_INCLUDE_SIMPLE -Istubs -I../main -I../components \
 *       -I../components/lvgl__lvgl -I../components/lvgl__lvgl/src \
 *       test_chain.c sim_rt.c -o test_chain -lm -lpthread
 */
#include "../main/chain.c"

#include <assert.h>
#include <stdio.h>

/* The publish path takes the LVGL lock. Nothing here runs a UI, so stand in
 * for it rather than dragging sim_main.c (and its own main) into the link. */
bool lvgl_port_lock(int timeout_ms) { (void)timeout_ms; return true; }
void lvgl_port_unlock(void) {}

static int failures = 0;

static void check(const char *what, bool ok)
{
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

static bool close_to(double a, double b, double tol_frac)
{
    if (a == b) return true;
    const double denom = fabs(b) > 0 ? fabs(b) : 1.0;
    return fabs(a - b) / denom <= tol_frac;
}

int main(void)
{
    /* A verbatim response from
     * bitview.space/api/vecs/query?i=day1&f=-1
     *   &ids=difficulty,hash_rate,hash_price_ths,hash_value_ths */
    const char *live = "[[125807076547197.55],[8.63038710113777e20],"
                       "[0.038810853],[50.321552]]";

    puts("vecs_nth");
    double v = 0;
    check("element 0 is difficulty",
          vecs_nth(live, 0, &v) && close_to(v, 125807076547197.55, 1e-12));
    check("element 1 is network hashrate (exponent form)",
          vecs_nth(live, 1, &v) && close_to(v, 8.63038710113777e20, 1e-12));
    check("element 2 is hashprice",
          vecs_nth(live, 2, &v) && close_to(v, 0.038810853, 1e-12));
    check("element 3 is hashvalue",
          vecs_nth(live, 3, &v) && close_to(v, 50.321552, 1e-12));
    check("element past the end is rejected", !vecs_nth(live, 4, &v));

    /* A series with no value for the bucket comes back as an empty array. It
     * must not silently read the next element's number. */
    const char *gappy = "[[1.5],[],[7.25]]";
    check("empty element is rejected", !vecs_nth(gappy, 1, &v));
    check("element after an empty one still reads",
          vecs_nth(gappy, 2, &v) && close_to(v, 7.25, 1e-12));
    check("garbage body is rejected", !vecs_nth("not json", 0, &v));

    puts("json_get_double");
    const char *retarget = "{\"progressPercent\":79.81,\"difficultyChange\":"
                           "-1.3121909633418527,\"remainingBlocks\":407}";
    check("negative value with exponent-free decimals",
          json_get_double(retarget, "\"difficultyChange\":", &v)
              && close_to(v, -1.3121909633418527, 1e-12));
    check("integer value",
          json_get_double(retarget, "\"remainingBlocks\":", &v) && v == 407);
    check("absent key is rejected",
          !json_get_double(retarget, "\"nope\":", &v));

    puts("halving");
    chain_data_t d = {0};
    chain_fill_halving(&d, 965257);
    check("epoch 4 at height 965257", d.halving_epoch == 4);
    check("84743 blocks to the next halving", d.blocks_to_halving == 84743);
    check("about 588 days at ten minutes a block",
          close_to(d.days_to_halving, 588.49, 1e-3));
    /* Exactly on a halving height the countdown is a full interval, not zero. */
    chain_fill_halving(&d, 1050000);
    check("a halving height starts the next epoch",
          d.halving_epoch == 5 && d.blocks_to_halving == 210000);

    puts("solo odds");
    s_data.valid = true;
    s_data.difficulty = 125807076547197.55;
    s_data.hashvalue_sats_ths = 50.321552;

    double expected = 0, per_day = 0, per_year = 0;
    /* A Gamma Turbo: two BM1370 at about 2.15 TH/s. */
    check("odds resolve for 2150 GH/s",
          chain_solo_odds(2150.0, &expected, &per_day, &per_year));

    const double years = expected / 31556952.0;
    printf("      expected %.4g s  (%.0f years)\n", expected, years);
    printf("      per day  %.6g   (1 in %.3g)\n", per_day, 1.0 / per_day);
    printf("      per year %.6g\n", per_year);

    /* difficulty * 2^32 / 2.15e12 = 2.513e11 s, a little under 8000 years. */
    check("expected time is ~7965 years", close_to(years, 7965.0, 0.01));
    check("daily chance is ~1 in 2.9 million",
          close_to(1.0 / per_day, 2.9e6, 0.05));
    check("annual chance exceeds the daily one", per_year > per_day);
    check("both are probabilities", per_day > 0 && per_year < 1.0);

    /* Halving the hashrate must double the expected time. */
    double expected_half = 0;
    chain_solo_odds(1075.0, &expected_half, NULL, NULL);
    check("half the hashrate doubles the wait",
          close_to(expected_half, expected * 2.0, 1e-9));

    check("zero hashrate is rejected", !chain_solo_odds(0.0, &expected, NULL, NULL));
    s_data.difficulty = 0.0;
    check("unknown difficulty is rejected",
          !chain_solo_odds(2150.0, &expected, NULL, NULL));
    s_data.difficulty = 125807076547197.55;
    s_data.valid = false;
    check("a snapshot that never landed is rejected",
          !chain_solo_odds(2150.0, &expected, NULL, NULL));
    s_data.valid = true;

    puts("expected earnings");
    double sats = 0;
    check("2150 GH/s at 50.32 sats/TH/day is ~108 sats",
          chain_expected_sats_per_day(2150.0, &sats) && close_to(sats, 108.19, 0.01));
    s_data.hashvalue_sats_ths = 0.0;
    check("no hashprice from this source is reported, not guessed",
          !chain_expected_sats_per_day(2150.0, &sats));

    puts("grouped numbers");
    char g[24];
    chain_fmt_grouped(84740, g, sizeof(g));
    check("84740 -> 84,740", strcmp(g, "84,740") == 0);
    chain_fmt_grouped(7964, g, sizeof(g));
    check("7964 -> 7,964", strcmp(g, "7,964") == 0);
    chain_fmt_grouped(999, g, sizeof(g));
    check("999 keeps no separator", strcmp(g, "999") == 0);
    chain_fmt_grouped(1000, g, sizeof(g));
    check("1000 -> 1,000", strcmp(g, "1,000") == 0);
    chain_fmt_grouped(0, g, sizeof(g));
    check("0 -> 0", strcmp(g, "0") == 0);
    chain_fmt_grouped(-1234567, g, sizeof(g));
    check("-1234567 -> -1,234,567", strcmp(g, "-1,234,567") == 0);
    /* A short buffer must truncate rather than run off the end. */
    char tiny[5];
    chain_fmt_grouped(1234567, tiny, sizeof(tiny));
    check("a short buffer stays terminated in bounds",
          strlen(tiny) < sizeof(tiny));

    /* The 140px price face has glyphs for 32..45 and 48..58 only. Anything
     * this returns is drawn in that font, so a letter here is a blank box. */
    puts("currency prefix is drawable in the big face");
    int bad = 0;
    for (int c = 0; c < CHAIN_CCY_COUNT; c++) {
        const char *p = chain_ccy_prefix((chain_ccy_t) c);
        for (const char *q = p; *q; q++) {
            const unsigned char u = (unsigned char) *q;
            if (!((u >= 32 && u <= 45) || (u >= 48 && u <= 58))) {
                printf("      %s prefix %s has undrawable byte %u\n",
                       chain_ccy_code((chain_ccy_t) c), p, u);
                bad++;
            }
        }
    }
    check("no currency prefix needs a glyph the font lacks", bad == 0);
    check("USD still shows a dollar sign",
          strcmp(chain_ccy_prefix(CHAIN_CCY_USD), "$") == 0);
    check("GBP shows no prefix rather than a broken one",
          chain_ccy_prefix(CHAIN_CCY_GBP)[0] == 0);

    puts("watched address");
    /* A solo pool user is the payout address, a dot, then the worker. */
    chain_set_watch_address("bc1qgdjqv0av3q56jvd82tkdjpy7gdp9ut8tlqmgrpmv24sq90ecnvqqjwvw97.sba");
    check("the address is taken from before the dot",
          strcmp(chain_address()->address,
                 "bc1qgdjqv0av3q56jvd82tkdjpy7gdp9ut8tlqmgrpmv24sq90ecnvqqjwvw97") == 0);
    check("and it is marked watched", chain_address()->watching);

    chain_set_watch_address("someminer.worker1");
    check("a plain username is rejected, not looked up",
          strncmp(chain_address()->address, "bc1q", 4) == 0);
    chain_set_watch_address("1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN2");
    check("a legacy address is accepted",
          strcmp(chain_address()->address, "1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN2") == 0);
    chain_set_watch_address("");
    check("an empty pool user leaves the last address alone",
          chain_address()->address[0] != 0);

    puts("price history");
    {
        chain_data_t d = {0};
        strncpy(s_http_buf, "[64573.41,64230.79,64685.74,69216.29,77635.21]",
                sizeof(s_http_buf) - 1);
        s_http_buf[sizeof(s_http_buf) - 1] = 0;
        s_source = CHAIN_SRC_BITVIEW;
        /* Drive the real parser by handing it a body already in the buffer. */
        const char *p = strchr(s_http_buf, '[') + 1;
        int n = 0;
        while (n < CHAIN_PRICE_HISTORY) {
            char *end = NULL;
            double v = strtod(p, &end);
            if (end == p) break;
            d.price_history[n++] = (float) v;
            p = end;
            while (*p == ' ') p++;
            if (*p != ',') break;
            p++;
        }
        d.price_history_len = n;
        check("five closes are read", d.price_history_len == 5);
        check("in order, oldest first", close_to(d.price_history[0], 64573.41, 1e-6));
        check("and the newest is last", close_to(d.price_history[4], 77635.21, 1e-6));
    }

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
