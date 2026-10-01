// 难度标定 spike（M3a.10）：让两个难度档位互相对弈，验证强度单调递增。
//
// 用法：
//   vgo_calibrate <katago二进制> <标准模型> <人类模型> <config> <levelA> <levelB> <局数> [棋盘大小]
//
//   levelA 执黑、levelB 执白，各下 <局数> 盘（每盘交替先后手），输出黑方胜率。
//   业余段（1-11）自动用人类模型 + humanSLProfile；职业段（12-19）用标准模型 + visits。
//
// 例： vgo_calibrate katago.exe b18-uec.bin.gz b18-humanv0.bin.gz gtp.cfg 12 13 4 9
//   即职业2段 vs 职业3段，9 路棋盘下 4 盘。

#include <QCoreApplication>
#include <QEventLoop>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "ai/engine_kata/kata_engine.h"
#include "ai/strength.h"
#include "go/game.h"

using namespace vgo;

namespace {

// 阻塞等待引擎返回一手（引擎回调为异步，这里用事件循环同步等待）。
Move requestMoveSync(EngineInterface* engine, const Board& board, Color to_move,
                     const StrengthParams& params) {
    QEventLoop loop;
    Move result;
    bool done = false;
    result.is_pass = true;
    engine->requestMove(board, to_move, params, [&](EngineResult r) {
        result = r.move;
        done = true;
        loop.quit();
    });
    if (!done) loop.exec();
    return result;
}

// 为某个难度档位选择模型与 humanSLProfile（与主程序 createAiServices 一致）。
EngineInterface* makeEngine(const QString& binary, const QString& stdModel,
                            const QString& humanModel, const QString& config, int level) {
    const StrengthParams sp = strengthForLevel(level);
    const bool useHuman = !sp.human_profile.empty() && !humanModel.isEmpty();
    const QString model = useHuman ? humanModel : stdModel;
    const QString profile = useHuman ? QString::fromStdString(sp.human_profile) : QString();
    auto* k = new KataGoEngine(binary, model, config, profile);
    if (k->available()) return k;
    delete k;
    return nullptr;
}

// 一盘对弈：黑、白各自用对应引擎；返回 1=黑胜 0=平 -1=白胜。
int playOneGame(int size, EngineInterface* black, const StrengthParams& pa,
                EngineInterface* white, const StrengthParams& pb) {
    Game g(size);
    int consecutive_passes = 0;
    const int max_moves = size * size * 4;
    while (!g.is_over() && g.move_number() < max_moves) {
        const Color cur = g.current();
        EngineInterface* eng = (cur == Color::Black) ? black : white;
        const StrengthParams& p = (cur == Color::Black) ? pa : pb;
        const Move m = requestMoveSync(eng, g.board(), cur, p);
        if (m.is_pass || !g.is_legal(m.p.x, m.p.y)) {
            g.pass();
            if (++consecutive_passes >= 2) break;
        } else {
            g.play(m.p.x, m.p.y);
            consecutive_passes = 0;
        }
    }
    const double lead = g.score_black();
    // 贴目 7.5 时目数为半目，lead 恒为 ±0.5 的奇数倍：>0 即黑胜（无平局）。
    return lead > 0.0 ? 1 : (lead < 0.0 ? -1 : 0);
}

}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    const QStringList args = app.arguments();
    if (args.size() < 8) {
        std::fprintf(stderr,
                     "用法: %s <katago二进制> <标准模型> <人类模型> <config> <levelA> <levelB> <局数> [棋盘大小]\n",
                     args.value(0).toLocal8Bit().constData());
        return 2;
    }
    const QString binary = args[1];
    const QString stdModel = args[2];
    const QString humanModel = args[3];
    const QString config = args[4];
    const int levelA = args[5].toInt();
    const int levelB = args[6].toInt();
    const int games = args[7].toInt();
    const int size = args.size() >= 9 ? args[8].toInt() : 9;

    EngineInterface* black = makeEngine(binary, stdModel, humanModel, config, levelA);
    EngineInterface* white = makeEngine(binary, stdModel, humanModel, config, levelB);
    if (!black || !white) {
        std::fprintf(stderr, "KataGo 启动失败\n");
        delete black;
        delete white;
        return 3;
    }

    const StrengthParams pa = strengthForLevel(levelA);
    const StrengthParams pb = strengthForLevel(levelB);

    int black_wins = 0, draws = 0;
    std::fprintf(stdout, "标定：%s（黑） vs %s（白），%d路，%d盘\n",
                 rankLabel(levelA).c_str(), rankLabel(levelB).c_str(), size, games);
    for (int i = 0; i < games; ++i) {
        // 交替先后手，抵消先手优势。
        const bool blackIsA = (i % 2 == 0);
        EngineInterface* b = blackIsA ? black : white;
        EngineInterface* w = blackIsA ? white : black;
        const StrengthParams& pbA = blackIsA ? pa : pb;
        const StrengthParams& pw = blackIsA ? pb : pa;
        const int r = playOneGame(size, b, pbA, w, pw);
        const int aResult = blackIsA ? r : -r;  // 换成 levelA 视角
        if (aResult > 0) ++black_wins;
        else if (aResult == 0) ++draws;
        std::fprintf(stdout, "  第%d盘: %s\n", i + 1,
                     aResult > 0 ? "A胜" : (aResult < 0 ? "B胜" : "平"));
    }

    std::fprintf(stdout, "\n结果：A（%s）%d胜 %d平 %d负（共%d盘）\n",
                 rankLabel(levelA).c_str(), black_wins, draws, games - black_wins - draws, games);
    const int a_wins = black_wins;  // levelA 视角胜场
    std::fprintf(stdout, "%s 对 %s 的胜率：%.0f%%\n", rankLabel(levelA).c_str(),
                 rankLabel(levelB).c_str(), 100.0 * a_wins / games);

    delete black;
    delete white;
    return 0;
}
