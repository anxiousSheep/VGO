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
    src/ui/AppConfig.h
    src/ui/AppConfig.cpp
)
target_include_directories(vgo_app PRIVATE src)
target_link_libraries(vgo_app PRIVATE vgo_core vgo_ai_qt Qt6::Widgets)

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
