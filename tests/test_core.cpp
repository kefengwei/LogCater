// Minimal unit tests for LogCater core logic (no external framework).
#include "core/LogBuffer.h"
#include "util/Timestamp.h"
#include <cassert>
#include <cstdio>
#include <vector>

static int g_failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); g_failures++; } } while (0)

static LogEntry makeEntry(const char* raw, const char* tag, char level) {
    LogEntry e;
    e.raw = raw;
    e.tag = tag;
    e.level = level;
    e.message = raw;
    return e;
}

static void testQueryFilters() {
    LogBuffer buf(100);
    buf.push(makeEntry("07-30 12:00:00.000 1 1 E TagA: hello world", "TagA", 'E'));
    buf.push(makeEntry("07-30 12:30:00.000 2 2 I TagB: world again", "TagB", 'I'));
    buf.push(makeEntry("07-30 13:00:00.000 3 3 D TagC: debug stuff", "TagC", 'D'));

    std::vector<LogEntry> out;

    // Single tag filter
    buf.query(out, "", "TagA", 0x3F, 0, "");
    CHECK(out.size() == 1 && out[0].tag == "TagA");

    // Multi-tag OR filter
    out.clear();
    buf.query(out, "", "TagA;TagB", 0x3F, 0, "");
    CHECK(out.size() == 2);

    // Text filter (case-insensitive)
    out.clear();
    buf.query(out, "HELLO", "", 0x3F, 0, "");
    CHECK(out.size() == 1 && out[0].tag == "TagA");

    // Level mask: only D and I
    out.clear();
    buf.query(out, "", "", (1 << 1) | (1 << 2), 0, "");
    CHECK(out.size() == 2);

    // Time filter: >= 12:30
    out.clear();
    buf.query(out, "", "", 0x3F, 0, "12:30:00");
    CHECK(out.size() == 2);

    // Exclude tag
    out.clear();
    buf.query(out, "", "", 0x3F, 0, "", "TagB");
    CHECK(out.size() == 2);
    for (const auto& e : out) CHECK(e.tag != "TagB");

    // maxResults
    out.clear();
    buf.query(out, "", "", 0x3F, 2, "");
    CHECK(out.size() == 2);
}

static void testTimestamp() {
    int64_t t = util::parseDropboxTimestamp("2026-07-30 12:34:56");
    CHECK(t > 0);
    // Round trip
    std::string s = util::formatTimestamp(t);
    CHECK(!s.empty());
}

int main() {
    testQueryFilters();
    testTimestamp();
    if (g_failures == 0) {
        std::printf("All tests passed.\n");
        return 0;
    }
    std::printf("%d test(s) FAILED.\n", g_failures);
    return 1;
}
