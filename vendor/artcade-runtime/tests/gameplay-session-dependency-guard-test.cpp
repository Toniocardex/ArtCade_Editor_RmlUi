// gameplay-session-dependency-guard-test — RU-02h (see docs/
// RU02_GAMEPLAY_SESSION_REFACTOR.md in the editor repo).
//
// Enforces the RU-02h dependency gate: GameplaySession must never include
// editor-api.h or RmlUi, and must never call BeginDrawing/EndDrawing - it
// stays link-clean of the editor and platform-window layers so editor Play
// (RU-03) can reuse it directly without dragging either in. A plain
// substring scan over the tracked source is enough here: the point is to
// catch a future #include creeping back in, not to parse C++.

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifndef ARTCADE_RUNTIME_ROOT
#error "ARTCADE_RUNTIME_ROOT must be defined by CMake"
#endif

namespace {

int passed = 0;
int failed = 0;

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "FAIL: could not open " << path << "\n";
        ++failed;
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void checkNoForbiddenSubstring(const std::string& path, const std::string& contents,
                               const std::string& forbidden) {
    if (contents.find(forbidden) != std::string::npos) {
        std::cerr << "FAIL: " << path << " contains forbidden substring \""
                  << forbidden << "\"\n";
        ++failed;
    } else {
        ++passed;
    }
}

} // namespace

int main() {
    const std::string root = ARTCADE_RUNTIME_ROOT;
    const std::vector<std::string> files = {
        root + "/src/app/src/gameplay_session.h",
        root + "/src/app/src/gameplay_session.cpp",
    };
    const std::vector<std::string> forbidden = {
        "editor-api.h",
        "RmlUi",
        "Rml/",
        "BeginDrawing",
        "EndDrawing",
    };

    for (const auto& file : files) {
        const std::string contents = readFile(file);
        for (const auto& term : forbidden) {
            checkNoForbiddenSubstring(file, contents, term);
        }
    }

    std::cout << "gameplay-session-dependency-guard-test: " << passed
              << " passed, " << failed << " failed\n";
    return failed > 0 ? 1 : 0;
}
