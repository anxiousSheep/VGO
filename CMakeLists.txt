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
    tests/test_template.cpp
    tests/test_session.cpp
    tests/test_advisor.cpp
    tests/test_ownership.cpp
    tests/test_llm_presets.cpp
)
target_link_libraries(vgo_ai_tests PRIVATE vgo_ai)
add_test(NAME vgo_ai_tests COMMAND vgo_ai_tests)

# ---------------------------------------------------------------------------
# Desktop application (Qt Widgets).
# ---------------------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

# ---------------------------------------------------------------------------
# AI Qt adapters: KataGo GTP client + HTTP conversation service.
# ---------------------------------------------------------------------------
add_library(vgo_ai_qt STATIC
    src/ai/engine_kata/kata_engine.h
    src/ai/engine_kata/kata_engine.cpp
    src/ai/conversation_http/http_conversation.h
    src/ai/conversation_http/http_conversation.cpp
)
target_include_directories(vgo_ai_qt PUBLIC src)
target_link_libraries(vgo_ai_qt PUBLIC vgo_ai Qt6::Network)

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
)
target_link_libraries(vgo_model_tests PRIVATE vgo_model)
add_test(NAME vgo_model_tests COMMAND vgo_model_tests)

# KataGo JSON analysis parsing (Qt layer) — headless with a QCoreApplication.
add_executable(vgo_ai_qt_tests
    tests/test_main_qt.cpp
    tests/test_kata_analyze.cpp
)
target_link_libraries(vgo_ai_qt_tests PRIVATE vgo_ai_qt)
add_test(NAME vgo_ai_qt_tests COMMAND vgo_ai_qt_tests)

# 难度标定 spike（M3a.10）：相邻档位对弈，验证强度单调递增。
add_executable(vgo_calibrate
    tools/calibrate_strength.cpp
)
target_include_directories(vgo_calibrate PRIVATE src)
target_link_libraries(vgo_calibrate PRIVATE vgo_ai_qt Qt6::Core)

add_executable(vgo_app WIN32
    src/ui/main.cpp
    src/ui/GobanWidget.h
    src/ui/GobanWidget.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/ChatPanel.h
    src/ui/ChatPanel.cpp
    src/ui/NewGameDialog.h
    src/ui/NewGameDialog.cpp
    src/ui/SettingsDialog.h
    src/ui/SettingsDialog.cpp
    src/ui/AppConfig.h
    src/ui/AppConfig.cpp
)
target_include_directories(vgo_app PRIVATE src)
target_link_libraries(vgo_app PRIVATE vgo_core vgo_ai_qt vgo_model Qt6::Widgets)

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
