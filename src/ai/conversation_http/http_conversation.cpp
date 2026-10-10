#include "http_conversation.h"

#include <chrono>
#include <map>
#include <memory>
#include <thread>
#include <utility>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QUrl>

#include "ai/evaluate.h"
#include "ai/explain.h"
#include "ai/gtp.h"
#include "sse_parser.h"
#include "winhttp_transport.h"

namespace vgo {

namespace {

// A3 防截断：max_tokens 兜底重试上限。输出被 finish_reason=length 截断时，以该上限
// 重试一次，避免讲解被生硬截断（正常 stop 结束不受影响，不会更啰嗦）。
constexpr int kLengthRetryTokens = 4096;

// Wall-clock ms since epoch, for the tool-loop deadline (D1.10). Monotonic (steady).
qint64 nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// A2：取一句的「结论首句」——截到第一个句末标点或换行，再截断到固定长度。
QString firstSentence(const QString& text) {
    QString out;
    constexpr int kMaxLen = 60;
    for (QChar c : text) {
        if (out.size() >= kMaxLen) break;
        if (c == '\n' || c == '\r') break;
        out.append(c);
        if (c == QLatin1Char('。') || c == QLatin1Char('！') || c == QLatin1Char('？') ||
            c == QLatin1Char('；'))
            break;
    }
    return out.trimmed();
}

}  // namespace

QString buildStructureContract(int level) {
    if (level <= 6) {
        // 新手：一句话结论 → 类比/术语解释 → 一个最简变化（≤3 手）→ 一句鼓励。
        return QStringLiteral(
            "讲解结构（初学者档）：先用一句话给出明确结论；再用一个生活化的类比或术语解释"
            "讲清为什么；再给出一个最简变化（不超过 3 手）说明后果；最后给一句鼓励。\n"
            "语气耐心、温和，术语要解释清楚，多给一点思路引导。\n");
    }
    if (level >= 12) {
        // 高手：要点式，直接给坐标与数据。
        return QStringLiteral(
            "讲解结构（高手档）：要点式作答，直接给出坐标与数据（胜率/目数），不铺垫、"
            "不解释基础术语，简洁精炼。\n");
    }
    // 进阶：结论 → 数据依据（胜率/目数变化）→ 关键变化 → 练习建议。
    return QStringLiteral(
        "讲解结构（进阶档）：先给结论；再给数据依据（引用提供的胜率/目数变化）；"
        "再说明关键变化；最后给练习建议。客观、清晰。\n");
}

int maxTokensForLevel(int level) {
    // 初值 512/1024/768 过紧：讲解正文 + 注入的 Skills + <claims> JSON 常超出，
    // 触发 finish_reason=length 被生硬截断。这里调大上限（只是上限，模型仍按
    // stop 自然结束，不会更啰嗦），真正的兜底见 doChat/doChatStream 的 length 重试。
    if (level <= 6) return 1024;  // 新手（结论 + 类比 + 变化 + 鼓励 + claims）
    if (level >= 12) return 1024; // 高手（要点式 + 数据 + 多条 claims）
    return 2048;                  // 进阶（数据依据 + 关键变化 + 练习建议，最详尽）
}

QString buildSystemPrompt(const SkillRegistry& skills, QuestionClass cls,
                          const ConversationRequest& req) {
    QString s;
    if (req.user_color != Color::Empty) {
        // 人机对弈：有明确立场（用户执黑/白），从「对方」视角教学。
        s = QStringLiteral(
            "你是一位耐心、友善的围棋老师，在陪一位棋手对弈。\n"
            "重要约束：你只能依据下面提供的事实（棋盘、目数、落点评估、死活结论）作答，"
            "绝不能编造胜率、目数、坐标或棋手意图。如果不确定，就直说“这一点我还需要进一步分析”。\n");
    } else {
        // 摆棋模式：无立场，从用户提问角度 / 棋盘实际情况出发，以黑方/白方称呼双方。
        s = QStringLiteral(
            "你是一位客观的围棋分析师，协助用户分析棋局。\n"
            "重要约束：你只能依据下面提供的事实（棋盘、目数、落点评估、死活结论）作答，"
            "绝不能编造胜率、目数、坐标或棋手意图。如果不确定，就直说“这一点我还需要进一步分析”。\n"
            "你没有立场：不要预设用户执黑或执白，一律从用户提问的角度、以黑方/白方称呼双方作答。\n");
    }
    // A3：分级讲解结构契约（三档语气 + 结构 + 长度约定），替代原先「简短/口语化」一刀切。
    s += buildStructureContract(req.profile.level);
    // A.5 定式能力声明：有词典先查词典，词典外诚实说「还没收录」，不要硬编（治根因 3）。
    s += QStringLiteral(
        "你有一个定式词典（joseki_lookup 工具）。遇到定式、点三三、小飞挂、托退、双飞燕等"
        "提问，先调用 joseki_lookup 取走法序列，再用 demonstrate_go_moves 的 sequence 参数逐手"
        "演示。词典里没有的定式，诚实说「这个定式我还没收录」，绝不硬编走法。\n");
    // D3.6：注入跨局教练记忆——让 LLM 知道用户最近的弱项（如「最近在打劫上吃亏」），
    // 仅在相关时自然提及，不要突兀复述。
    if (!req.coach_memory.empty()) {
        s += QStringLiteral("教练记忆（跨局观察，仅在相关时自然提及，不要突兀复述）：%1\n")
                 .arg(QString::fromStdString(req.coach_memory));
    }
    // D-a：注入教学侧重指令（行为导向，与 coach_memory 的事实摘要并列、语义分离）。
    if (!req.weakness_directive.empty()) {
        s += QStringLiteral("教学侧重（仅自然融入，不突兀）：%1\n")
                 .arg(QString::fromStdString(req.weakness_directive));
    }
    // D2：要求模型在回答末尾附上可机读的断言块，供校验闸门对照引擎真值。
    s += QStringLiteral(
        "回答末尾请附上一段 <claims>…</claims> JSON 数组，列出你断言的所有可核验事实：\n"
        "领先目数用 {\"type\":\"score_lead\",\"value\":<目数>,\"color\":\"black\"或\"white\"}；\n"
        "胜率用 {\"type\":\"winrate\",\"value\":<0到1的小数>,\"color\":\"black\"或\"white\"}；\n"
        "某块棋死活用 {\"type\":\"group_life\",\"point\":\"<GTP坐标>\",\"alive\":true或false}。\n"
        "没有可核验的断言时可省略该块。\n");

    // N4 + D4：按问题类型 + 语义检索选择匹配的领域知识包（类别白名单优先、按 id
    // 去重、受预算裁剪），检索让「换种问法」或复合问题也能拿到相关知识。
    for (const Skill* sk : skills.select(cls, req.question, 3)) {
        s += QStringLiteral("\n【%1】%2")
                 .arg(QString::fromStdString(sk->name), QString::fromStdString(sk->prompt));
    }
    // A.5 定式目录注入：让模型知道词典里有哪些定式（只含索引，几十行）。
    if (!req.joseki_directory.empty()) {
        s += QStringLiteral("\n【定式词典目录】\n");
        s += QString::fromStdString(req.joseki_directory);
    }
    return s;
}

QJsonArray buildConversationMessages(const QJsonObject& system,
                                     const QString& summary,
                                     const std::vector<SessionTurn>& turns,
                                     const QJsonObject& user) {
    QJsonArray messages;
    messages.append(system);
    if (!summary.isEmpty()) {
        QJsonObject s;
        s["role"] = "system";
        s["content"] = QStringLiteral("前情提要：") + summary;
        messages.append(s);
    }
    for (const SessionTurn& t : turns) {
        QJsonObject u;
        u["role"] = "user";
        u["content"] = t.user;
        messages.append(u);
        QJsonObject a;
        a["role"] = "assistant";
        a["content"] = t.assistant;
        messages.append(a);
    }
    messages.append(user);
    return messages;
}

void compressSession(std::vector<SessionTurn>* turns, QString* summary, int max_turns) {
    if (!turns || !summary) return;
    if (max_turns <= 0) max_turns = 1;
    while (static_cast<int>(turns->size()) > max_turns) {
        const SessionTurn t = turns->front();
        turns->erase(turns->begin());
        const QString line =
            QStringLiteral("问：%1\n答：%2").arg(t.user, firstSentence(t.assistant));
        if (!summary->isEmpty()) *summary += QStringLiteral("\n");
        *summary += line;
    }
}

QString buildUserPrompt(const ConversationRequest& req, QuestionClass cls) {
    QString s;
    // D1.9：Other（未命中任何专用路径）由模型走工具按需取引擎真值——prompt 只保留
    // 无引擎基础事实（棋盘 / 规则计分 / 最近一手），胜率 / 候选点 / 死棋清单改由工具取。
    const bool stripEngine = (cls == QuestionClass::Other);
    s += QStringLiteral("棋盘（X=黑 O=白 .=空，最下方是 A 列起的坐标）：\n");
    s += QString::fromStdString(req.board_context);
    if (req.facts.analysis.valid && !stripEngine) {
        const double blackWinrate = (req.to_move == Color::Black)
                                        ? req.facts.analysis.winrate
                                        : 1.0 - req.facts.analysis.winrate;
        const double blackLead = (req.to_move == Color::Black)
                                     ? req.facts.analysis.score_lead
                                     : -req.facts.analysis.score_lead;
        s += QStringLiteral("形势（KataGo 实时）：黑方胜率 %1%，黑领先 %2 目。手数：%3。\n")
                 .arg(blackWinrate * 100.0, 0, 'f', 1)
                 .arg(blackLead, 0, 'f', 1)
                 .arg(req.move_number);
    } else {
        s += QStringLiteral("形势（规则计分数子法）：黑领先 %1 目。手数：%2。\n")
                 .arg(req.score_black, 0, 'f', 1)
                 .arg(req.move_number);
    }
    if (req.user_color != Color::Empty) {
        s += QStringLiteral("棋手执%1。")
                 .arg(req.user_color == Color::Black ? QStringLiteral("黑")
                                                     : QStringLiteral("白"));
    }
    s += QStringLiteral("当前轮到%1方落子。\n")
             .arg(req.to_move == Color::Black ? QStringLiteral("黑") : QStringLiteral("白"));
    // B2：局面阶段 + 最近手顺（让讲解获得「前文」：接上一步挂角，这手拆二生根）。
    if (!req.phase.empty()) {
        s += QStringLiteral("局面阶段：%1\n").arg(QString::fromStdString(req.phase));
    }
    if (!req.move_sequence.empty()) {
        s += QStringLiteral("最近手顺：%1\n").arg(QString::fromStdString(req.move_sequence));
    }
    if (req.facts.analysis.valid && !req.hint && !stripEngine) {
        int shown = 0;
        for (const auto& c : req.facts.analysis.candidates) {
            if (shown >= 4) break;
            // 候选点的 winrate/score_lead 是「行棋方」视角；统一转成黑方视角，
            // 与上方「形势」行的「黑方胜率/黑领先」口径一致，避免 LLM 混淆。
            const double cWinrate =
                (req.to_move == Color::Black) ? c.winrate : 1.0 - c.winrate;
            const double cLead =
                (req.to_move == Color::Black) ? c.score_lead : -c.score_lead;
            s += QStringLiteral("  候选点 %1：黑方胜率 %2%，黑领先 %3 目\n")
                     .arg(QString::fromStdString(gtpPoint(c.p, req.size)))
                     .arg(cWinrate * 100.0, 0, 'f', 1)
                     .arg(cLead, 0, 'f', 1);
            ++shown;
        }
    }
    if (req.hint) {
        s += QStringLiteral("棋手希望得到「半提示」（方向性引导），请只给思路，不要直接说出具体坐标。\n");
    }
    if (req.has_last_move) {
        s += QStringLiteral("最近一手的坐标：%1（%2方）。\n")
                 .arg(QString::fromStdString(gtpPoint(req.last_move, req.size)))
                 .arg(req.last_mover == Color::Black ? "黑" : "白");
    }
    const std::string exp = renderExplanation(req.facts.facts, req.last_mover);
    if (!exp.empty()) {
        s += QStringLiteral("最近一手的客观描述：%1\n").arg(QString::fromStdString(exp));
    }
    // 讲解增强（N1.4）：把 what-if 落点评估 / 死活结论 / 死棋清单作为硬事实喂给 LLM。
    if (req.facts.move_eval) {
        s += QStringLiteral("落点评估（硬事实）：%1\n")
                 .arg(QString::fromStdString(renderMoveEvaluation(*req.facts.move_eval, req.size)));
        // 程序已判定的恶手预警（与 UI 提示一致，供 LLM 引用而非重新发明）。
        const std::string warn = blunderWarning(*req.facts.move_eval);
        if (!warn.empty()) {
            s += QStringLiteral("恶手预警（程序判定，供参考）：%1\n")
                     .arg(QString::fromStdString(warn));
        }
    }
    // 程序确定性输出：本局已提示的恶手历史（与 UI 已提示内容对齐，供 LLM 引述）。
    if (!req.blunder_notes.empty()) {
        s += QStringLiteral("本局已提示的恶手（程序判定，供参考）：\n");
        for (const BlunderNote& b : req.blunder_notes) {
            s += QStringLiteral("  第 %1 手（%2）：%3\n")
                     .arg(b.move)
                     .arg(b.point.x >= 0
                              ? QString::fromStdString(gtpPoint(b.point, req.size))
                              : QStringLiteral("—"))
                     .arg(QString::fromStdString(b.text));
        }
    }
    if (req.facts.life_death) {
        s += QStringLiteral("死活结论（硬事实）：%1\n")
                 .arg(QString::fromStdString(renderLifeDeath(*req.facts.life_death, req.size)));
    }
    if (!stripEngine) {
        const std::string dead = renderDeadGroups(req.facts.dead_groups, req.size);
        if (!dead.empty()) s += QString::fromStdString(dead) + QStringLiteral("\n");
    }

    // 试下模式上下文：让模型知道已有进行中的演示，从而正确判断继续/重置/回退。
    if (req.in_demo) {
        s += QStringLiteral("注意：当前已有一个进行中的棋盘演示（试下模式），已下的手按编号为：%1\n"
                            "· 用户说「继续/接着/再演示」→ 调用演示工具，reset=false（在当前基础上续下）。\n"
                            "· 用户说「重新假设/重下/改第 k 手」或要回到某一步 → 调用演示工具，"
                            "undo_to = k（回退到第 k 手之前），再落新子续下。\n"
                            "· 用户明确说「完全重新/从头」→ reset=true。\n")
                 .arg(req.demo_moves.empty() ? QStringLiteral("（尚未落子）")
                                             : QString::fromStdString(req.demo_moves));
    }

    s += QStringLiteral("\n棋手的问题：%1").arg(QString::fromStdString(req.question));
    return s;
}

namespace {

// The tools exposed to the model via function-calling. The model decides when
// to call each and with which arguments (parsing natural language). The demo
// tool is always declared (UI-owned); the injected ToolRegistry (D1) adds the
// deterministic capabilities: count_territory / life_death_solve / ownership_map /
// dead_groups / analyze_position.
QJsonArray buildTools(const ToolRegistry* reg) {
    QJsonObject fn;
    fn["name"] = "demonstrate_go_moves";
    fn["description"] = QStringLiteral(
        "在棋盘上演示走法：进入试下模式，可回退若干步、落一手假设手，并用权威 AI 续下若干手。"
        "当用户要求演示、展示变化、模拟对局、询问后续应对、局部变化或「后面几步怎么下」时调用。"
        "当用户点名定式/具体变化时，先用 joseki_lookup 取序列，再用 sequence 逐手演示。");

    QJsonObject pPoint;
    pPoint["type"] = "string";
    pPoint["description"] = QStringLiteral(
        "假设手的 GTP 坐标（如 H6），必须是当前棋盘上的空点；无明确落点或不确定时为空字符串");
    QJsonObject pColor;
    pColor["type"] = "string";
    pColor["enum"] = QJsonArray{"black", "white", ""};
    pColor["description"] = QStringLiteral("假设手的执子方（black/white），无落点可省略");
    QJsonObject pPlies;
    pPlies["type"] = "integer";
    pPlies["description"] = QStringLiteral("权威 AI 续下的手数，默认 4，范围 1-8");
    QJsonObject pReset;
    pReset["type"] = "boolean";
    pReset["description"] = QStringLiteral(
        "true 表示从头重新演示（丢弃当前变化图）；false 或省略表示在当前演示基础上继续");
    QJsonObject pUndo;
    pUndo["type"] = "integer";
    pUndo["description"] = QStringLiteral(
        "回退到第 k 手之前（保留前 k-1 手，撤销第 k 手及之后的所有手）。"
        "例如用户说「重新假设/改第9手」就传 9；省略或 0 = 不回退");
    QJsonObject pSeqItem;
    pSeqItem["type"] = "object";
    pSeqItem["properties"] = QJsonObject{
        {"move", QJsonObject{{"type", QStringLiteral("string")},
                             {"description", QStringLiteral("GTP 坐标，如 C3")}}},
        {"color",
         QJsonObject{{"type", QStringLiteral("string")},
                     {"enum", QJsonArray{QStringLiteral("black"), QStringLiteral("white")}},
                     {"description", QStringLiteral("执子方，可省（省则跟随行棋方）")}}},
    };
    QJsonObject pSequence;
    pSequence["type"] = "array";
    pSequence["items"] = pSeqItem;
    pSequence["description"] = QStringLiteral(
        "固定线手顺（如定式词典返回的 sequence）：先逐手摆完这一串，再用权威 AI 续下。"
        "非空时按序摆子；例如点三三定式。");

    QJsonObject props;
    props["point"] = pPoint;
    props["color"] = pColor;
    props["plies"] = pPlies;
    props["reset"] = pReset;
    props["undo_to"] = pUndo;
    props["sequence"] = pSequence;

    QJsonObject params;
    params["type"] = "object";
    params["properties"] = props;
    fn["parameters"] = params;

    QJsonObject tool;
    tool["type"] = "function";
    tool["function"] = fn;

    QJsonArray tools;
    tools.append(tool);

    if (reg) {
        for (const Tool& t : reg->all()) {
            QJsonObject f;
            f["name"] = QString::fromStdString(t.name);
            f["description"] = QString::fromStdString(t.description);
            f["parameters"] =
                QJsonDocument::fromJson(QByteArray::fromStdString(t.json_schema)).object();
            QJsonObject o;
            o["type"] = "function";
            o["function"] = f;
            tools.append(o);
        }
    }
    return tools;
}

// 把 HTTP 状态 + 响应体翻译成对用户有用的中文：从 OpenAI 兼容端点常见的错误体
// （{"error":{"message":…}}）里抽出 message。
QString httpErrorDetail(int status, const std::string& body) {
    if (status <= 0) return QStringLiteral("请求超时或网络不可用（可稍后重试）");
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(body));
    QString msg;
    if (doc.isObject()) {
        msg = doc.object().value("error").toObject().value("message").toString();
    }
    if (!msg.isEmpty()) {
        return QStringLiteral("服务返回 %1：%2").arg(status).arg(msg);
    }
    return QStringLiteral("服务返回 HTTP %1").arg(status);
}

}  // namespace

