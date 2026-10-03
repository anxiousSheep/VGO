// 真实服务集成自检（L13）：读取 ModelManager 已配置的 KataGo 与 LLM，跑一次
// 真实分析 + 多轮经 AiAdvisor（工具注入 + 校验闸门）的真实问答，打印结果。
//
// 用法（无参数，直接读取用户已配置的模型）：
//   vgo_integration_check
//
// 覆盖：① KataGo 握手 / 分析 / ownership；② LLM 真实回答；③ D1 工具调用；
// ④ D2 校验闸门端到端（含确定性演示）。

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <cstdio>
#include <string>

#include "ai/advisor.h"
#include "ai/conversation_http/http_conversation.h"
#include "ai/engine_kata/kata_engine.h"
#include "ai/gtp.h"
#include "ai/ownership.h"
#include "ai/strength.h"
#include "ai/verify/claim_verifier.h"
#include "go/game.h"
#include "ui/model/ModelManager.h"

using namespace vgo;

namespace {

// 阻塞等待一个异步回调，超时 60s 后强制返回。
void runWithTimeout(QEventLoop* loop, int timeout_ms = 90000) {
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, loop, &QEventLoop::quit);
    timer.start(timeout_ms);
    loop->exec();
}

}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("VGO"));  // 与主程序一致，指向同一数据目录

    ModelManager& mm = ModelManager::instance();
    mm.load();
    const KataGoSettings& ks = mm.katago();
    const LlmSettings& ls = mm.llm();
    const QString apiKey = mm.apiKey();

    std::printf("=== 配置 ===\n");
    std::printf("KataGo: %s\n", mm.katagoStatusText().toUtf8().constData());
    std::printf("LLM: %s\n", mm.llmStatusText().toUtf8().constData());
    std::printf("API key: %s\n", apiKey.isEmpty() ? "（未配置）" : "（已配置）");

    // ---------- 1) KataGo 分析 ----------
    auto* engine = new KataGoEngine(ks.binary, ks.model, ks.config);
    if (!engine->available()) {
        std::printf("\n[失败] KataGo 启动失败\n");
        delete engine;
        return 3;
    }
    std::printf("\n=== KataGo 分析 ===\n");

    Game g(9);
    g.play(2, 2);
    g.play(2, 3);
    g.play(3, 2);
    g.play(2, 4);

    Analysis fullAnalysis;
    {
        QEventLoop loop;
        engine->analyze(g.board(), g.current(), strengthForLevel(kAnalysisLevelNormal),
                        [&](const Analysis& a) {
                            fullAnalysis = a;
                            std::printf("手数=%d 行棋方=%s\n", g.move_number(),
                                        g.current() == Color::Black ? "黑" : "白");
                            std::printf("winrate=%.3f score_lead=%.2f\n", a.winrate,
                                        a.score_lead);
                            std::printf("ownership=%s lead=%.2f\n",
                                        a.ownership.empty() ? "空" : "有",
                                        a.ownership.empty() ? 0.0 : ownershipLead(a.ownership));
                            std::printf("候选点: ");
                            for (std::size_t i = 0; i < a.candidates.size() && i < 5; ++i)
                                std::printf("%s ", gtpPoint(a.candidates[i].p, 9).c_str());
                            std::printf("\n");
                            loop.quit();
                        });
        runWithTimeout(&loop);
    }

    // ---------- 2) LLM 问答（AiAdvisor：工具注入 + 校验闸门） ----------
    std::printf("\n=== LLM 问答（DeepSeek） ===\n");
    auto* conv = new HttpConversationService(ls.baseUrl, apiKey, ls.model);
    AiAdvisor advisor(engine, conv, /*level=*/5);
    advisor.setAnalysisEngine(engine);
    advisor.setAnalysisLevel(mm.analysisLevel());

    auto askOne = [&](const std::string& q) {
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

    askOne("现在黑棋领先多少目？胜率多少？");
    // 复合问题：触发工具调用（死活 + 数目），验证 D1「模型自己调工具」。
    askOne("右下角这块棋是死是活？另外帮我数一下双方现在各有多少目。");

    // ---------- 3) 校验闸门确定性演示（对照真实分析） ----------
    std::printf("\n=== 校验闸门（对照真实分析） ===\n");
    {
        Claim fake;
        fake.type = ClaimType::ScoreLead;
        fake.color = Color::Black;
        fake.value = fullAnalysis.score_lead + 20.0;  // 故意报错 20 目
        fake.valid = true;
        const auto v = ClaimVerifier{}.verify({fake}, g.board(), fullAnalysis.ownership,
                                              fullAnalysis, g.score_black(), g.current());
        const char* verdict =
            v[0].disposition == ClaimDisposition::Corrected ? "已按引擎修正"
            : (v[0].disposition == ClaimDisposition::Pass ? "通过" : "不可验证");
        std::printf("真实领先=%.2f 故意报=%.2f -> %s\n", fullAnalysis.score_lead,
                    fake.value, verdict);

        Claim ok = fake;
        ok.value = fullAnalysis.score_lead;  // 准确断言
        const auto v2 = ClaimVerifier{}.verify({ok}, g.board(), fullAnalysis.ownership,
                                               fullAnalysis, g.score_black(), g.current());
        std::printf("真实领先=%.2f 如实报=%.2f -> %s\n", fullAnalysis.score_lead, ok.value,
                    v2[0].disposition == ClaimDisposition::Pass ? "通过" : "其他");
    }

    delete conv;
    delete engine;
    std::printf("\n[完成]\n");
    return 0;
}
