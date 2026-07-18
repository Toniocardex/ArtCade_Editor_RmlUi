// Shared fixtures and CHECK harness for editor-core-* tests.
#pragma once

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/commands/tileset_commands.h"
#include "editor-native/model/tileset_slicing.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

namespace ArtCade::EditorNative::CoreTest {

extern int g_passed;
extern int g_failed;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) ++::ArtCade::EditorNative::CoreTest::g_passed;               \
        else {                                                                 \
            std::cerr << "FAIL: " #cond " (line " << __LINE__ << ")\n";        \
            ++::ArtCade::EditorNative::CoreTest::g_failed;                     \
        }                                                                      \
    } while (0)

inline constexpr EntityId kHero = 42;
inline const SceneId kSceneA = "scene-a";
inline const SceneId kSceneB = "scene-b";

ProjectDoc makeDoc();
ProjectDoc makeReplacementDoc();
ProjectDoc makeSpriteDoc();
ProjectDoc makeInheritedDoc();
ProjectDoc makeAnimationDoc();
ProjectDoc makeInvalidStartDoc();
ProjectDoc makeMoverDoc();
ProjectDoc makeTopDownDoc(float speed = 100.f);
ProjectDoc makeEmptyDoc();

std::string validProjectJson();
std::string danglingStartJson();
std::string unsupportedVersionJson();
std::string zeroSceneJson();
std::string duplicateSceneJson();

std::filesystem::path testTempDir();
void writeTextFile(const std::filesystem::path& path, const std::string& text);
std::string readTextFile(const std::filesystem::path& path);
bool hasTempSibling(const std::filesystem::path& destination);

void expectCoordinatorBaseline(const EditorCoordinator& c,
                               const std::string& projectName,
                               const SceneId& activeScene,
                               EntityId selection,
                               float leftPanel,
                               std::size_t undoSize,
                               uint64_t revision,
                               uint64_t savedRevision,
                               bool dirty);

void sliceTilesOne(EditorCoordinator& c);
void setUpTilemapForPainting(EditorCoordinator& c);

int reportAndExit(const char* suiteName);

} // namespace ArtCade::EditorNative::CoreTest