QJsonArray buildToolFollowup(const QJsonArray& messages, const QString& content,
                             const QJsonArray& toolCalls,
                             const std::function<std::string(const QJsonObject&)>& exec) {
    QJsonArray next = messages;
    QJsonObject assistantMsg;
    assistantMsg["role"] = "assistant";
    if (!content.isEmpty()) assistantMsg["content"] = content;
    assistantMsg["tool_calls"] = toolCalls;
    next.append(assistantMsg);
    for (const QJsonValue& tv : toolCalls) {
        const QJsonObject call = tv.toObject();
        QJsonObject toolMsg;
        toolMsg["role"] = "tool";
        toolMsg["tool_call_id"] = call["id"].toString();
        toolMsg["content"] = QString::fromStdString(exec(call));
        next.append(toolMsg);
    }
    return next;
}

QJsonArray buildToolCallsFromDeltas(const std::vector<QJsonObject>& deltas) {
    // 按 index 聚合流式 tool_calls 增量：id/type/name 只取首个非空值，arguments 串接。
    struct Acc {
        int index = 0;
        QString id;
        QString type;
        QString name;
        QString arguments;
    };
    std::map<int, Acc> acc;
    for (const QJsonObject& d : deltas) {
        const int index = d["index"].toInt(-1);
        if (index < 0) continue;
        Acc& a = acc[index];
        a.index = index;
        if (!d["id"].toString().isEmpty()) a.id = d["id"].toString();
        if (!d["type"].toString().isEmpty()) a.type = d["type"].toString();
        const QJsonObject fn = d["function"].toObject();
        if (!fn["name"].toString().isEmpty()) a.name = fn["name"].toString();
        a.arguments += fn["arguments"].toString();
    }

    QJsonArray out;
    for (const auto& [index, a] : acc) {
        QJsonObject fn;
        fn["name"] = a.name;
        fn["arguments"] = a.arguments;
        QJsonObject call;
        call["index"] = index;
        call["id"] = a.id;
        call["type"] = a.type.isEmpty() ? QStringLiteral("function") : a.type;
        call["function"] = fn;
        out.append(call);
    }
    return out;
}

