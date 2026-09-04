#include "feed_web.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void url_policy(void)
{
    assert(feed_web_validate_rss_url("https://example.com/feed.xml"));
    assert(feed_web_validate_rss_url("http://news.example.org:8080/rss?q=1"));
    assert(feed_web_validate_rss_url("https://sub.domain.example.au/path"));

    assert(!feed_web_validate_rss_url("ftp://example.com/feed"));
    assert(!feed_web_validate_rss_url("https://localhost/feed"));
    assert(!feed_web_validate_rss_url("http://printer.local/rss"));
    assert(!feed_web_validate_rss_url("http://127.0.0.1/feed"));
    assert(!feed_web_validate_rss_url("http://10.0.0.5/feed"));
    assert(!feed_web_validate_rss_url("http://192.168.1.2/feed"));
    assert(!feed_web_validate_rss_url("https://user:pass@example.com/feed"));
    assert(!feed_web_validate_rss_url("https://example.com:65536/feed"));

    char oversized[FEED_WEB_URL_MAX + 2];
    memset(oversized, 'a', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = 0;
    assert(!feed_web_validate_rss_url(oversized));
}

static void parses_and_sanitizes_titles(void)
{
    const char *xml =
        "<rss><channel><item><title> First &amp; &lt;b&gt;News&lt;/b&gt; </title></item>"
        "<item><title><![CDATA[Second <em>story</em>]]></title></item></channel></rss>";
    char titles[2][FEED_RSS_TITLE_MAX];
    size_t count = feed_web_parse_titles(xml, strlen(xml), titles, 2);
    assert(count == 2);
    assert(strcmp(titles[0], "First & <b>News</b>") == 0);
    assert(strcmp(titles[1], "Second story") == 0);

    const char *atom = "<feed xmlns='x'><entry><title>Atom &#x54;itle</title></entry></feed>";
    count = feed_web_parse_titles(atom, strlen(atom), titles, 1);
    assert(count == 1 && strcmp(titles[0], "Atom Title") == 0);

    /* Results are bounded by both the requested count and title capacity. */
    const char *many = "<rss><item><title>one</title></item><item><title>two</title></item></rss>";
    count = feed_web_parse_titles(many, strlen(many), titles, 1);
    assert(count == 1 && strcmp(titles[0], "one") == 0);
    assert(feed_web_parse_titles(many, strlen(many), titles, 0) == 0);
}

int main(void)
{
    url_policy();
    parses_and_sanitizes_titles();
    puts("feed web tests passed");
    return 0;
}
