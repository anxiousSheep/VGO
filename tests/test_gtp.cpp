#include "test_framework.h"

#include "ai/gtp.h"

using namespace vgo;

TEST(gtp_encode) {
    const int size = 9;
    CHECK_EQ(gtpPoint(0, 8, size), "A1");   // bottom-left
    CHECK_EQ(gtpPoint(8, 0, size), "J9");   // 'I' skipped
    CHECK_EQ(gtpPoint(3, 5, size), "D4");
    CHECK_EQ(gtpPoint(Point{2, 2}, size), "C7");
}

TEST(gtp_parse) {
    const int size = 19;
    Point p;
    CHECK(gtpParsePoint("D4", size, &p));
    CHECK_EQ(p.x, 3);
    CHECK_EQ(p.y, 15);  // 19 - 4

    CHECK(gtpParsePoint("A19", size, &p));
    CHECK_EQ(p.x, 0);
    CHECK_EQ(p.y, 0);

    CHECK(gtpParsePoint("T1", size, &p));
    CHECK_EQ(p.x, 18);
    CHECK_EQ(p.y, 18);

    CHECK(!gtpParsePoint("I4", size, &p));   // 'I' is skipped
    CHECK(!gtpParsePoint("", size, &p));
    CHECK(!gtpParsePoint("Z5", size, &p));   // out of range column
    CHECK(!gtpParsePoint("D0", size, &p));   // out of range row
}

TEST(gtp_roundtrip) {
    const int size = 13;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            Point out;
            const std::string s = gtpPoint(x, y, size);
            CHECK(gtpParsePoint(s, size, &out));
            CHECK_EQ(out.x, x);
            CHECK_EQ(out.y, y);
        }
    }
}