TurnRoute routeTurn(const QString& strong, const QString& router, int depth,
                    bool has_tools, bool before_deadline) {
    TurnRoute r;
    const bool routing = !router.isEmpty() && has_tools;
    if (!routing) {
        // 无轻模型或本局面无可调工具：单模型（强模型），声明工具直到 depth 上限。
        r.model = strong;
        r.declare_tools = has_tools && depth < 4 && before_deadline;
        return r;
    }
    if (depth == 0) {
        r.model = router;         // 轻模型挑工具
        r.declare_tools = true;   // 声明工具供其选择
    } else {
        r.model = strong;         // 强模型做最终讲解
        r.declare_tools = false;  // 不再声明工具，模型直接给出文本答案
    }
    return r;
}

HttpConversationService::HttpConversationService(const QString& baseUrl, const QString& apiKey,
                                                 const QString& model, QObject* parent)
    : QObject(parent), baseUrl_(baseUrl), apiKey_(apiKey), model_(model) {
    available_ = !baseUrl_.isEmpty() && !model_.isEmpty();
}

HttpConversationService::~HttpConversationService() = default;

void HttpConversationService::answer(const ConversationRequest& req, Callback cb) {
    if (!available_) {
        cb(ConversationReply{});
        return;
    }
    const QuestionClass cls = classifyQuestion(req.question);
    // A3：分档 max_tokens（0 = 按 level 自动；n>0 = 显式覆盖）。
    const int mt = max_tokens_ > 0 ? max_tokens_ : maxTokensForLevel(req.profile.level);

    QJsonObject system;
    system["role"] = "system";
    system["content"] = buildSystemPrompt(skills_, cls, req);
    QJsonObject user;
    user["role"] = "user";
    user["content"] = buildUserPrompt(req, cls);

    // A2：只把「成功的最终回答」写入历史（工具环路中间态 / 失败 / 降级回答不入）。
    // 包装后的回调在工具环路/路由回退中只被最终终点调用一次，故不会重复记历史。
    const QString rawQuestion = QString::fromStdString(req.question);
    auto wrapped = [this, rawQuestion, cb = std::move(cb)](ConversationReply r) mutable {
        if (!r.unsure && !r.text.empty()) {
            session_.push_back({rawQuestion, QString::fromStdString(r.text)});
            compressSession(&session_, &session_summary_, kSessionWindowTurns);
        }
        cb(std::move(r));
    };

    QJsonArray messages = buildConversationMessages(system, session_summary_, session_, user);
    doChat(messages, 0, nowMs() + max_loop_ms_, mt, std::move(wrapped));
}

