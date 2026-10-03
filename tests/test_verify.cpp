#include "test_framework.h"

#include <cmath>

#include "ai/verify/claim.h"
#include "ai/verify/claim_extractor.h"
#include "ai/verify/claim_verifier.h"

using namespace vgo;

TEST(extract_claims_block) {
    ClaimExtractor ex;
    const std::string text =
        "黑方领先。<claims>[{\"type\":\"score_lead\",\"value\":5.5,\"color\":\"black\"},"
        "{\"type\":\"winrate\",\"value\":0.62,\"color\":\"black\"}]</claims>";
    const auto claims = ex.extract(text, 9);
    CHECK_EQ(claims.size(), 2u);
    CHECK(claims[0].type == ClaimType::ScoreLead);
    CHECK_EQ(claims[0].value, 5.5);
    CHECK(claims[0].color == Color::Black);
    CHECK(claims[1].type == ClaimType::Winrate);
    CHECK_EQ(claims[1].value, 0.62);
}

TEST(extract_group_life_claim) {
    ClaimExtractor ex;
    const auto claims = ex.extract(
        "...<claims>[{\"type\":\"group_life\",\"point\":\"C3\",\"alive\":false}]</claims>",
        9);
    CHECK_EQ(claims.size(), 1u);
    CHECK(claims[0].type == ClaimType::GroupLife);
    CHECK(!claims[0].alive);
    CHECK(claims[0].point.x >= 0);
}

TEST(extract_prose_fallback_winrate) {
    ClaimExtractor ex;
    const auto claims = ex.extract("黑方胜率 62%，局势不错。", 9);
    bool has = false;
    for (const Claim& c : claims) {
        if (c.type == ClaimType::Winrate) {
            has = true;
            CHECK(std::abs(c.value - 0.62) < 0.01);
        }
    }
    CHECK(has);
}

TEST(extract_prose_fallback_score_lead) {
    ClaimExtractor ex;
    const auto claims = ex.extract("黑领先约 5 目。", 9);
    bool has = false;
    for (const Claim& c : claims) {
        if (c.type == ClaimType::ScoreLead) {
            has = true;
            CHECK(std::abs(c.value - 5.0) < 0.01);
            CHECK(c.color == Color::Black);
        }
    }
    CHECK(has);
}

TEST(verify_score_lead_pass) {
    Claim c;
    c.type = ClaimType::ScoreLead;
    c.value = 5.0;
    c.color = Color::Black;
    c.valid = true;
    Analysis a;
    a.valid = true;
    a.score_lead = 5.5;
    a.winrate = 0.6;
    const auto v = ClaimVerifier{}.verify({c}, Board(9), {}, a, 0.0, Color::Black);
    CHECK_EQ(v.size(), 1u);
    CHECK(v[0].disposition == ClaimDisposition::Pass);
    CHECK_EQ(v[0].claim.source, std::string("KataGo 分析"));
}

TEST(verify_score_lead_corrected) {
    Claim c;
    c.type = ClaimType::ScoreLead;
    c.value = 10.0;
    c.color = Color::Black;
    c.valid = true;
    Analysis a;
    a.valid = true;
    a.score_lead = 5.5;
    const auto v = ClaimVerifier{}.verify({c}, Board(9), {}, a, 0.0, Color::Black);
    CHECK_EQ(v.size(), 1u);
    CHECK(v[0].disposition == ClaimDisposition::Corrected);
    CHECK(v[0].truth.has_value());
    CHECK(std::abs(*v[0].truth - 5.5) < 0.01);
}

TEST(verify_winrate_unverifiable_no_engine) {
    Claim c;
    c.type = ClaimType::Winrate;
    c.value = 0.62;
    c.color = Color::Black;
    c.valid = true;
    Analysis a;  // invalid
    const auto v = ClaimVerifier{}.verify({c}, Board(9), {}, a, 0.0, Color::Black);
    CHECK_EQ(v.size(), 1u);
    CHECK(v[0].disposition == ClaimDisposition::Unverifiable);
}

TEST(verify_group_life_pass_and_corrected) {
    Board b(9);
    b.set(0, 0, Color::Black);
    b.set(1, 0, Color::Black);
    OwnershipMap om;
    om.size = 9;
    om.value.assign(81, 0.0);
    om.value[0] = -0.95;
    om.value[1] = -0.95;
    Analysis a;  // invalid; group-life only needs ownership

    Claim dead;
    dead.type = ClaimType::GroupLife;
    dead.point = {0, 0};
    dead.alive = false;
    dead.valid = true;
    auto v = ClaimVerifier{}.verify({dead}, b, om, a, 0.0, Color::Black);
    CHECK_EQ(v.size(), 1u);
    CHECK(v[0].disposition == ClaimDisposition::Pass);

    Claim alive;
    alive.type = ClaimType::GroupLife;
    alive.point = {0, 0};
    alive.alive = true;
    alive.valid = true;
    v = ClaimVerifier{}.verify({alive}, b, om, a, 0.0, Color::Black);
    CHECK(v[0].disposition == ClaimDisposition::Corrected);
}

TEST(verify_group_life_unverifiable_no_ownership) {
    Claim c;
    c.type = ClaimType::GroupLife;
    c.point = {0, 0};
    c.alive = false;
    c.valid = true;
    const auto v =
        ClaimVerifier{}.verify({c}, Board(9), OwnershipMap{}, Analysis{}, 0.0, Color::Black);
    CHECK_EQ(v.size(), 1u);
    CHECK(v[0].disposition == ClaimDisposition::Unverifiable);
}
