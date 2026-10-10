// G1–G6 实测修复的定式端到端复测（真实 KataGo + LLM）：
// ① 点三三适用性判定（joseki_lookup → summary/瘦身 → LLM 只叙述、不泄露内部字段）
// ② 「我该下哪」在点三三局面的推荐点术语（move_shape 注入 → 点入三三）
//
// 用法（无参数，读 ModelManager 已配置的 KataGo / LLM）：
//   vgo_joseki_e2e_check

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <cstdio>
#include <memory>
#include <string>

#include "ai/advisor.h"
#include "ai/conversation_http/http_conversation.h"
#include "ai/engine_kata/kata_engine.h"
#include "ai/gtp.h"
#include "ai/joseki.h"
#include "ai/joseki_apply.h"
#include "go/game.h"
#include "ui/model/ModelManager.h"

using namespace vgo;

namespace {

void runWithTimeout(QEventLoop* loop, int timeout_ms = 120000) {
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, loop, &QEventLoop::quit);
    timer.start(timeout_ms);
    loop->exec();
}

Point P(const std::string& gtp) {
    Point p;
    gtpParsePoint(gtp, 19, &p);
    return p;
}

}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("VGO"));

    ModelManager& mm = ModelManager::instance();
    mm.load();
    const KataGoSettings& ks = mm.katago();
    const LlmSettings& ls = mm.llm();

    std::printf("=== 配置 ===\nKataGo: %s\nLLM: %s\n",
                mm.katagoStatusText().toUtf8().constData(),
                mm.llmStatusText().toUtf8().constData());

    auto* engine = new KataGoEngine(ks.binary, ks.model, ks.config);
    if (!engine->available()) {
        std::printf("\n[失败] KataGo 启动失败\n");
        delete engine;
        return 3;
    }

    // 点三三前提局面：黑 4-4（Q16）+ 一间夹（M17），白小飞挂（O17）。
    Game g(19);
    const Point q16 = P("Q16"), o17 = P("O17"), m17 = P("M17");
    g.play(q16.x, q16.y);  // 黑 Q16（4-4 星位）
    g.play(o17.x, o17.y);  // 白 O17（小飞挂）
    g.play(m17.x, m17.y);  // 黑 M17（一间夹）

    auto* conv = new HttpConversationService(ls.baseUrl, mm.apiKey(), ls.model);
    AiAdvisor advisor(engine, conv, /*level=*/5);
    advisor.setAnalysisEngine(engine);
    advisor.setAnalysisLevel(mm.analysisLevel());
    advisor.setJosekiLibrary(std::make_shared<const JosekiLibrary>(JosekiLibrary::builtin()));

    auto ask = [&](const std::string& q) {
        std::printf("\n--- 问：%s ---\n", q.c_str());
        QElapsedTimer t;
        t.start();
        QEventLoop loop;
        advisor.ask(g, q, [&](const ConversationReply& r) {
            std::printf("%s\n", r.text.c_str());
            std::printf("unsure=%d 耗时=%lldms\n", r.unsure ? 1 : 0, t.elapsed());
            loop.quit();
        });
        runWithTimeout(&loop);
    };

    // ① 定式适用性：触发 joseki_lookup（判定书瘦身 + summary + 泄露闸门）。
    ask("点三三这个定式现在适用吗？可以实战演示吗？");

    // ② 推荐点：触发引擎推荐 + move_shape 术语注入。
    ask("现在轮白棋，我该下哪一手？");

    delete conv;
    delete engine;
    std::printf("\n[完成]\n");
    return 0;
}