void HttpConversationService::clearSession() {
    session_.clear();
    session_summary_.clear();
}

bool HttpConversationService::hasHistory() const {
    return !session_.empty() || !session_summary_.isEmpty();
}

void HttpConversationService::doChat(QJsonArray messages, int depth, qint64 deadline_ms,
                                     int max_tokens, Callback cb) {
    QUrl url(baseUrl_);
    if (!url.path().endsWith("/chat/completions")) {
        QString path = url.path();
        if (!path.endsWith('/')) path += '/';
        path += "chat/completions";
        url.setPath(path);
    }

    // D5.3 模型路由 + D1.10 双界：用 routeTurn 决定本回合模型与是否声明工具。
    // 轻模型（router_model_）承担工具选择，强模型（model_）做最终讲解。
    const bool has_tools = (tool_handler_ || !tool_registry_.all().empty());
    const TurnRoute tr = routeTurn(model_, router_model_, depth, has_tools,
                                   nowMs() < deadline_ms);

    QJsonObject body;
    body["model"] = tr.model;
    body["messages"] = messages;
    if (tr.declare_tools) body["tools"] = buildTools(&tool_registry_);
    body["max_tokens"] = max_tokens;  // A3：分档上限（贯穿工具选择与最终回合同 cap）
    body["stream"] = false;

    const std::string bodyJson =
        QJsonDocument(body).toJson(QJsonDocument::Compact).toStdString();
    const std::string auth = apiKey_.isEmpty()
                                 ? std::string{}
                                 : ("Authorization: Bearer " + apiKey_.toStdString());
    const std::string urlStr = url.toString().toStdString();
    const int timeout = timeout_ms_;
    const int gen = ++generation_;

    // L16：WinHTTP 直连（后台线程阻塞执行），结果经 queued 回主线程处理。QPointer
    // 兜底：服务析构后（对局 teardown）在途请求直接丢弃，不触碰已释放的 this。
    QPointer<HttpConversationService> self(this);
    std::thread([self, gen, urlStr, bodyJson, auth, timeout, depth, deadline_ms, max_tokens,
                 messages, cb = std::move(cb)]() mutable {
        const WinHttpResult res = winHttpPost(urlStr, bodyJson, "application/json", auth, timeout);
        if (!self) return;  // 服务已析构，丢弃结果
        QMetaObject::invokeMethod(self, [self, gen, res, depth, deadline_ms, max_tokens,
                                         messages, cb = std::move(cb)]() mutable {
            if (!self || gen != self->generation_) return;  // stale / cancelled / destructed

            ConversationReply r;
            if (!res.ok) {
                r.text = "（语言服务暂时不可用：" + res.error + "）";
                r.unsure = true;
                cb(std::move(r));
                return;
            }
            if (res.status >= 400) {
                r.text = "（语言服务暂时不可用：" +
                         httpErrorDetail(res.status, res.body).toStdString() + "）";
                r.unsure = true;
                cb(std::move(r));
                return;
            }

            const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(res.body));
            const QJsonObject choice = doc.object()["choices"].toArray().first().toObject();
            const QJsonObject msg = choice["message"].toObject();
            const QJsonArray toolCalls = msg["tool_calls"].toArray();
            const QString content = msg["content"].toString();
            const QString finish = choice["finish_reason"].toString();

            const bool has_tools = (self->tool_handler_ || !self->tool_registry_.all().empty());
            if (toolCalls.isEmpty()) {
                // D5.3：轻模型在工具选择回合没挑工具 → 忽略其文本，改由强模型直接作答
                // （强模型在本回合不声明工具，给出最终文本答案）。
                if (!self->router_model_.isEmpty() && has_tools && depth == 0) {
                    self->doChat(messages, depth + 1, deadline_ms, max_tokens, std::move(cb));
                    return;
                }
                // 输出被 max_tokens 截断（finish_reason=length）：以足额上限重试一次。
                if (finish == QLatin1String("length") && max_tokens < kLengthRetryTokens) {
                    self->doChat(messages, depth, deadline_ms, kLengthRetryTokens, std::move(cb));
                    return;
                }
                r.text = content.toStdString();
                cb(std::move(r));
                return;
            }

            // Execute every requested tool call, then feed each result back and ask the
            // model for the final natural-language answer. Every tool_call must be
            // answered by a matching `tool` message (same tool_call_id), or the endpoint
            // rejects the follow-up request.
            const QJsonArray next = buildToolFollowup(
                messages, content, toolCalls,
                [self](const QJsonObject& call) { return self->execToolCall(call); });
            self->doChat(next, depth + 1, deadline_ms, max_tokens, std::move(cb));
        }, Qt::QueuedConnection);
    }).detach();
}

