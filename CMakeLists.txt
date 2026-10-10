cmake_minimum_required(VERSION 3.16)
project(VGO CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Source files are UTF-8; tell MSVC so Chinese literals decode correctly.
if(MSVC)
    add_compile_options(/utf-8)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# Core engine: rules + board. No Qt, no I/O -- UI-agnostic and unit-testable.
# ---------------------------------------------------------------------------
add_library(vgo_core STATIC
    src/go/board.cpp
    src/go/game.cpp
    src/go/handicap.cpp
)
target_include_directories(vgo_core PUBLIC src/go)

# ---------------------------------------------------------------------------
# AI core: analysis / engine / session logic. UI-agnostic, no Qt.
# ---------------------------------------------------------------------------
add_library(vgo_ai STATIC
    src/ai/analysis.h
    src/ai/strength.h
    src/ai/strength.cpp
    src/ai/game_setup.h
    src/ai/gtp.h
    src/ai/gtp.cpp
    src/ai/engine.h
    src/ai/fallback_engine.h
    src/ai/fallback_engine.cpp
    src/ai/explain.h
    src/ai/explain.cpp
    src/ai/phase.h
    src/ai/phase.cpp
    src/ai/quality_judge.h
    src/ai/quality_judge.cpp
    src/ai/conversation.h
    src/ai/template_conversation.h
    src/ai/template_conversation.cpp
    src/ai/session.h
    src/ai/session.cpp
    src/ai/advisor.h
    src/ai/advisor.cpp
    src/ai/ownership.h
    src/ai/ownership.cpp
    src/ai/llm_presets.h
    src/ai/llm_presets.cpp
    src/ai/skill.h
    src/ai/skill.cpp
    src/ai/skill_eval.h
    src/ai/skill_eval.cpp
    src/ai/harness_eval.h
    src/ai/harness_eval.cpp
    src/ai/variation.h
    src/ai/variation.cpp
    src/ai/record.h
    src/ai/record.cpp
    src/ai/sgf.h
    src/ai/sgf.cpp
    src/ai/evaluate.h
    src/ai/evaluate.cpp
    src/ai/lifedeath.h
    src/ai/lifedeath.cpp
    src/ai/symmetry.h
    src/ai/symmetry.cpp
    src/ai/board_predicates.h
    src/ai/board_predicates.cpp
    src/ai/joseki.h
    src/ai/joseki.cpp
    src/ai/joseki_apply.h
    src/ai/joseki_apply.cpp
    src/ai/lifedeath_shape.h
    src/ai/lifedeath_shape.cpp
    src/ai/lifedeath_verify.h
    src/ai/lifedeath_verify.cpp
    src/ai/probe.h
    src/ai/probe.cpp
    src/ai/tsumego.h
    src/ai/tsumego.cpp
    src/ai/json.h
    src/ai/json.cpp
    src/ai/degradation.h
    src/ai/degradation.cpp
    src/ai/engine_pool.h
    src/ai/engine_pool.cpp
    src/ai/agent/tool.h
    src/ai/agent/tool_registry.h
    src/ai/agent/tool_registry.cpp
    src/ai/agent/agent_tools.h
    src/ai/agent/agent_tools.cpp
    src/ai/agent/loop.h
    src/ai/agent/loop.cpp
    src/ai/agent/trace.h
    src/ai/agent/trace.cpp
    src/ai/verify/claim.h
    src/ai/verify/claim_extractor.h
    src/ai/verify/claim_extractor.cpp
    src/ai/verify/claim_verifier.h
    src/ai/verify/claim_verifier.cpp
    src/ai/verify/bad_sample.h
    src/ai/verify/bad_sample.cpp
    src/ai/verify/leak_gate.h
    src/ai/verify/leak_gate.cpp
    src/ai/learner/learner_model.h
    src/ai/learner/learner_model.cpp
    src/ai/learner/daily_puzzle.h
    src/ai/learner/daily_puzzle.cpp
)
target_include_directories(vgo_ai PUBLIC src)
target_link_libraries(vgo_ai PUBLIC vgo_core)

# ---------------------------------------------------------------------------
# Tests (headless, no Qt).
# ---------------------------------------------------------------------------
enable_testing()
add_executable(vgo_tests
    tests/test_main.cpp
    tests/test_board.cpp
    tests/test_game.cpp
)
target_link_libraries(vgo_tests PRIVATE vgo_core)
add_test(NAME vgo_tests COMMAND vgo_tests)

add_executable(vgo_ai_tests
    tests/test_main.cpp
    tests/test_gtp.cpp
    tests/test_strength.cpp
    tests/test_explain.cpp
    tests/test_phase.cpp
    tests/test_quality_judge.cpp
    tests/test_weakness.cpp
    tests/test_template.cpp
    tests/test_session.cpp
    tests/test_advisor.cpp
    tests/test_ownership.cpp
    tests/test_llm_presets.cpp
    tests/test_skill.cpp
    tests/test_skill_eval.cpp
    tests/test_harness_eval.cpp
    tests/test_variation.cpp
    tests/test_evaluate.cpp
    tests/test_lifedeath.cpp
    tests/test_symmetry.cpp
    tests/test_board_predicates.cpp
    tests/test_joseki_v2.cpp
    tests/test_demo_modes.cpp
    tests/test_lifedeath_shape.cpp
    tests/test_lifedeath_verify.cpp
    tests/test_probe.cpp
    tests/test_tsumego_judge.cpp
    tests/test_tsumego_load.cpp
    tests/test_record.cpp
    tests/test_sgf.cpp
    tests/test_agent.cpp
    tests/test_verify.cpp
    tests/test_leak_gate.cpp
    tests/test_learner.cpp
    tests/test_daily_puzzle.cpp
    tests/test_engine_pool.cpp
    tests/test_handicap.cpp
)
target_link_libraries(vgo_ai_tests PRIVATE vgo_ai)
add_test(NAME vgo_ai_tests COMMAND vgo_ai_tests)

# ---------------------------------------------------------------------------
# Desktop application (Qt Widgets).
# ---------------------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Widgets Network Svg)

# ---------------------------------------------------------------------------
# AI Qt adapters: KataGo GTP client + HTTP conversation service.
# ---------------------------------------------------------------------------
add_library(vgo_ai_qt STATIC
    src/ai/engine_kata/kata_engine.h
    src/ai/engine_kata/kata_engine.cpp
    src/ai/conversation_http/http_conversation.h
    src/ai/conversation_http/http_conversation.cpp
    src/ai/conversation_http/sse_parser.h
    src/ai/conversation_http/sse_parser.cpp
    src/ai/conversation_http/winhttp_transport.h
    src/ai/conversation_http/winhttp_transport.cpp
)
target_include_directories(vgo_ai_qt PUBLIC src)
target_link_libraries(vgo_ai_qt PUBLIC vgo_ai Qt6::Network)
if(WIN32)
    # L16：LLM HTTP 走 WinHTTP 直连，绕开 Qt6Network 的网卡枚举崩溃。
    target_link_libraries(vgo_ai_qt PUBLIC winhttp)
endif()

# ---------------------------------------------------------------------------
# Model provisioning (Qt layer): config/secrets, downloader, GPU detect,
# KataGo installer, LLM presets/probe. Shared by the app and the model tests.
# ---------------------------------------------------------------------------
add_library(vgo_model STATIC
    src/ui/model/KeyStore.h
    src/ui/model/KeyStore.cpp
    src/ui/model/ModelManager.h
    src/ui/model/ModelManager.cpp
    src/ui/model/Downloader.h
    src/ui/model/Downloader.cpp
    src/ui/model/GpuDetect.h
    src/ui/model/GpuDetect.cpp
    src/ui/model/OllamaDetect.h
    src/ui/model/OllamaDetect.cpp
    src/ui/model/ModelsManifest.h
    src/ui/model/ModelsManifest.cpp
    src/ui/model/KataGoProvisioner.h
    src/ui/model/KataGoProvisioner.cpp
    src/ui/model/LearnerStore.h
    src/ui/model/LearnerStore.cpp
    src/ui/model/TraceStore.h
    src/ui/model/TraceStore.cpp
    src/ui/model/BadSampleStore.h
    src/ui/model/BadSampleStore.cpp
)
target_include_directories(vgo_model PUBLIC src)
target_link_libraries(vgo_model PUBLIC vgo_ai Qt6::Core Qt6::Network)
if(WIN32)
    target_link_libraries(vgo_model PUBLIC Crypt32)
endif()

add_executable(vgo_model_tests
    tests/test_main_qt.cpp
    tests/test_keystore.cpp
    tests/test_model_manager.cpp
    tests/test_learner_store.cpp
    tests/test_trace_store.cpp
    tests/test_bad_sample_store.cpp
)
target_link_libraries(vgo_model_tests PRIVATE vgo_model)
add_test(NAME vgo_model_tests COMMAND vgo_model_tests)

# KataGo JSON analysis parsing (Qt layer) — headless with a QCoreApplication.
add_executable(vgo_ai_qt_tests
    tests/test_main_qt.cpp
    tests/test_kata_analyze.cpp
    tests/test_http_tools.cpp
    tests/test_conversation_memory.cpp
    tests/test_sse.cpp
)
target_link_libraries(vgo_ai_qt_tests PRIVATE vgo_ai_qt)
add_test(NAME vgo_ai_qt_tests COMMAND vgo_ai_qt_tests)

# 难度标定 spike（M3a.10）：相邻档位对弈，验证强度单调递增。
add_executable(vgo_calibrate
    tools/calibrate_strength.cpp
)
target_include_directories(vgo_calibrate PRIVATE src)
target_link_libraries(vgo_calibrate PRIVATE vgo_ai_qt Qt6::Core)

# 真实服务集成自检（L13）：KataGo 分析 + LLM 问答 + 校验闸门端到端。
add_executable(vgo_integration_check
    tools/integration_check.cpp
)
target_include_directories(vgo_integration_check PRIVATE src)
target_link_libraries(vgo_integration_check PRIVATE vgo_ai_qt vgo_model Qt6::Core)

# B4 讲解质量评估（离线 judge 工具，手动触发，不进 CTest）。
add_executable(vgo_quality_eval
    tools/quality_eval.cpp
)

# 死活题生成器（C.3，离线，用 L3 常型库摆题，不进 CTest）。
add_executable(vgo_tsumego_gen
    tools/tsumego_gen.cpp
)
target_include_directories(vgo_tsumego_gen PRIVATE src)
target_link_libraries(vgo_tsumego_gen PRIVATE vgo_ai)
target_include_directories(vgo_quality_eval PRIVATE src)
target_link_libraries(vgo_quality_eval PRIVATE vgo_ai_qt vgo_model Qt6::Core)

add_executable(vgo_app WIN32
    src/ui/main.cpp
    src/ui/GobanWidget.h
    src/ui/GobanWidget.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/ChatPanel.h
    src/ui/ChatPanel.cpp
    src/ui/FeedbackBar.h
    src/ui/FeedbackBar.cpp
    src/ui/TracePanel.h
    src/ui/TracePanel.cpp
    src/ui/VariationPanel.h
    src/ui/VariationPanel.cpp
    src/ui/NewGameDialog.h
    src/ui/NewGameDialog.cpp
    src/ui/SettingsDialog.h
    src/ui/SettingsDialog.cpp
    src/ui/AppConfig.h
    src/ui/AppConfig.cpp
)
target_include_directories(vgo_app PRIVATE src)
target_link_libraries(vgo_app PRIVATE vgo_core vgo_ai_qt vgo_model Qt6::Widgets Qt6::Svg)

# ---------------------------------------------------------------------------
# Deploy Qt runtime DLLs next to vgo_app on Windows, so the executable can be
# launched directly (from CLion or Explorer) without editing PATH.
# ---------------------------------------------------------------------------
if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt
                 HINTS "${CMAKE_PREFIX_PATH}"
                 PATH_SUFFIXES bin)
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET vgo_app POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --no-translations
                    "$<IF:$<CONFIG:Debug>,--debug,--release>"
                    "$<TARGET_FILE:vgo_app>"
            COMMENT "Deploying Qt runtime libraries"
            VERBATIM)
    endif()
endif()
