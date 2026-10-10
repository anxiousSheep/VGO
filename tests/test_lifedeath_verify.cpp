#include "test_framework.h"

#include <functional>
#include <vector>

#include "ai/engine.h"
#include "ai/lifedeath_verify.h"
#include "ai/strength.h"

using namespace vgo;

namespace {

// 测试替身引擎：按「谁占了要点」返回把守方棋块标死/标活的归属图。
struct FakeEngine : EngineInterface {
    bool kill_works = false;   // 攻方占要点 → 守方死？
    bool live_works = false;   // 守方占要点 → 守方活？
    Point key;
    Color defender = Color::White;
    std::vector<Point> group;

    void analyze(const Board& b, Color, const StrengthParams&,
                 EngineInterface::AnalysisCallback cb) override {
        Analysis a;
        OwnershipMap om;
        om.size = b.size();
        om.value.assign(static_cast<std::size_t>(b.size()) * b.size(), 0.0);
        const bool attacker_played = (b.at(key.x, key.y) == opponent(defender));
        const bool dead = attacker_played ? kill_works : !live_works;
        for (const Point& p : group) {
            if (b.at(p.x, p.y) != defender) continue;
            // ownership +1=黑 -1=白；白子被标为黑方领地(+0.9)即判死。
            om.value[static_cast<std::size_t>(b.index(p.x, p.y))] = dead ? 0.9 : -0.9;
        }
        a.ownership = om;
        a.valid = true;
        cb(a);
    }
    void requestMove(const Board&, Color, const StrengthParams&, Callback) override {}
};

LifeDeathResult runVerify(EngineInterface* eng, const Board& board, Color defender,
                          const std::vector<Point>& group,
                          const std::vector<Point>& kill, const std::vector<Point>& live) {
    LifeDeathVerifier v(eng, strengthForLevel(15));
    LifeDeathResult out;
    bool done = false;
    v.verify(board, defender, group, kill, live, [&](LifeDeathResult r) {
        out = std::move(r);
        done = true;
    });
    CHECK(done);
    return out;
}

Board makeBoard() {
    // 白守方棋块 (0,0)(1,0)(0,1)；要点 (1,1) 为空。
    Board b(9);
    b.set(0, 0, Color::White);
    b.set(1, 0, Color::White);
    b.set(0, 1, Color::White);
    return b;
}

}  // namespace

TEST(verify_dead) {
    FakeEngine e;
    e.kill_works = true;   // 攻方占要点即死
    e.live_works = false;  // 守方占要点仍救不活
    e.key = {1, 1};
    e.defender = Color::White;
    e.group = {{0, 0}, {1, 0}, {0, 1}};
    const LifeDeathResult r = runVerify(&e, makeBoard(), Color::White, e.group, {{1, 1}}, {{1, 1}});
    CHECK(r.status == LDStatus::Dead);
    CHECK(!r.alive);
    CHECK(r.verification == LDVerify::EngineVerified);
    CHECK(!r.kill_points.empty());
}

TEST(verify_alive) {
    FakeEngine e;
    e.kill_works = false;  // 攻方杀不掉
    e.live_works = true;   // 守方补要点即活
    e.key = {1, 1};
    e.defender = Color::White;
    e.group = {{0, 0}, {1, 0}, {0, 1}};
    const LifeDeathResult r = runVerify(&e, makeBoard(), Color::White, e.group, {{1, 1}}, {{1, 1}});
    CHECK(r.status == LDStatus::Alive);
    CHECK(r.alive);
    CHECK(!r.live_points.empty());
}

TEST(verify_unsettled) {
    FakeEngine e;
    e.kill_works = true;   // 攻方占即死
    e.live_works = true;   // 守方占即活 → 先手相关
    e.key = {1, 1};
    e.defender = Color::White;
    e.group = {{0, 0}, {1, 0}, {0, 1}};
    const LifeDeathResult r = runVerify(&e, makeBoard(), Color::White, e.group, {{1, 1}}, {{1, 1}});
    CHECK(r.status == LDStatus::Unsettled);
    CHECK(!r.alive);
}

TEST(verify_unknown) {
    FakeEngine e;
    e.kill_works = false;
    e.live_works = false;
    e.key = {1, 1};
    e.defender = Color::White;
    e.group = {{0, 0}, {1, 0}, {0, 1}};
    const LifeDeathResult r = runVerify(&e, makeBoard(), Color::White, e.group, {{1, 1}}, {{1, 1}});
    CHECK(r.status == LDStatus::Unknown);
}

TEST(verify_no_engine_degrades) {
    LifeDeathVerifier v(nullptr, strengthForLevel(15));
    LifeDeathResult out;
    bool done = false;
    v.verify(makeBoard(), Color::White, {{0, 0}, {1, 0}, {0, 1}}, {{1, 1}}, {{1, 1}},
             [&](LifeDeathResult r) {
                 out = std::move(r);
                 done = true;
             });
    CHECK(done);
    CHECK(out.verification == LDVerify::None);
    CHECK(out.status == LDStatus::Unknown);
}