void HttpConversationService::cancel() {
    ++generation_;  // 在途请求的结果回到主线程后被丢弃
}

std::string HttpConversationService::execToolCall(const QJsonObject& call) const {
    const QString fnName = call["function"].toObject()["name"].toString();
    const QJsonValue av = call["function"].toObject()["arguments"];
    QJsonObject args;
    if (av.isString())
        args = QJsonDocument::fromJson(av.toString().toUtf8()).object();
    else if (av.isObject())
        args = av.toObject();

    // D1：先查注入的工具注册表（确定性能力），再回退到 UI 的演示工具。
    if (const Tool* t = tool_registry_.find(fnName.toStdString())) {
        const std::string argsJson =
            QJsonDocument(args).toJson(QJsonDocument::Compact).toStdString();
        const ToolResult tr = t->invoke(argsJson);
        return tr.ok ? tr.json : ("工具执行失败：" + tr.error);
    }
    if (tool_handler_) return tool_handler_(fnName, args);
    return "未知工具";
}

void HttpConversationService::answerStream(const ConversationRequest& req, DeltaCallback cb) {
    if (!available_) {
        cb(StreamEvent{StreamEvent::Kind::Done, {}});
        return;
    }
    const QuestionClass cls = classifyQuestion(req.question);
    const int mt = max_tokens_ > 0 ? max_tokens_ : maxTokensForLevel(req.profile.level);

    QJsonObject system;
    system["role"] = "system";
    system["content"] = buildSystemPrompt(skills_, cls, req);
    QJsonObject user;
    user["role"] = "user";
    user["content"] = buildUserPrompt(req, cls);

    // A2：只把「最终回合的成功回答」写入历史。累积 final 回合的 delta，Abort 时清空
    // （丢弃工具回合 / 轻模型兜底回合的内容），Done 时落历史。
    const QString rawQuestion = QString::fromStdString(req.question);
    auto accum = std::make_shared<std::string>();
    auto wrapped = [this, rawQuestion, accum, cb = std::move(cb)](StreamEvent e) mutable {
        switch (e.kind) {
            case StreamEvent::Kind::Delta: *accum += e.text; break;
            case StreamEvent::Kind::Abort: accum->clear(); break;
            case StreamEvent::Kind::Done:
                if (!accum->empty()) {
                    session_.push_back({rawQuestion, QString::fromStdString(*accum)});
                    compressSession(&session_, &session_summary_, kSessionWindowTurns);
                }
                accum->clear();
                break;
        }
        cb(std::move(e));
    };

    QJsonArray messages = buildConversationMessages(system, session_summary_, session_, user);
    doChatStream(messages, 0, nowMs() + max_loop_ms_, mt, std::move(wrapped));
}

