#include "../include/app.h"

#include "app_modules.h"

#include "../../modules/game-state/include/splash-state.h"

#include "raylib.h"

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
// raylib also declares CloseWindow / ShowCursor — rename Win32 before include.
#define CloseWindow CloseWindowWin32
#define ShowCursor ShowCursorWin32
#include <windows.h>
#undef CloseWindow
#undef ShowCursor
#endif

namespace ArtCade {
namespace {

// ADR-0019: default ship archive is beside the executable, not the CWD.
// argv[1] may be a .artcade file or a loose project directory.
std::filesystem::path resolveProjectPath(int argc, char* argv[]) {
    if (argc > 1 && argv[1] != nullptr && argv[1][0] != '\0') {
        return std::filesystem::absolute(std::filesystem::u8path(argv[1]));
    }
    return std::filesystem::u8path(GetApplicationDirectory()) / "game.artcade";
}

} // namespace

Application::Application() : mod_(std::make_unique<Modules>()) {}
Application::~Application() {
    shutdownModules();
}

int Application::run(int argc, char* argv[]) {
    const std::filesystem::path projectPath = resolveProjectPath(argc, argv);
    if (!initModules(projectPath.string())) {
        const std::string detail =
            "Initialization failed.\n\nResolved project path:\n"
            + projectPath.u8string()
            + "\n\nIf you opened the game by double-click, run it from a "
              "Command Prompt to see the detailed error on stderr.";
        std::cerr << "[App] Initialization failed. Resolved project path: "
                  << projectPath.u8string() << "\n";
#if defined(_WIN32)
        MessageBoxA(nullptr, detail.c_str(), "ArtCade", MB_OK | MB_ICONERROR);
#endif
        return 1;
    }

    running_ = true;
    mainLoop();
    shutdownModules();
    return 0;
}

} // namespace ArtCade
