cmake_minimum_required(VERSION 3.16)
project(VGO CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

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

# ---------------------------------------------------------------------------
# Desktop application (Qt Widgets).
# ---------------------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(vgo_app WIN32
    src/ui/main.cpp
    src/ui/GobanWidget.h
    src/ui/GobanWidget.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
)
target_include_directories(vgo_app PRIVATE src)
target_link_libraries(vgo_app PRIVATE vgo_core Qt6::Widgets)

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