namespace {
// B3 流式一个回合的跨线程累积状态（worker 经 queued 回调回主线程时共享）。
struct StreamRound {
    bool content_streamed = false;        // 本回合是否已向 UI 流式过 content（供 Abort 兜底）
    std::vector<QJsonObject> tool_deltas;  // 本回合累积的 tool_calls 增量
};
}  // namespace

void HttpConversationService::doChatStream(QJsonArray messages, int depth, qint64 deadline_ms,
                                           int max_tokens, DeltaCallback cb) {
    QUrl url(baseUrl_);
    if (!url.path().endsWith("/chat/completions")) {
        QString path = url.path();
        if (!path.endsWith('/')) path += '/';
        path += "chat/completions";
        url.setPath(path);
    }

    const bool has_tools = (tool_handler_ || !tool_registry_.all().empty());
    const TurnRoute tr = routeTurn(model_, router_model_, depth, has_tools,
                                   nowMs() < deadline_ms);

    QJsonObject body;
    body["model"] = tr.model;
    body["messages"] = messages;
    if (tr.declare_tools) body["tools"] = buildTools(&tool_registry_);
    body["max_tokens"] = max_tokens;
    body["stream"] = true;

    const std::string bodyJson =
        QJsonDocument(body).toJson(QJsonDocument::Compact).toStdString();
    const std::string auth =
        apiKey_.isEmpty() ? std::string{} : ("Authorization: Bearer " + apiKey_.toStdString());
    const std::string urlStr = url.toString().toStdString();
    const int timeout = timeout_ms_;
    const int gen = ++generation_;
    const bool declared_tools = tr.declare_tools;

    auto cbPtr = std::make_shared<DeltaCallback>(std::move(cb));
    auto round = std::make_shared<StreamRound>();
    QPointer<HttpConversationService> self(this);

    std::thread([self, gen, urlStr, bodyJson, auth, timeout, depth, deadline_ms, max_tokens,
                 messages = std::move(messages), cbPtr, round, declared_tools]() mutable {
        SseParser parser;
        std::vector<std::string> content_batch;
        std::vector<QJsonObject> tool_batch;
        auto lastFlush = std::chrono::steady_clock::now();

        // 把累积的增量批量回主线程（每 ~40ms 或收尾时），避免逐 delta 跨线程开销。
        auto flush = [&](bool force) {
            if (content_batch.empty() && tool_batch.empty()) return;
            const auto now = std::chrono::steady_clock::now();
            if (!force &&
                std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFlush).count() <
                    40)
                return;
            lastFlush = now;
            auto cd = std::move(content_batch);
            content_batch.clear();
            auto td = std::move(tool_batch);
            tool_batch.clear();
            QMetaObject::invokeMethod(
                self, [self, gen, round, cbPtr, cd = std::move(cd), td = std::move(td)]() mutable {
                    if (!self || gen != self->generation_) return;  // stale / cancelled
                    for (const std::string& d : cd) {
                        round->content_streamed = true;
                        (*cbPtr)(StreamEvent{StreamEvent::Kind::Delta, d});
                    }
                    for (const QJsonObject& t : td) round->tool_deltas.push_back(t);
                },
                Qt::QueuedConnection);
        };

        WinHttpResult res = winHttpPostStream(
            urlStr, bodyJson, "application/json", auth, timeout, [&](const std::string& chunk) {
                parser.feed(chunk);
                auto cd = parser.drainContentDeltas();
                auto td = parser.drainToolCallDeltas();
                content_batch.insert(content_batch.end(), cd.begin(), cd.end());
                tool_batch.insert(tool_batch.end(), td.begin(), td.end());
                flush(false);
            });

        // 收尾：把剩余增量全部回主线程，然后以结束回调结束本回合。
        {
            auto cd = parser.drainContentDeltas();
            auto td = parser.drainToolCallDeltas();
            content_batch.insert(content_batch.end(), cd.begin(), cd.end());
            tool_batch.insert(tool_batch.end(), td.begin(), td.end());
        }
        flush(true);
        const std::string finish_reason = parser.finishReason();

        if (!self) return;
        QMetaObject::invokeMethod(
            self,
            [self, gen, res, depth, deadline_ms, max_tokens, messages = std::move(messages),
             cbPtr, round, declared_tools, finish_reason]() mutable {
                if (!self || gen != self->generation_) return;  // stale / cancelled

                auto fail = [&](const std::string& text) {
                    (*cbPtr)(StreamEvent{StreamEvent::Kind::Delta, text});
                    (*cbPtr)(StreamEvent{StreamEvent::Kind::Done, {}});
                };

                // 传输失败 / 5xx：报错并结束（4xx 走降级，见下）。
                if (!res.ok || res.status == 0 || res.status >= 500) {
                    fail("（语言服务暂时不可用：" + res.error + "）");
                    return;
                }
                if (res.status >= 400) {
                    // B3：已声明 tools 且流式返回 4xx（弱端点不支持 stream+tools）→
                    // 该回合降级为非流式 doChat 重试，其余回合仍流式。
                    if (declared_tools) {
                        self->doChat(messages, depth, deadline_ms, max_tokens,
                                     [cbPtr](const ConversationReply& r) {
                                         (*cbPtr)(StreamEvent{StreamEvent::Kind::Delta, r.text});
                                         (*cbPtr)(StreamEvent{StreamEvent::Kind::Done, {}});
                                     });
                        return;
                    }
                    fail("（语言服务暂时不可用：" +
                         httpErrorDetail(res.status, res.body).toStdString() + "）");
                    return;
                }

                const QJsonArray toolCalls = buildToolCallsFromDeltas(round->tool_deltas);
                const bool round_has_tools = (self->tool_handler_ ||
                                              !self->tool_registry_.all().empty());

                if (!toolCalls.isEmpty()) {
                    // 工具回合：丢弃 content（若已流式则通知 UI abort），执行工具，
                    // 回填后流式进入下一轮。
                    if (round->content_streamed)
                        (*cbPtr)(StreamEvent{StreamEvent::Kind::Abort, {}});
                    const QJsonArray next = buildToolFollowup(
                        messages, QString(), toolCalls,
                        [self](const QJsonObject& call) { return self->execToolCall(call); });
                    self->doChatStream(next, depth + 1, deadline_ms, max_tokens,
                                       std::move(*cbPtr));
                    return;
                }

                // 无工具调用：若为轻模型未挑工具的兜底（router 且 depth 0），丢弃其
                // 文本并交给强模型作答（与 answer() 的非流式逻辑一致）。
                if (!self->router_model_.isEmpty() && round_has_tools && depth == 0) {
                    if (round->content_streamed)
                        (*cbPtr)(StreamEvent{StreamEvent::Kind::Abort, {}});
                    self->doChatStream(messages, depth + 1, deadline_ms, max_tokens,
                                       std::move(*cbPtr));
                    return;
                }

                // 输出被 max_tokens 截断（finish_reason=length）：丢弃已流式的截断文本，
                // 以足额上限重试一次。
                if (finish_reason == "length" && max_tokens < kLengthRetryTokens) {
                    if (round->content_streamed)
                        (*cbPtr)(StreamEvent{StreamEvent::Kind::Abort, {}});
                    self->doChatStream(messages, depth, deadline_ms, kLengthRetryTokens,
                                       std::move(*cbPtr));
                    return;
                }

                // 最终回合：收尾。
                (*cbPtr)(StreamEvent{StreamEvent::Kind::Done, {}});
            },
            Qt::QueuedConnection);
    }).detach();
}

}  // namespace vgo
