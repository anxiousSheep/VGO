#pragma once

#include <QString>

struct KataGoConfig {
    bool enabled = false;
    QString binary;
    QString model;
    QString config;
};

struct LlmConfig {
    bool enabled = false;
    QString baseUrl;
    QString apiKey;
    QString model;
};

struct AppConfig {
    KataGoConfig katago;
    LlmConfig llm;
};

// Loads <dir>/vgo_config.json if present; otherwise returns defaults (all
// disabled -> fallback engine / template conversation).
AppConfig loadAppConfig(const QString& dir);
