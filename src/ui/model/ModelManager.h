#pragma once

#include <QString>

namespace vgo {

struct KataGoSettings {
    bool enabled = true;
    QString binary;      // katago.exe 路径
    QString model;       // 标准模型路径
    QString humanModel;  // 人类风格模型路径（可选）
    QString config;      // gtp.cfg 路径
    QString version;
    QString variant;     // "opencl" | "eigen" | "cuda" | ""
};

struct LlmSettings {
    QString provider;  // 预设 id
    QString baseUrl;
    QString model;
    QString routerModel;  // D5.3 可选「轻模型」（意图/工具选择）；空 = 不路由
    // API key 不在此处（内存/磁盘都经 ModelManager::apiKey 加解密）
};

// Single source of truth for model configuration. Persists to the per-user data
// directory (QStandardPaths::AppDataLocation) as config.json + secrets.json.
// Migrates the legacy vgo_config.json (next to the exe) on first run.
class ModelManager {
public:
    static ModelManager& instance();

    // Test hook: override the data directory (used by unit tests).
    static void setDataDirOverride(const QString& dir);

    void load();  // 幂等：首次调用读取/迁移

    QString dataDir() const;

    const KataGoSettings& katago() const { return katago_; }
    void setKatago(const KataGoSettings&);

    const LlmSettings& llm() const { return llm_; }
    void setLlm(const LlmSettings&);

    QString apiKey() const;          // 解密后返回（仅内存）
    void setApiKey(const QString&);  // DPAPI 加密落盘

    bool onboardingDone() const;
    void setOnboardingDone(bool);

    // 形势分析 / 教学演示强度档位（职业5/7/9段 = 15/17/19，见 ai/strength.h）。
    int analysisLevel() const { return analysisLevel_; }
    void setAnalysisLevel(int level);

    // 状态判断（供 UI 显示）
    bool katagoReady() const;         // binary+model 文件都存在
    QString katagoStatusText() const;
    QString llmStatusText() const;

private:
    ModelManager() = default;
    void ensureLoaded();
    void migrateLegacy();
    void saveConfig();
    void saveSecrets();
    void readConfig();
    void readSecrets();
    QString configPath() const;
    QString secretsPath() const;

    KataGoSettings katago_;
    LlmSettings llm_;
    QString apiKeyCache_;
    bool keyLoaded_ = false;
    bool loaded_ = false;
    bool onboardingDone_ = false;
    int analysisLevel_ = 15;  // 默认「一般」= 职业5段
};

}  // namespace vgo
