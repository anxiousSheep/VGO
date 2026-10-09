// B4 讲解质量评估（离线 judge 工具，不进 CTest）。读取 ModelManager 已配置的
// KataGo 与 LLM，对 golden Q&A 集逐条：reply 为空则用真实 harness（AiAdvisor）现场
// 生成，再用强模型按 4 维 rubric 打分（raw WinHTTP 直连，judge 系统提示词），打印
// 4 维均值 + 每维最低分样本，作为 A1-A3 迭代的人工验收线。
//
// 用法（无参数读内置最小集；可传一个 JSONL 文件追加题目）：
//   vgo_quality_eval [golden.jsonl]

#include <QByteArray>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>
#include <cstdio>
#include <string>

#include "ai/advisor.h"
#include "ai/conversation_http/http_conversation.h"
#include "ai/conversation_http/winhttp_transport.h"
#include "ai/engine_kata/kata_engine.h"
#include "ai/quality_judge.h"
#include "go/game.h"
#include "ui/model/ModelManager.h"

using namespace vgo;

namespace {

// 阻塞等待一个异步回调，超时后强制返回。
void runWithTimeout(QEventLoop* loop, int timeout_ms = 90000) {
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, loop, &QEventLoop::quit);
    timer.start(timeout_ms);
    loop->exec();
}

// 把 baseUrl 规整成 /chat/completions 终点。
std::string chatCompletionsUrl(const QString& baseUrl) {
    QUrl url(baseUrl);
    QString path = url.path();
    if (!path.endsWith("/chat/completions")) {
        if (!path.endsWith('/')) path += '/';
        path += "chat/completions";
        url.setPath(path);
    }
    return url.toString().toStdString();
}

// 从 JSONL 文件追加题目（每行 {"question":..,"reference_truth":..,"reply":..,"phase":..}）。
void loadJsonl(const QString& path, std::vector<QualityCase>* cases) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    while (!f.atEnd()) {
        const QByteArray line = f.readLine().trimmed();
        if (line.isEmpty()) continue;
        const QJsonObject o = QJsonDocument::fromJson(line).object();
        QualityCase c;
        c.question = o["question"].toString().toStdString();
        c.reference_truth = o["reference_truth"].toString().toStdString();
        c.reply = o["reply"].toString().toStdString();
        c.phase = o["phase"].toString().toStdString();
        if (!c.question.empty() && !c.reference_truth.empty()) cases->push_back(std::move(c));
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("VGO"));

    ModelManager& mm = ModelManager::instance();
    mm.load();
    const LlmSettings& ls = mm.llm();
    const QString apiKey = mm.apiKey();
    const KataGoSettings& ks = mm.katago();

    if (ls.baseUrl.isEmpty() || ls.model.isEmpty()) {
        std::printf("[失败] 未配置 LLM（judge 与被评模型都需要）。请先在设置页配置模型。\n");
        return 2;
    }
    std::printf("LLM（judge + 被评）: %s\n", ls.model.toUtf8().constData());

    // golden 集：内置最小集 + 可选 JSONL。
    std::vector<QualityCase> cases = builtinQualityCases();
    if (argc > 1) loadJsonl(QString::fromLocal8Bit(argv[1]), &cases);

    // 被评回复生成：真实 harness（KataGo + HttpConversationService + AiAdvisor）。
    auto* engine = new KataGoEngine(ks.binary, ks.model, ks.config);
    auto* conv = new HttpConversationService(ls.baseUrl, apiKey, ls.model);
    AiAdvisor* advisor = engine->available() ? new AiAdvisor(engine, conv, 5) : nullptr;
    if (advisor) advisor->setAnalysisEngine(engine);

    Game g(9);
    g.play(2, 2);
    g.play(2, 3);
    g.play(3, 2);
    g.play(2, 4);

    const char* kNames[4] = {"correctness", "professionalism", "tone", "structure"};
    double sums[4] = {0.0, 0.0, 0.0, 0.0};
    double minScore[4] = {5.0, 5.0, 5.0, 5.0};
    std::string minQuestion[4];
    int scored = 0;

    const std::string url = chatCompletionsUrl(ls.baseUrl);
    const std::string auth =
        apiKey.isEmpty() ? std::string{} : ("Authorization: Bearer " + apiKey.toStdString());

    for (std::size_t idx = 0; idx < cases.size(); ++idx) {
        QualityCase& c = cases[idx];
        std::printf("\n[%zu/%zu] 问：%s\n", idx + 1, cases.size(), c.question.c_str());

        // 生成回复（reply 为空则现场生成）。
        std::string reply = c.reply;
        if (reply.empty() && advisor) {
            QEventLoop loop;
            advisor->ask(g, c.question, [&](const ConversationReply& r) {
                reply = r.text;
                loop.quit();
            });
            runWithTimeout(&loop);
        }
        if (reply.empty()) {
            std::printf("  （跳过：无回复且无法生成）\n");
            continue;
        }

        // judge 打分：raw WinHTTP 直连（强模型 + judge 系统提示词）。
        QJsonObject sys;
        sys["role"] = "system";
        sys["content"] = QString::fromStdString(buildJudgeSystemPrompt());
        QJsonObject usr;
        usr["role"] = "user";
        usr["content"] = QString::fromStdString(buildJudgePrompt(c));
        QJsonObject body;
        body["model"] = ls.model;
        body["messages"] = QJsonArray{sys, usr};
        body["stream"] = false;

        const WinHttpResult res =
            winHttpPost(url, QJsonDocument(body).toJson(QJsonDocument::Compact).toStdString(),
                        "application/json", auth, 120000);
        if (!res.ok || res.status >= 400) {
            std::printf("  （judge 失败：%s）\n",
                        res.error.empty() ? "HTTP 错误" : res.error.c_str());
            continue;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(res.body));
        const QString judge_reply = doc.object()["choices"]
                                        .toArray()
                                        .first()
                                        .toObject()["message"]
                                        .toObject()["content"]
                                        .toString();

        JudgeVerdict v;
        if (!parseJudgeVerdict(judge_reply.toStdString(), &v)) {
            std::printf("  （judge 回复解析失败：%s）\n",
                        judge_reply.left(120).toUtf8().constData());
            continue;
        }
        const double sc[4] = {v.scores.correctness, v.scores.professionalism, v.scores.tone,
                              v.scores.structure};
        std::printf("  correctness=%.1f professionalism=%.1f tone=%.1f structure=%.1f\n", sc[0],
                    sc[1], sc[2], sc[3]);
        if (!v.note.empty()) std::printf("  note: %s\n", v.note.c_str());
        for (int k = 0; k < 4; ++k) {
            sums[k] += sc[k];
            if (sc[k] < minScore[k]) {
                minScore[k] = sc[k];
                minQuestion[k] = c.question;
            }
        }
        ++scored;
    }

    if (scored == 0) {
        std::printf("\n[无有效打分]\n");
    } else {
        std::printf("\n=== 4 维均值（%d 题） ===\n", scored);
        for (int k = 0; k < 4; ++k)
            std::printf("%s: %.2f（最低 %.1f，来自「%s」）\n", kNames[k], sums[k] / scored,
                        minScore[k], minQuestion[k].c_str());
    }

    delete advisor;
    delete conv;
    delete engine;
    std::printf("\n[完成]\n");
    return 0;
}
