// editor-core-test.cpp — architectural guarantees of the native editor core.
//
// Each CHECK maps to a numbered requirement in the refactor prompt (§24).
// Domain suites live in sibling *-test.cpp targets.

#include "editor_core_test_harness.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/app/generated_sfx_generation_preflight.h"
#include "editor-native/app/hierarchy_actions.h"
#include "editor-native/app/input_routing.h"
#include "editor-native/app/inspector_commit.h"
#include "editor-native/app/new_project_transaction.h"
#include "editor-native/app/asset_import.h"
#include "editor-native/app/project_file.h"
#include "editor-native/app/project_load.h"
#include "editor-native/app/project_script_file_service.h"
#include "editor-native/app/script_syntax_validator.h"
#include "editor-native/app/script_asset_workflow.h"
#include "editor-native/app/unsaved_guard.h"
#include "editor-native/app/pending_edit.h"
#include "editor-native/app/inspector_actions.h"
#include "editor-native/commands/box_collider_commands.h"
#include "editor-native/commands/linear_mover_commands.h"
#include "editor-native/commands/auto_destroy_commands.h"
#include "editor-native/commands/camera_target_commands.h"
#include "editor-native/commands/top_down_controller_commands.h"
#include "editor-native/commands/platformer_controller_commands.h"
#include "editor-native/commands/project_commands.h"
#include "editor-native/commands/scene_layer_commands.h"
#include "editor-native/commands/image_asset_commands.h"
#include "editor-native/commands/audio_asset_commands.h"
#include "editor-native/commands/generated_sfx_commands.h"
#include "artcade/sfx/recipe_json.hpp"
#include "editor-native/commands/generated_sfx_macros.h"
#include "editor-native/commands/font_asset_commands.h"
#include "editor-native/commands/script_asset_commands.h"
#include "editor-native/commands/script_attachment_commands.h"
#include "editor-native/commands/tileset_commands.h"
#include "editor-native/commands/tilemap_commands.h"
#include "editor-native/commands/sprite_animation_commands.h"
#include "editor-native/commands/entity_commands.h"
#include "editor-native/commands/scene_commands.h"
#include "editor-native/commands/sprite_presentation_commands.h"
#include "editor-native/model/project_io.h"
#include "editor-native/model/authored_transform.h"
#include "editor-native/model/path_confinement.h"
#include "editor-native/model/play_session.h"
#include "editor-native/model/script_source_stamp.h"
#include "editor-native/model/box_collider_view.h"
#include "editor-native/model/box_collider_geometry.h"
#include "editor-native/model/scene_frame_snapshot.h"
#include "editor-native/model/sprite_animation_slicing.h"
#include "editor-native/model/tileset_slicing.h"
#include "editor-native/model/tilemap_chunk_math.h"
#include "editor-native/model/tilemap_cell_access.h"
#include "editor-native/model/tilemap_stroke_math.h"
#include "editor-native/model/tilemap_region_math.h"
#include "editor-native/model/tilemap_validation.h"
#include "editor-native/model/tilemap_render_view.h"
#include "editor-native/model/sprite_render_view.h"
#include "editor-native/view/scene_grid.h"
#include "editor-native/view/scene_view_camera.h"
#include "logic-core.h"
#include "script-runtime.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include "artcade/sfx/presets.hpp"
#include "artcade/sfx/synthesizer.hpp"

#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

using namespace ArtCade;
using namespace ArtCade::EditorNative;
using namespace ArtCade::EditorNative::CoreTest;

int main() {
    // -- ┬º24.1  A command modifies a single authority --------------------------
    {
        EditorCoordinator c{makeDoc()};
        const SelectionState selectionBefore = c.selection();
        const EditorUiState  uiBefore = c.uiState();

        const auto r = c.execute(SetEntityTransformCommand{kSceneA, kHero, {99.f, 20.f}});
        CHECK(r.ok);
        // Only the document changed; selection and UI state are untouched.
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.x == 99.f);
        CHECK(c.selection().primaryEntity == selectionBefore.primaryEntity);
        CHECK(c.uiState().leftPanelWidth == uiBefore.leftPanelWidth);
        CHECK(c.document().isDirty());
    }

    // -- ┬º24.2 / ┬º24.3  A failed command changes nothing and invalidates nothing
    {
        EditorCoordinator c{makeDoc()};
        const uint64_t revBefore = c.document().revision();
        c.consumeInvalidations(); // clear

        const auto r = c.execute(SetEntityTransformCommand{kSceneA, 9999, {1.f, 2.f}});
        CHECK(!r.ok);
        CHECK(!r.error.empty());
        CHECK(c.document().revision() == revBefore);          // state unchanged
        CHECK(!c.document().isDirty());
        // Only the console error was raised; no Inspector/Viewport invalidation.
        const EditorInvalidation inv = c.consumeInvalidations();
        CHECK(!has(inv, EditorInvalidation::Inspector));
        CHECK(!has(inv, EditorInvalidation::Viewport));
        CHECK(!c.canUndo());                                   // not pushed to undo
    }

    // -- ┬º24.4  SetEntityTransformCommand invalidates only Inspector|Viewport ---
    {
        EditorCoordinator c{makeDoc()};
        c.consumeInvalidations();
        const auto r = c.execute(SetEntityTransformCommand{kSceneA, kHero, {1.f, 1.f}});
        CHECK(r.invalidation ==
              (EditorInvalidation::Inspector | EditorInvalidation::Viewport));
        CHECK(r.change.kind == DomainChangeKind::EntityChanged);
        CHECK(r.change.sceneId == kSceneA);
        CHECK(r.change.entityId == kHero);
        CHECK(!has(r.invalidation, EditorInvalidation::Hierarchy));
        CHECK(!has(r.invalidation, EditorInvalidation::Project));
    }

    // -- ┬º24.5  A selection does not perform a Replace -------------------------
    {
        EditorCoordinator c{makeDoc()};
        const uint32_t replacesBefore = c.document().replaceCount();
        c.apply(SelectEntityIntent{kHero});
        CHECK(c.selection().primaryEntity == kHero);
        CHECK(c.document().replaceCount() == replacesBefore);  // no Replace
        CHECK(!c.document().isDirty());                        // no authoring mutation

        const auto bad = c.apply(SelectEntityIntent{9999});
        CHECK(!bad.ok);
        CHECK(c.selection().primaryEntity == kHero);           // unchanged on failure
    }

    // -- ┬º24.6  A scene change does not serialize / Replace the project --------
    {
        EditorCoordinator c{makeDoc()};
        const uint32_t replacesBefore = c.document().replaceCount();
        const uint64_t revBefore = c.document().revision();

        c.apply(SelectEntityIntent{kHero});
        CHECK(c.selection().primaryEntity == kHero);

        const auto r = c.apply(SelectSceneIntent{kSceneB});
        CHECK(r.ok);
        CHECK(c.state().activeSceneId == kSceneB);
        CHECK(c.selection().primaryEntity == INVALID_ENTITY);
        CHECK(c.uiState().leftPanelWidth == 280.0f);
        CHECK(c.document().startSceneId() == kSceneA);         // project unchanged
        CHECK(c.document().replaceCount() == replacesBefore);  // no Replace
        CHECK(c.document().revision() == revBefore);           // no serialization/mutation
        CHECK(!c.document().isDirty());

        const auto bad = c.apply(SelectSceneIntent{"nope"});
        CHECK(!bad.ok);
        CHECK(c.state().activeSceneId == kSceneB);             // unchanged on failure
        CHECK(c.document().startSceneId() == kSceneA);
    }

    // -- ┬º24.8  Nothing accumulates invalidation without an operation ----------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.consumeInvalidations() == EditorInvalidation::None);
        CHECK(c.pendingInvalidations() == EditorInvalidation::None);
    }

    // -- Replace project is atomic at the coordinator boundary ----------------
    {
        EditorCoordinator c{makeDoc()};
        c.apply(SelectEntityIntent{kHero});
        c.apply(ResizePanelIntent{ResizePanelIntent::Panel::Left, 333.f});
        c.apply(SetHierarchyFilterIntent{"keep-me"});
        c.execute(SetEntityTransformCommand{kSceneA, kHero, {99.f, 20.f}});
        CHECK(c.canUndo());
        c.consumeInvalidations();

        const EditorUiState uiBefore = c.uiState();
        const auto r = c.replaceProject(ProjectDocument{makeReplacementDoc()});

        CHECK(r.ok);
        CHECK(r.change.kind == DomainChangeKind::ProjectReplaced);
        CHECK(r.invalidation == (EditorInvalidation::Hierarchy | EditorInvalidation::Inspector
                                 | EditorInvalidation::Viewport | EditorInvalidation::Assets
                                 | EditorInvalidation::Toolbar | EditorInvalidation::Project
                                 | EditorInvalidation::ScriptEditor
                                 | EditorInvalidation::Layout));
        CHECK(c.document().data().projectName == "replacement");
        CHECK(c.state().activeSceneId == "scene-replacement");
        CHECK(c.selection().primaryEntity == INVALID_ENTITY);
        CHECK(!c.canUndo());
        CHECK(c.undoSize() == 0);
        CHECK(!c.document().isDirty());
        CHECK(c.document().revision() == c.document().savedRevision());
        CHECK(c.uiState().leftPanelWidth == uiBefore.leftPanelWidth);
        CHECK(c.uiState().hierarchyFilter == uiBefore.hierarchyFilter);
    }

    // -- Replace normalizes missing and empty scene focus ----------------------
    {
        EditorCoordinator invalidStart{makeDoc()};
        const auto invalid = invalidStart.replaceProject(ProjectDocument{makeInvalidStartDoc()});
        CHECK(invalid.ok);
        CHECK(invalidStart.document().startSceneId() == "missing-start-scene");
        CHECK(invalidStart.state().activeSceneId == "scene-replacement");
        CHECK(!invalidStart.document().isDirty());

        EditorCoordinator empty{makeDoc()};
        const auto emptyReplace = empty.replaceProject(ProjectDocument{makeEmptyDoc()});
        CHECK(emptyReplace.ok);
        CHECK(empty.state().activeSceneId.empty());
        CHECK(empty.selection().primaryEntity == INVALID_ENTITY);
        CHECK(!empty.document().isDirty());
    }

    // -- New Project: replacing with an empty document yields the clean, blank
    //    lifecycle start the application's New action produces. The guard
    //    (Save/Discard/Cancel) and the Play rejection are exercised separately
    //    (resolveUnsavedGuard tests; replaceProject-during-Play test); the path
    //    clearing and "Untitled" title are application state. Here we pin the
    //    domain transition: dirty edits + history + selection + view state in,
    //    a truly empty, clean, history-less project out.
    {
        EditorCoordinator c{makeDoc()};
        c.apply(SelectEntityIntent{kHero});
        c.apply(SetViewportZoomIntent{kSceneA, 2.0f});   // populate per-scene view state
        c.execute(SetEntityTransformCommand{kSceneA, kHero, {7.f, 8.f}});
        c.execute(SetEntityTransformCommand{kSceneA, kHero, {9.f, 10.f}});
        c.undo();                                   // leave a redo branch too
        CHECK(c.canUndo());
        CHECK(c.canRedo());
        CHECK(c.document().isDirty());
        CHECK(!c.state().sceneViews.empty());
        c.consumeInvalidations();

        const EditorUiState uiBefore = c.uiState();
        const auto r = c.replaceProject(ProjectDocument{ProjectDoc{}});

        CHECK(r.ok);
        CHECK(r.change.kind == DomainChangeKind::ProjectReplaced);
        CHECK(c.document().startSceneId().empty());           // 0 scenes -> empty start
        CHECK(c.document().data().scenes.empty());
        CHECK(c.document().data().entities.empty());
        CHECK(c.document().data().imageAssets.empty());
        CHECK(c.document().data().audioAssets.empty());
        CHECK(c.document().data().fontAssets.empty());
        CHECK(c.state().activeSceneId.empty());
        CHECK(c.state().sceneViews.empty());                  // view state pruned
        CHECK(c.selection().primaryEntity == INVALID_ENTITY); // selection cleared
        CHECK(!c.canUndo());                                  // history cleared
        CHECK(!c.canRedo());
        CHECK(c.undoSize() == 0);
        CHECK(c.redoSize() == 0);
        CHECK(!c.document().isDirty());                       // clean (no destination yet)
        CHECK(c.document().revision() == c.document().savedRevision());
        CHECK(!c.isPlaying());                                // no session carried over
        CHECK(c.uiState().leftPanelWidth == uiBefore.leftPanelWidth);  // UI prefs kept
    }

    // -- Console copy: clipboard formatting + safe indexed lookup -------------
    //    The selection itself is local panel state (not exercised here); these
    //    pin the editor-core pieces the copy entry point relies on.
    {
        CHECK(formatConsoleMessageForClipboard(
                  ConsoleMessage{ConsoleMessage::Level::Info, "ready"}) == "[Info] ready");
        CHECK(formatConsoleMessageForClipboard(
                  ConsoleMessage{ConsoleMessage::Level::Warning, "watch out"})
              == "[Warning] watch out");
        // The full, unabbreviated model text is copied (UI truncation must not leak in).
        const std::string longText =
            "Open failed: invalid TopDownController speed: -20 in scene-a/entity-7";
        CHECK(formatConsoleMessageForClipboard(
                  ConsoleMessage{ConsoleMessage::Level::Error, longText})
              == "[Error] " + longText);

        EditorCoordinator c{makeDoc()};
        CHECK(c.consoleMessage(std::nullopt) == nullptr);  // nothing selected
        CHECK(c.consoleMessage(0) == nullptr);             // empty log
        c.logInfo("first");
        c.logError("second");
        CHECK(c.consoleMessage(0) != nullptr);
        CHECK(c.consoleMessage(0)->text == "first");
        CHECK(c.consoleMessage(1)->level == ConsoleMessage::Level::Error);
        CHECK(c.consoleMessage(2) == nullptr);             // out of range -> safe
    }

    // -- Load from text mutates only after deserialize/migrate/validate --------
    {
        EditorCoordinator c{makeDoc()};
        c.apply(SelectEntityIntent{kHero});
        c.apply(ResizePanelIntent{ResizePanelIntent::Panel::Left, 345.f});
        c.execute(SetEntityTransformCommand{kSceneA, kHero, {44.f, 55.f}});
        c.consumeInvalidations();

        const uint64_t revisionBefore = c.document().revision();
        const uint64_t savedBefore = c.document().savedRevision();
        const bool dirtyBefore = c.document().isDirty();
        const std::size_t undoBefore = c.undoSize();

        const auto malformed = loadProjectFromText(c, "{ not json");
        CHECK(!malformed.ok);
        CHECK(malformed.error.stage == ProjectLoadStage::Deserialize);
        expectCoordinatorBaseline(c, "spike", kSceneA, kHero, 345.f,
                                  undoBefore, revisionBefore, savedBefore, dirtyBefore);

        const auto migrationFailed = loadProjectFromText(c, unsupportedVersionJson());
        CHECK(!migrationFailed.ok);
        CHECK(migrationFailed.error.stage == ProjectLoadStage::Migration);
        expectCoordinatorBaseline(c, "spike", kSceneA, kHero, 345.f,
                                  undoBefore, revisionBefore, savedBefore, dirtyBefore);

        const auto validationFailed = loadProjectFromText(c, danglingStartJson());
        CHECK(!validationFailed.ok);
        CHECK(validationFailed.error.stage == ProjectLoadStage::Validation);
        expectCoordinatorBaseline(c, "spike", kSceneA, kHero, 345.f,
                                  undoBefore, revisionBefore, savedBefore, dirtyBefore);

        const auto duplicateScene = loadProjectFromText(c, duplicateSceneJson());
        CHECK(!duplicateScene.ok);
        CHECK(duplicateScene.error.stage == ProjectLoadStage::Validation);
        expectCoordinatorBaseline(c, "spike", kSceneA, kHero, 345.f,
                                  undoBefore, revisionBefore, savedBefore, dirtyBefore);

        const auto loaded = loadProjectFromText(c, validProjectJson());
        CHECK(loaded.ok);
        CHECK(loaded.operation.change.kind == DomainChangeKind::ProjectReplaced);
        CHECK(c.document().data().projectName == "LoadedProject");
        CHECK(c.document().startSceneId() == "loaded-scene");
        CHECK(c.state().activeSceneId == "loaded-scene");
        CHECK(c.selection().primaryEntity == INVALID_ENTITY);
        CHECK(c.uiState().leftPanelWidth == 345.f);
        CHECK(c.undoSize() == 0);
        CHECK(!c.document().isDirty());
        CHECK(c.document().revision() == c.document().savedRevision());
        CHECK(c.document().findInstanceInScene("loaded-scene", 88) != nullptr);
    }

    // -- Empty projects load without inventing a scene -------------------------
    {
        EditorCoordinator c{makeDoc()};
        const auto loaded = loadProjectFromText(c, zeroSceneJson());
        CHECK(loaded.ok);
        CHECK(c.document().data().scenes.empty());
        CHECK(c.state().activeSceneId.empty());
        CHECK(c.selection().primaryEntity == INVALID_ENTITY);
        CHECK(!c.document().isDirty());
    }

    // -- P0-03: malformed present fields never fall back or escape as throws --
    {
        const std::string formatPrefix = R"({"formatVersion":)";
        for (const std::string& badValue : {std::string{"\"2\""}, std::string{"null"},
                                            std::string{"[]"}, std::string{"{}"}}) {
            const DeserializeResult result =
                ProjectSerializer::deserialize(formatPrefix + badValue + "}");
            CHECK(!result.ok);
            CHECK(result.error.find("formatVersion") != std::string::npos);
        }

        const std::string instancePrefix =
            R"({"activeSceneId":"s","scenes":[{"id":"s","instances":[{"id":1,"objectTypeId":"T","transform":)";
        for (const std::string& badTransform : {std::string{"7"}, std::string{"false"},
                                                std::string{"\"bad\""}, std::string{"[]"}}) {
            const DeserializeResult result = ProjectSerializer::deserialize(
                instancePrefix + badTransform + "}]}]}");
            CHECK(!result.ok);
            CHECK(result.error.find("transform") != std::string::npos);
        }

        CHECK(!ProjectSerializer::deserialize(R"({"scenes":[42]})").ok);
        CHECK(!ProjectSerializer::deserialize(R"({"objectTypes":[null]})").ok);
        CHECK(!ProjectSerializer::deserialize(R"({"imageAssets":{}})").ok);
        CHECK(!ProjectSerializer::deserialize(
            R"({"activeSceneId":"s","scenes":[{"id":"s","instances":"bad"}]})").ok);

        const DeserializeResult hugeFloat =
            ProjectSerializer::deserialize(R"({"targetFPS":1e100})");
        CHECK(!hugeFloat.ok);
        CHECK(hugeFloat.error.find("out of range") != std::string::npos);
        CHECK(!ProjectSerializer::deserialize(R"({"formatVersion":2147483648})").ok);
        CHECK(!ProjectSerializer::deserialize(R"({"schemaVersion":"2"})").ok);
        CHECK(!ProjectSerializer::deserialize(
            R"({"activeSceneId":"s","scenes":[{"id":"s","instances":[{"id":4294967296,"objectTypeId":"T"}]}]})").ok);

        CHECK(!ProjectSerializer::deserialize("{\"scenes\":[").ok);   // truncated
        CHECK(!ProjectSerializer::deserialize("{ not json").ok);       // malformed

        const DeserializeResult unicode = ProjectSerializer::deserialize(
            R"({"projectName":"Progetto \u00c8","scenes":[]})");
        CHECK(unicode.ok);
        CHECK(unicode.value.data().projectName == std::string("Progetto \xC3\x88"));

        // Truly absent optional fields retain schema defaults; this is distinct
        // from the malformed-present cases above.
        const DeserializeResult minimal =
            ProjectSerializer::deserialize(R"({"projectName":"Minimal"})");
        CHECK(minimal.ok);
        CHECK(minimal.value.data().targetFPS == 60.f);
        CHECK(minimal.value.data().scenes.empty());
    }

    // -- P0-03: every deserialize failure leaves the live coordinator intact --
    {
        EditorCoordinator c{makeDoc()};
        c.apply(SelectEntityIntent{kHero});
        c.apply(ResizePanelIntent{ResizePanelIntent::Panel::Left, 345.f});
        CHECK(c.execute(SetEntityTransformCommand{kSceneA, kHero, {44.f, 55.f}}).ok);
        const uint64_t revisionBefore = c.document().revision();
        const uint64_t savedBefore = c.document().savedRevision();
        const bool dirtyBefore = c.document().isDirty();
        const std::size_t undoBefore = c.undoSize();

        for (const std::string& hostile : {
                 std::string{R"({"formatVersion":null})"},
                 std::string{R"({"targetFPS":1e100})"},
                 std::string{R"({"scenes":[{"id":"s","instances":[[]]}]})"}}) {
            const ProjectLoadResult result = loadProjectFromText(c, hostile);
            CHECK(!result.ok);
            CHECK(result.error.stage == ProjectLoadStage::Deserialize);
            expectCoordinatorBaseline(c, "spike", kSceneA, kHero, 345.f,
                                      undoBefore, revisionBefore, savedBefore, dirtyBefore);
        }
    }

    // -- Serializer round-trip keeps authoring data, not workspace state ------
    {
        EditorCoordinator c{makeDoc()};
        c.apply(SelectEntityIntent{kHero});
        c.apply(SetActiveToolIntent{EditorTool::Pan});
        c.apply(ResizePanelIntent{ResizePanelIntent::Panel::Left, 410.f});
        CHECK(c.execute(SetEntityTransformCommand{kSceneA, kHero, {321.f, 20.f}}).ok);

        SerializeResult serialized = ProjectSerializer::serialize(c.document());
        CHECK(serialized.ok);
        CHECK(serialized.value.find("selection") == std::string::npos);
        CHECK(serialized.value.find("sceneViews") == std::string::npos);
        CHECK(serialized.value.find("activeTool") == std::string::npos);
        CHECK(serialized.value.find("leftPanelWidth") == std::string::npos);
        CHECK(serialized.value.find("consoleVisible") == std::string::npos);

        DeserializeResult deserialized = ProjectSerializer::deserialize(serialized.value);
        CHECK(deserialized.ok);
        DeserializeResult validated =
            ProjectValidator::validate(std::move(deserialized.value));
        CHECK(validated.ok);
        CHECK(validated.value.findInstanceInScene(kSceneA, kHero)->transform.position.x
              == 321.f);
    }

    // -- RU-01: canonical read path (formatVersion == kCurrentSchemaVersion)
    // round-trips a realistic, fully-conformant document. Scene has a real
    // layer (mirrors ProjectDocument::createScene()'s "layer-1" default) so
    // this document actually satisfies validate_current_project_json and
    // takes the new canonical delegation path (ProjectJson::read_* -
    // project_io.cpp's deserializeCanonical) rather than falling back to the
    // legacy parser. Proves parity for globalVariables specifically - the
    // legacy path never read it back at all (RU-01a's writer emits it, but no
    // reader existed) - and confirms logicBoard still round-trips through
    // the new path too.
    {
        ProjectDoc doc;
        doc.projectName = "RU01 Parity";
        doc.activeSceneId = "ru01-scene";

        SceneDef scene;
        scene.id = "ru01-scene";
        scene.name = "RU01 Scene";
        scene.layers.push_back(SceneLayerDef{"layer-1", "Layer 1", false});
        scene.defaultLayerId = "layer-1";
        SceneInstanceDef instance;
        instance.id = 1;
        instance.objectTypeId = "Hero";
        instance.instanceName = "Hero";
        instance.layerId = "layer-1";
        scene.instances.push_back(instance);
        doc.scenes.emplace(scene.id, scene);

        EntityDef hero;
        hero.className = "Hero";
        hero.name = "Hero";
        LogicBoardDef board;
        board.id = "board-hero";
        LogicRuleDef rule;
        rule.id = "on-start-rule";
        rule.name = "On Start Rule";
        // kOnStart/kStateSet ("event.on_start"/"state.set"): the two simplest Logic Board block
        // kinds with no sprite/animation/physics component prerequisite -
        // not exercising Logic Board semantics itself, just picking a
        // trigger+action pair proven to construct a valid board so this
        // round-trip test can focus on JSON parity, not block registry rules.
        rule.trigger = LogicBlockDef{Logic::kOnStart, {}};
        LogicBlockDef setScore{Logic::kStateSet, {}};
        setScore.properties.push_back(
            LogicPropertyDef{"key", LogicValue{LogicVariableReference{"score"}}});
        setScore.properties.push_back(LogicPropertyDef{"value", LogicValue{1.0}});
        rule.actions.push_back(std::move(setScore));
        board.rules.push_back(std::move(rule));
        hero.logicBoard = std::move(board);
        doc.objectTypes.emplace("Hero", hero);

        GameVariableDefinition score;
        score.key = "score";
        score.type = GameVariableDefinition::Type::Number;
        score.initialValue = 42.0;
        score.description = "player score";
        doc.globalVariables.push_back(score);

        const SerializeResult serialized = ProjectSerializer::serialize(ProjectDocument{doc});
        CHECK(serialized.ok);
        CHECK(serialized.value.find("\"formatVersion\": 10") != std::string::npos);

        const DeserializeResult deserialized = ProjectSerializer::deserialize(serialized.value);
        CHECK(deserialized.ok);
        const ProjectDoc& roundTripped = deserialized.value.data();

        CHECK(roundTripped.globalVariables.size() == 1);
        if (roundTripped.globalVariables.size() == 1) {
            CHECK(roundTripped.globalVariables[0].key == "score");
            CHECK(std::holds_alternative<double>(roundTripped.globalVariables[0].initialValue));
            CHECK(std::get<double>(roundTripped.globalVariables[0].initialValue) == 42.0);
        }

        const auto typeIt = roundTripped.objectTypes.find("Hero");
        CHECK(typeIt != roundTripped.objectTypes.end());
        if (typeIt != roundTripped.objectTypes.end()) {
            CHECK(typeIt->second.logicBoard.has_value());
            CHECK(typeIt->second.logicBoard && typeIt->second.logicBoard->rules.size() == 1);
        }
        CHECK(roundTripped.scenes.count("ru01-scene") == 1);
    }

    // -- v3 migration promotes the two legacy sprite records to v10 Sprite ---
    {
        const std::string v3 = R"json({
          "formatVersion": 3,
          "activeSceneId": "z",
          "objectTypes": [{"id":"Hero","name":"Hero"}],
          "scenes": [
            {"id":"z","instances":[
              {"id":1,"objectTypeId":"Hero","instanceName":"Absent"}
            ]},
            {"id":"a","instances":[
              {"id":2,"objectTypeId":"Hero","instanceName":"Default",
               "spriteRenderer":{"imageAssetId":"blue","visible":true},
               "spriteAnimator":{"initialClipId":"idle","autoPlay":true,"playbackSpeed":1}},
              {"id":3,"objectTypeId":"Hero","instanceName":"Delta",
               "spriteRenderer":{"imageAssetId":"green","visible":false},
               "spriteAnimator":{"initialClipId":"run","autoPlay":false,"playbackSpeed":2}}
            ]}
          ]
        })json";

        DeserializeResult decoded = ProjectSerializer::deserialize(v3);
        CHECK(decoded.ok);
        DeserializeResult migrated = ProjectMigration::migrate(std::move(decoded.value));
        CHECK(migrated.ok);
        CHECK(migrated.value.data().formatVersion == 10);

        const EntityDef& type = migrated.value.data().objectTypes.at("Hero");
        CHECK(type.spritePresentation.has_value());
        CHECK(type.spritePresentation
              && std::holds_alternative<SpritePresentationImage>(type.spritePresentation->source));
        CHECK(type.spritePresentation
              && std::get<SpritePresentationImage>(type.spritePresentation->source).imageAssetId == "blue");

        const SceneInstanceDef* inherited = migrated.value.findInstanceInScene("a", 2);
        const SceneInstanceDef* delta = migrated.value.findInstanceInScene("a", 3);
        const SceneInstanceDef* absent = migrated.value.findInstanceInScene("z", 1);
        CHECK(inherited && !inherited->legacySpriteRendererV3
              && !inherited->spritePresentationOverride);
        CHECK(inherited && !inherited->legacySpriteAnimatorV3
              && !inherited->spritePresentationOverride);
        CHECK(delta && delta->spritePresentationOverride);
        CHECK(delta && delta->spritePresentationOverride->visible
              && !*delta->spritePresentationOverride->visible);
        CHECK(delta && delta->spritePresentationOverride->source
              && std::holds_alternative<SpritePresentationAnimation>(
                  *delta->spritePresentationOverride->source));
        CHECK(delta && std::get<SpritePresentationAnimation>(
                  *delta->spritePresentationOverride->source).defaultClipId == "run");
        CHECK(delta && std::get<SpritePresentationAnimation>(
                  *delta->spritePresentationOverride->source).playbackSpeed == 2.f);
        CHECK(absent && absent->spritePresentationOverride
              && absent->spritePresentationOverride->source
              && std::holds_alternative<SpritePresentationNone>(
                  *absent->spritePresentationOverride->source));

        const SerializeResult once = ProjectSerializer::serialize(migrated.value);
        CHECK(once.ok);
        DeserializeResult migratedAgain = ProjectMigration::migrate(
            ProjectDocument{migrated.value.data()});
        CHECK(migratedAgain.ok);
        const SerializeResult twice = ProjectSerializer::serialize(migratedAgain.value);
        CHECK(twice.ok && twice.value == once.value); // migration is idempotent

        const DeserializeResult roundTrip = ProjectSerializer::deserialize(once.value);
        CHECK(roundTrip.ok);
        const SceneInstanceDef* roundTripDelta =
            roundTrip.value.findInstanceInScene("a", 3);
        CHECK(roundTripDelta && !roundTripDelta->legacySpriteRendererV3);
        CHECK(roundTripDelta && !roundTripDelta->legacySpriteAnimatorV3);
        CHECK(roundTripDelta && roundTripDelta->spritePresentationOverride);
    }

    // -- v10 Sprite is one authoring component with exact command history -----
    {
        ProjectDoc doc = makeAnimationDoc();
        EntityDef& hero = doc.objectTypes.at("Hero");
        hero.spritePresentation = SpritePresentationComponent{
            true, SpritePresentationAnimation{"hero.anim", "idle", true, 1.f}};
        hero.spriteRenderer.reset();
        hero.spriteAnimator.reset();
        EditorCoordinator c{std::move(doc)};

        CHECK(c.execute(SetObjectTypeSpritePresentationCommand{
            "Hero", SpritePresentationComponent{
                true, SpritePresentationImage{"img-hero"}}}).ok);
        const EntityDef& imageType = c.document().data().objectTypes.at("Hero");
        CHECK(imageType.spritePresentation
              && std::holds_alternative<SpritePresentationImage>(
                  imageType.spritePresentation->source));
        CHECK(!imageType.spriteRenderer && !imageType.spriteAnimator);
        CHECK(c.undo().ok);
        const EntityDef& restored = c.document().data().objectTypes.at("Hero");
        CHECK(restored.spritePresentation
              && std::holds_alternative<SpritePresentationAnimation>(
                  restored.spritePresentation->source));

        SpritePresentationOverride overrideValue;
        overrideValue.visible = false;
        CHECK(c.execute(SetInstanceSpritePresentationOverrideCommand{
            kSceneA, kHero, overrideValue}).ok);
        const SceneInstanceDef* instance =
            c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(instance && instance->spritePresentationOverride
              && instance->spritePresentationOverride->visible
              && !*instance->spritePresentationOverride->visible);
        CHECK(c.undo().ok);
        CHECK(!c.document().findInstanceInScene(kSceneA, kHero)
                   ->spritePresentationOverride);

        ProjectDoc guardedDoc = makeAnimationDoc();
        EntityDef& guardedHero = guardedDoc.objectTypes.at("Hero");
        guardedHero.spritePresentation = SpritePresentationComponent{
            true, SpritePresentationAnimation{"hero.anim", "idle", true, 1.f}};
        guardedHero.spriteRenderer.reset();
        guardedHero.spriteAnimator.reset();
        LogicRuleDef animationRule;
        animationRule.actions.push_back(LogicBlockDef{"animation.stop", {}});
        LogicBoardDef guardedBoard;
        guardedBoard.rules.push_back(std::move(animationRule));
        guardedHero.logicBoard = std::move(guardedBoard);
        EditorCoordinator guarded{std::move(guardedDoc)};
        const uint64_t revisionBefore = guarded.document().revision();
        const std::size_t undoBefore = guarded.undoSize();
        const EditorOperationResult rejected = guarded.execute(
            SetObjectTypeSpritePresentationCommand{"Hero", SpritePresentationComponent{
                true, SpritePresentationImage{"img-hero"}}});
        CHECK(!rejected.ok);
        CHECK(guarded.document().revision() == revisionBefore);
        CHECK(guarded.undoSize() == undoBefore);
        CHECK(std::holds_alternative<SpritePresentationAnimation>(
            guarded.document().data().objectTypes.at("Hero").spritePresentation->source));
    }

    // -- 2D.0C: type-owned capability + sparse override have exact history ---
    {
        ProjectDoc doc = makeAnimationDoc();
        doc.objectTypes.at("Hero").spriteRenderer.reset();
        doc.objectTypes.at("Hero").spriteAnimator.reset();
        EditorCoordinator c{std::move(doc)};

        CHECK(c.execute(AddSpriteRendererToObjectTypeCommand{"Hero"}).ok);
        CHECK(c.document().data().objectTypes.at("Hero").spriteRenderer.has_value());
        CHECK(!c.document().findInstanceInScene(kSceneA, kHero)->spriteRendererOverride);
        CHECK(c.execute(SetObjectTypeSpriteSourceCommand{
            "Hero", ObjectTypeSpriteSourceKind::Animation, "hero.anim"}).ok);
        const EntityDef& animatedType = c.document().data().objectTypes.at("Hero");
        CHECK(animatedType.spriteAnimator.has_value());
        CHECK(animatedType.spriteAnimator->animationAssetId == "hero.anim");
        CHECK(animatedType.spriteAnimator->defaultClipId == "idle");

        SpriteAnimatorOverride speedDelta;
        speedDelta.playbackSpeed = 1.5f;
        CHECK(c.execute(SetInstanceAnimatorOverrideCommand{
            kSceneA, kHero, speedDelta}).ok);
        const SceneInstanceDef* instance =
            c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(instance && instance->spriteAnimatorOverride);
        CHECK(instance && instance->spriteAnimatorOverride->playbackSpeed
              && *instance->spriteAnimatorOverride->playbackSpeed == 1.5f);
        const ResolvedSpritePresentation resolved = resolveSpritePresentation(
            c.document().data().objectTypes.at("Hero"), *instance);
        CHECK(resolved.animator && resolved.animator->playbackSpeed == 1.5f);
        CHECK(resolved.animatorOrigin == ComponentOrigin::InstanceOverride);

        CHECK(c.undo().ok);
        instance = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(instance && !instance->spriteAnimatorOverride);
        CHECK(c.redo().ok);
        instance = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(instance && instance->spriteAnimatorOverride);

        CHECK(c.execute(ClearInstanceAnimatorOverrideCommand{kSceneA, kHero}).ok);
        CHECK(!c.document().findInstanceInScene(kSceneA, kHero)->spriteAnimatorOverride);
        CHECK(c.undo().ok);
        instance = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(instance && instance->spriteAnimatorOverride
              && instance->spriteAnimatorOverride->playbackSpeed
              && *instance->spriteAnimatorOverride->playbackSpeed == 1.5f);
        CHECK(c.redo().ok);
        CHECK(!c.document().findInstanceInScene(kSceneA, kHero)->spriteAnimatorOverride);

        CHECK(c.execute(RemoveSpriteRendererFromObjectTypeCommand{"Hero"}).ok);
        CHECK(!c.document().data().objectTypes.at("Hero").spriteRenderer);
        CHECK(!c.document().data().objectTypes.at("Hero").spriteAnimator);
        CHECK(c.undo().ok);
        CHECK(c.document().data().objectTypes.at("Hero").spriteRenderer);
        CHECK(c.document().data().objectTypes.at("Hero").spriteAnimator);

        ProjectDoc guardedDoc = c.document().data();
        LogicBoardDef board;
        LogicRuleDef rule;
        rule.id = "animation-rule";
        rule.actions.push_back(LogicBlockDef{"animation.play_clip", {}});
        rule.actions.push_back(LogicBlockDef{"animation.stop", {}});
        board.rules.push_back(std::move(rule));
        guardedDoc.objectTypes.at("Hero").logicBoard = std::move(board);
        EditorCoordinator guarded{std::move(guardedDoc)};
        const EditorOperationResult rejected = guarded.execute(
            RemoveSpriteAnimatorFromObjectTypeCommand{"Hero"});
        CHECK(!rejected.ok);
        CHECK(rejected.error.find("contains 2 actions") != std::string::npos);
        CHECK(guarded.document().data().objectTypes.at("Hero").spriteAnimator);

        // Changing the source to Image/None removes that same capability, so
        // it must honour the identical Logic dependency guard and leave the
        // document/history untouched on failure.
        const uint64_t guardedRevision = guarded.document().revision();
        const std::size_t guardedUndoSize = guarded.undoSize();
        const EditorOperationResult sourceRejected = guarded.execute(
            SetObjectTypeSpriteSourceCommand{
                "Hero", ObjectTypeSpriteSourceKind::Image, "img-hero"});
        CHECK(!sourceRejected.ok);
        CHECK(sourceRejected.error.find("contains 2 actions") != std::string::npos);
        CHECK(guarded.document().revision() == guardedRevision);
        CHECK(guarded.undoSize() == guardedUndoSize);
        CHECK(guarded.document().data().objectTypes.at("Hero").spriteAnimator);
        CHECK(guarded.document().data().objectTypes.at("Hero").spriteRenderer
              ->imageAssetId.empty());

        const EditorOperationResult clearRejected = guarded.execute(
            SetObjectTypeSpriteSourceCommand{
                "Hero", ObjectTypeSpriteSourceKind::None, {}});
        CHECK(!clearRejected.ok);
        CHECK(guarded.document().revision() == guardedRevision);
        CHECK(guarded.undoSize() == guardedUndoSize);
    }

    // -- Sprite Animator exposure: preflight chooses a valid default ----------
    {
        ProjectDoc data = makeAnimationDoc();
        data.objectTypes.at("Hero").spriteAnimator.reset();
        EditorCoordinator c{std::move(data)};
        CHECK(c.apply(SelectEntityIntent{kHero}).ok);
        CHECK(addSpriteAnimator(c).ok);
        const SpriteAnimatorComponent* animator =
            c.document().data().objectTypes.at("Hero").spriteAnimator
                ? &*c.document().data().objectTypes.at("Hero").spriteAnimator
                : nullptr;
        CHECK(animator != nullptr);
        CHECK(animator->animationAssetId == "hero.anim");
        CHECK(animator->defaultClipId == "idle");
        CHECK(c.undo().ok);
        CHECK(!c.document().data().objectTypes.at("Hero").spriteAnimator);

        EditorCoordinator noAnimation{makeInheritedDoc()};
        CHECK(noAnimation.apply(SelectEntityIntent{kHero}).ok);
        const uint64_t revision = noAnimation.document().revision();
        CHECK(!addSpriteAnimator(noAnimation).ok);
        CHECK(noAnimation.document().revision() == revision);
    }

    // -- 2D.0D: orphan/invalid deltas and referenced clips fail in core -------
    {
        ProjectDoc orphan = makeInheritedDoc();
        SpriteAnimatorOverride animatorDelta;
        animatorDelta.playbackSpeed = 1.5f;
        orphan.scenes.at(kSceneA).instances.front().spriteAnimatorOverride = animatorDelta;
        CHECK(!ProjectValidator::validate(ProjectDocument{std::move(orphan)}).ok);

        ProjectDoc invalidSpeed = makeAnimationDoc();
        animatorDelta.playbackSpeed = -1.f;
        invalidSpeed.scenes.at(kSceneA).instances.front().spriteAnimatorOverride = animatorDelta;
        CHECK(!ProjectValidator::validate(ProjectDocument{std::move(invalidSpeed)}).ok);

        EditorCoordinator referencedClip{makeAnimationDoc()};
        const uint64_t revision = referencedClip.document().revision();
        CHECK(!referencedClip.execute(RemoveAnimationClipCommand{"hero.anim", "idle"}).ok);
        CHECK(referencedClip.document().revision() == revision);
        CHECK(referencedClip.document().findSpriteAnimationAsset("hero.anim")->clips.size() == 1);
    }

    // -- 2D.0D: image deletion clears/restores a sparse instance delta exactly -
    {
        EditorCoordinator c{makeInheritedDoc()};
        SpriteRendererOverride delta;
        delta.imageAssetId = "img-alt";
        delta.visible = false;
        CHECK(c.execute(SetInstanceSpriteOverrideCommand{kSceneA, kHero, delta}).ok);
        CHECK(c.execute(RemoveImageAssetCommand{"img-alt"}).ok);
        const SceneInstanceDef* instance = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(instance && instance->spriteRendererOverride
              && instance->spriteRendererOverride->imageAssetId
              && instance->spriteRendererOverride->imageAssetId->empty());
        CHECK(c.undo().ok);
        instance = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(instance && instance->spriteRendererOverride
              && instance->spriteRendererOverride->imageAssetId
              && *instance->spriteRendererOverride->imageAssetId == "img-alt");
        CHECK(instance && instance->spriteRendererOverride->visible
              && !*instance->spriteRendererOverride->visible);
    }

    // -- Project rename is persistent authoring metadata ----------------------
    {
        EditorCoordinator c{makeDoc()};
        c.consumeInvalidations();
        const EditorOperationResult r = c.execute(RenameProjectCommand{"Arcade Quest"});
        CHECK(r.ok);
        CHECK(r.change.kind == DomainChangeKind::ProjectChanged);
        CHECK(r.invalidation == (EditorInvalidation::Inspector
                                 | EditorInvalidation::Toolbar
                                 | EditorInvalidation::Project));
        CHECK(c.document().data().projectName == "Arcade Quest");
        CHECK(c.document().isDirty());
        CHECK(c.undo().ok);
        CHECK(c.document().data().projectName == "spike");
        CHECK(c.redo().ok);
        CHECK(c.document().data().projectName == "Arcade Quest");

        const std::filesystem::path path = testTempDir() / "renamed.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);
        EditorCoordinator reloaded{ProjectDoc{}};
        CHECK(loadProjectFromFile(reloaded, path).ok);
        CHECK(reloaded.document().data().projectName == "Arcade Quest");
    }

    // -- File load adapter: filesystem failure leaves the coordinator intact ---
    {
        const std::filesystem::path dir = testTempDir();
        EditorCoordinator c{makeDoc()};
        c.apply(SelectEntityIntent{kHero});
        c.apply(ResizePanelIntent{ResizePanelIntent::Panel::Left, 360.f});
        CHECK(c.execute(SetEntityTransformCommand{kSceneA, kHero, {44.f, 55.f}}).ok);

        const uint64_t revisionBefore = c.document().revision();
        const uint64_t savedBefore = c.document().savedRevision();
        const bool dirtyBefore = c.document().isDirty();
        const std::size_t undoBefore = c.undoSize();

        const auto missing = loadProjectFromFile(c, dir / "missing.artcade-project");
        CHECK(!missing.ok);
        CHECK(missing.error.stage == ProjectLoadStage::FileRead);
        expectCoordinatorBaseline(c, "spike", kSceneA, kHero, 360.f,
                                  undoBefore, revisionBefore, savedBefore, dirtyBefore);

        const std::filesystem::path emptyFile = dir / "empty.artcade-project";
        writeTextFile(emptyFile, "");
        const auto empty = loadProjectFromFile(c, emptyFile);
        CHECK(!empty.ok);
        CHECK(empty.error.stage == ProjectLoadStage::Deserialize);
        expectCoordinatorBaseline(c, "spike", kSceneA, kHero, 360.f,
                                  undoBefore, revisionBefore, savedBefore, dirtyBefore);

        const std::filesystem::path validFile = dir / "valid.artcade-project";
        writeTextFile(validFile, validProjectJson());
        const auto loaded = loadProjectFromFile(c, validFile);
        CHECK(loaded.ok);
        CHECK(loaded.operation.change.kind == DomainChangeKind::ProjectReplaced);
        CHECK(c.document().data().projectName == "LoadedProject");
        CHECK(c.state().activeSceneId == "loaded-scene");
        CHECK(!c.document().isDirty());
    }

    // -- Atomic save: failure does not mark saved; success reloads from disk ---
    {
        const std::filesystem::path dir = testTempDir();
        const std::filesystem::path projectPath = dir / "project.artcade-project";

        EditorCoordinator c{makeDoc()};
        c.apply(SelectEntityIntent{kHero});
        c.apply(SetViewportZoomIntent{kSceneA, 2.5f});
        c.apply(ResizePanelIntent{ResizePanelIntent::Panel::Left, 390.f});
        CHECK(c.execute(SetEntityTransformCommand{kSceneA, kHero, {777.f, 20.f}}).ok);
        CHECK(c.canUndo());
        CHECK(c.document().isDirty());

        const uint64_t revisionBeforeFailedSave = c.document().revision();
        const uint64_t savedBeforeFailedSave = c.document().savedRevision();
        const std::size_t undoBeforeFailedSave = c.undoSize();
        const std::filesystem::path blockedDestination = dir / "blocked";
        std::filesystem::create_directory(blockedDestination);

        const auto failedSave = saveProjectToFile(c, blockedDestination);
        CHECK(!failedSave.ok);
        CHECK(failedSave.error.stage == ProjectSaveStage::FileWrite);
        CHECK(c.document().revision() == revisionBeforeFailedSave);
        CHECK(c.document().savedRevision() == savedBeforeFailedSave);
        CHECK(c.document().isDirty());
        CHECK(c.undoSize() == undoBeforeFailedSave);
        CHECK(std::filesystem::is_directory(blockedDestination));
        CHECK(!hasTempSibling(blockedDestination));

        const auto saved = saveProjectToFile(c, projectPath);
        CHECK(saved.ok);
        CHECK(saved.operation.change.kind == DomainChangeKind::None);
        CHECK(saved.operation.invalidation == EditorInvalidation::Toolbar);
        CHECK(!c.document().isDirty());
        CHECK(c.document().revision() == revisionBeforeFailedSave);
        CHECK(c.document().savedRevision() == c.document().revision());
        CHECK(c.undoSize() == undoBeforeFailedSave);
        CHECK(c.canUndo());

        const std::string bytes = readTextFile(projectPath);
        CHECK(bytes.find("activeSceneId") != std::string::npos); // persisted start scene
        CHECK(bytes.find("selection") == std::string::npos);
        CHECK(bytes.find("sceneViews") == std::string::npos);
        CHECK(bytes.find("activeTool") == std::string::npos);
        CHECK(bytes.find("splitter") == std::string::npos);
        CHECK(bytes.find("hierarchyFilter") == std::string::npos);
        CHECK(bytes.find("consoleVisible") == std::string::npos);
        CHECK(bytes.find("PlaySession") == std::string::npos);
        CHECK(bytes.find("Rml") == std::string::npos);

        EditorCoordinator sameCoordinator{makeReplacementDoc()};
        sameCoordinator.apply(ResizePanelIntent{ResizePanelIntent::Panel::Left, 444.f});
        sameCoordinator.apply(SelectEntityIntent{77});
        CHECK(sameCoordinator.execute(SetEntityTransformCommand{
            "scene-replacement", 77, {1.f, 2.f}}).ok);
        CHECK(sameCoordinator.canUndo());

        const auto reloadedSame = loadProjectFromFile(sameCoordinator, projectPath);
        CHECK(reloadedSame.ok);
        CHECK(sameCoordinator.document().findInstanceInScene(kSceneA, kHero)
                  ->transform.position.x == 777.f);
        CHECK(sameCoordinator.uiState().leftPanelWidth == 444.f);
        CHECK(sameCoordinator.selection().primaryEntity == INVALID_ENTITY);
        CHECK(sameCoordinator.undoSize() == 0);
        CHECK(!sameCoordinator.canUndo());
        CHECK(!sameCoordinator.document().isDirty());

        EditorCoordinator fresh{makeReplacementDoc()};
        const auto reloadedFresh = loadProjectFromFile(fresh, projectPath);
        CHECK(reloadedFresh.ok);
        CHECK(fresh.document().findInstanceInScene(kSceneA, kHero)
                  ->transform.position.x == 777.f);
        CHECK(fresh.uiState().leftPanelWidth == 280.f);
        CHECK(fresh.selection().primaryEntity == INVALID_ENTITY);
        CHECK(fresh.state().activeTool == EditorTool::Select);
        CHECK(!fresh.document().isDirty());
    }

    // -- P0-05: New Project is prepare/save/commit, with exact rollback -------
    {
        const std::filesystem::path base = testTempDir();
        EditorCoordinator c{makeDoc()};
        c.apply(SelectEntityIntent{kHero});
        c.apply(ResizePanelIntent{ResizePanelIntent::Panel::Left, 345.f});
        CHECK(c.execute(SetEntityTransformCommand{kSceneA, kHero, {44.f, 55.f}}).ok);
        const uint64_t revisionBefore = c.document().revision();
        const uint64_t savedBefore = c.document().savedRevision();
        const bool dirtyBefore = c.document().isDirty();
        const std::size_t undoBefore = c.undoSize();

        const auto freshCandidate = [] {
            ProjectDoc doc;
            doc.projectName = "Fresh";
            return ProjectDocument{std::move(doc)};
        };
        const auto expectUnchanged = [&] {
            expectCoordinatorBaseline(c, "spike", kSceneA, kHero, 345.f,
                                      undoBefore, revisionBefore, savedBefore, dirtyBefore);
        };

        const NewProjectResult cancelled = createNewProjectTransaction(
            c, freshCandidate(), std::nullopt);
        CHECK(!cancelled.ok);
        CHECK(cancelled.cancelled);
        CHECK(cancelled.error.stage == NewProjectStage::Cancelled);
        CHECK(cancelled.destination.empty());
        expectUnchanged();

        const std::filesystem::path blocker = base / "blocker";
        writeTextFile(blocker, "not a directory");
        const NewProjectResult directoryFailed = createNewProjectTransaction(
            c, freshCandidate(), blocker / "nested" / "Fresh.artcade-project");
        CHECK(!directoryFailed.ok);
        CHECK(directoryFailed.error.stage == NewProjectStage::DirectoryCreate);
        expectUnchanged();

        const NewProjectResult validationFailed = createNewProjectTransaction(
            c, ProjectDocument{makeInvalidStartDoc()},
            base / "validation" / "Fresh.artcade-project");
        CHECK(!validationFailed.ok);
        CHECK(validationFailed.error.stage == NewProjectStage::Validation);
        CHECK(!std::filesystem::exists(base / "validation"));
        expectUnchanged();

        NewProjectTransactionHooks serializeHooks;
        serializeHooks.saveCandidate = [](
            const ProjectDocument&, const std::filesystem::path& destination) {
            return ProjectSaveResult::failure(
                ProjectSaveStage::Serialize, destination, "forced serialization failure");
        };
        const NewProjectResult serializeFailed = createNewProjectTransaction(
            c, freshCandidate(), base / "serialize" / "Fresh.artcade-project",
            std::move(serializeHooks));
        CHECK(!serializeFailed.ok);
        CHECK(serializeFailed.error.stage == NewProjectStage::Serialize);
        CHECK(!std::filesystem::exists(base / "serialize"));
        expectUnchanged();

        NewProjectTransactionHooks writeHooks;
        writeHooks.saveCandidate = [](
            const ProjectDocument&, const std::filesystem::path& destination) {
            const ProjectTextFileResult partial =
                writeProjectTextFileAtomically(destination, "partial");
            if (!partial.ok) {
                return ProjectSaveResult::failure(
                    ProjectSaveStage::FileWrite, destination, partial.error.message);
            }
            return ProjectSaveResult::failure(
                ProjectSaveStage::FileWrite, destination, "forced temporary write failure");
        };
        const std::filesystem::path writeDestination =
            base / "write" / "Fresh.artcade-project";
        const NewProjectResult writeFailed = createNewProjectTransaction(
            c, freshCandidate(), writeDestination, std::move(writeHooks));
        CHECK(!writeFailed.ok);
        CHECK(writeFailed.error.stage == NewProjectStage::FileWrite);
        CHECK(!std::filesystem::exists(writeDestination));
        CHECK(!std::filesystem::exists(base / "write"));
        expectUnchanged();

        const std::filesystem::path replaceBlocked = base / "replace-target.artcade-project";
        std::filesystem::create_directory(replaceBlocked);
        const NewProjectResult atomicReplaceFailed = createNewProjectTransaction(
            c, freshCandidate(), replaceBlocked);
        CHECK(!atomicReplaceFailed.ok);
        CHECK(atomicReplaceFailed.error.stage == NewProjectStage::FileWrite);
        CHECK(std::filesystem::is_directory(replaceBlocked));
        CHECK(!hasTempSibling(replaceBlocked));
        expectUnchanged();

        NewProjectTransactionHooks commitHooks;
        commitHooks.commitCandidate = [](
            EditorCoordinator&, ProjectDocument) {
            return EditorOperationResult::failure("forced commit failure");
        };
        const std::filesystem::path uncommittedDestination =
            base / "commit-new" / "Fresh.artcade-project";
        const NewProjectResult commitFailed = createNewProjectTransaction(
            c, freshCandidate(), uncommittedDestination, std::move(commitHooks));
        CHECK(!commitFailed.ok);
        CHECK(commitFailed.error.stage == NewProjectStage::Commit);
        CHECK(!std::filesystem::exists(uncommittedDestination));
        CHECK(!std::filesystem::exists(base / "commit-new"));
        expectUnchanged();

        const std::filesystem::path existingDestination =
            base / "existing.artcade-project";
        writeTextFile(existingDestination, "previous bytes");
        NewProjectTransactionHooks restoreHooks;
        restoreHooks.commitCandidate = [](
            EditorCoordinator&, ProjectDocument) {
            return EditorOperationResult::failure("forced commit failure");
        };
        const NewProjectResult restoreFailed = createNewProjectTransaction(
            c, freshCandidate(), existingDestination, std::move(restoreHooks));
        CHECK(!restoreFailed.ok);
        CHECK(restoreFailed.error.stage == NewProjectStage::Commit);
        CHECK(readTextFile(existingDestination) == "previous bytes");
        expectUnchanged();

        const std::filesystem::path successDestination =
            base / "success" / "Fresh.artcade-project";
        const NewProjectResult created = createNewProjectTransaction(
            c, freshCandidate(), successDestination);
        CHECK(created.ok);
        CHECK(!created.cancelled);
        CHECK(created.destination == std::filesystem::absolute(successDestination));
        CHECK(created.operation.change.kind == DomainChangeKind::ProjectReplaced);
        CHECK(std::filesystem::is_regular_file(successDestination));
        CHECK(c.document().data().projectName == "Fresh");
        CHECK(c.document().data().scenes.empty());
        CHECK(c.document().data().generatedSfx.empty());
        CHECK(c.document().data().audioAssets.empty());
        CHECK(!c.document().isDirty());
        CHECK(c.undoSize() == 0);
        CHECK(!c.selection().hasEntity());
        CHECK(c.state().activeSceneId.empty());
        CHECK(c.uiState().leftPanelWidth == 345.f);

        EditorCoordinator loaded{makeDoc()};
        CHECK(loadProjectFromFile(loaded, successDestination).ok);
        CHECK(loaded.document().data().projectName == "Fresh");
        CHECK(loaded.document().data().generatedSfx.empty());
        CHECK(loaded.document().data().audioAssets.empty());
        CHECK(!loaded.document().isDirty());
    }

    // -- ┬º24.11  Play does not modify the ProjectDocument ----------------------
    // RU-03: PlaySession no longer exposes raw entity introspection/mutation
    // (D-01 - only the render hand-off, buildFrame(), is public), so this only
    // checks the scene the session materialized and that the authoring
    // document/revision stay untouched by materialization or destruction.
    {
        EditorCoordinator c{makeDoc()};
        const uint64_t revBefore = c.document().revision();

        c.apply(SelectSceneIntent{kSceneB});
        std::optional<PlaySession> session = PlaySession::startProject(c.document());
        CHECK(session.has_value());
        CHECK(session->sceneId() == kSceneA);

        std::optional<PlaySession> currentSceneSession =
            PlaySession::startActiveScene(c.document(), c.state().activeSceneId);
        CHECK(currentSceneSession.has_value());
        CHECK(currentSceneSession->sceneId() == kSceneB);

        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.x == 10.f);
        CHECK(c.document().revision() == revBefore);

        // -- ┬º24.12  Stop needs no reload: destroying the session restores
        //            nothing because the document was never changed.
        session.reset();
        CHECK(c.document().findInstanceInScene(kSceneA, kHero) != nullptr);
        CHECK(c.document().revision() == revBefore);
    }

    // -- ┬º24.13  Invalid NumberField parse does not modify the document --------
    {
        EditorCoordinator c{makeDoc()};
        c.apply(SelectEntityIntent{kHero});
        const uint64_t revBefore = c.document().revision();

        const auto bad = commitInspectorTransformField(
            c, kHero, InspectorTransformField::PositionX, "abc");
        CHECK(!bad.ok);
        CHECK(c.document().revision() == revBefore);           // untouched
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.x == 10.f);
        CHECK(parseNumberField("12.5").has_value());
        CHECK(parseNumberField("12.").has_value());
        CHECK(*parseNumberField("12.") == 12.f);
        CHECK(!parseNumberField("-").has_value());
        CHECK(!parseNumberField(".").has_value());
        CHECK(!parseNumberField("1e").has_value());
        CHECK(!parseNumberField("nan").has_value());
        CHECK(!parseNumberField("inf").has_value());
        CHECK(!parseNumberField("12.5xz").has_value());
        CHECK(!parseNumberField("12px").has_value());
        CHECK(!parseNumberField("").has_value());
    }

    // -- ┬º24.17  Splitter applies min/max clamp --------------------------------
    {
        EditorCoordinator c{makeDoc()};
        const SceneId sceneBefore = c.state().activeSceneId;
        const EntityId selectionBefore = c.selection().primaryEntity;
        c.consumeInvalidations();
        const auto resized = c.apply(ResizePanelIntent{ResizePanelIntent::Panel::Left, 5.f});
        CHECK(resized.invalidation == (EditorInvalidation::Layout | EditorInvalidation::Viewport));
        CHECK(c.uiState().leftPanelWidth == PanelLimits::kLeftMin);
        CHECK(c.state().activeSceneId == sceneBefore);
        CHECK(c.selection().primaryEntity == selectionBefore);
        c.apply(ResizePanelIntent{ResizePanelIntent::Panel::Left, 99999.f});
        CHECK(c.uiState().leftPanelWidth == PanelLimits::kLeftMax);
        c.apply(ResizePanelIntent{ResizePanelIntent::Panel::Console, 300.f});
        CHECK(c.uiState().consoleHeight == 300.f);

        c.apply(SelectEntityIntent{kHero});
        c.apply(SetHierarchyFilterIntent{"hero"});
        CHECK(c.uiState().hierarchyFilter == "hero");
        CHECK(c.state().activeSceneId == sceneBefore);
        CHECK(c.selection().primaryEntity == kHero);

        const auto tool = c.apply(SetActiveToolIntent{EditorTool::Pan});
        CHECK(tool.invalidation == (EditorInvalidation::Inspector | EditorInvalidation::Toolbar));
        CHECK(c.state().activeTool == EditorTool::Pan);
        CHECK(!c.uiState().consoleVisible);

        const auto console = c.apply(ToggleConsoleIntent{});
        CHECK(console.invalidation == (EditorInvalidation::Layout | EditorInvalidation::Viewport));
        CHECK(c.uiState().consoleVisible);
        CHECK(c.state().activeTool == EditorTool::Pan);
    }

    // -- Console filter/level intents: workspace-only, Console invalidation ---
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.uiState().consoleShowInfo);
        CHECK(c.uiState().consoleShowWarning);
        CHECK(c.uiState().consoleShowError);

        const auto filter = c.apply(SetConsoleFilterIntent{"missing texture"});
        CHECK(filter.ok);
        CHECK(filter.invalidation == EditorInvalidation::Console);
        CHECK(c.uiState().consoleFilter == "missing texture");

        CHECK(c.apply(SetConsoleShowInfoIntent{false}).ok);
        CHECK(!c.uiState().consoleShowInfo);
        CHECK(c.apply(SetConsoleShowWarningIntent{false}).ok);
        CHECK(!c.uiState().consoleShowWarning);
        const auto errorToggle = c.apply(SetConsoleShowErrorIntent{false});
        CHECK(errorToggle.invalidation == EditorInvalidation::Console);
        CHECK(!c.uiState().consoleShowError);

        // Filters are workspace-only: no revision, no dirty, no undo entry.
        CHECK(c.document().revision() == 0);
        CHECK(!c.document().isDirty());
        CHECK(!c.canUndo());
    }

    // -- clearConsole(): direct mutator like logInfo/Warning/Error, not a
    // Command (console history isn't authoring) or an Intent (nothing in
    // EditorState/EditorUiState changes) ---------------------------------------
    {
        EditorCoordinator c{makeDoc()};
        c.logInfo("one");
        c.logWarning("two");
        c.consumeInvalidations();
        CHECK(c.consoleLog().size() == 2);

        c.clearConsole();
        CHECK(c.consoleLog().empty());
        CHECK(c.consumeInvalidations() == EditorInvalidation::Console);
        CHECK(c.document().revision() == 0);
        CHECK(!c.canUndo());
    }

    // -- Contract: public coordinator access is read-only ----------------------
    {
        EditorCoordinator c{makeDoc()};
        static_assert(std::is_const_v<std::remove_reference_t<decltype(c.document())>>,
                      "EditorCoordinator::document() must be const-only");
        static_assert(std::is_const_v<std::remove_reference_t<decltype(c.state())>>,
                      "EditorCoordinator::state() must be const-only");
        static_assert(std::is_const_v<std::remove_reference_t<decltype(c.uiState())>>,
                      "EditorCoordinator::uiState() must be const-only");
    }

    // -- ┬º24.16  Input captured by a text field never reaches the viewport -----
    {
        CHECK(shouldViewportReceiveInput({/*inRect*/true, false, false, false}));
        CHECK(!shouldViewportReceiveInput({true, false, /*textFocus*/true, false}));
        CHECK(!shouldViewportReceiveInput({true, /*rmlConsumed*/true, false, false}));
        CHECK(!shouldViewportReceiveInput({/*inRect*/false, false, false, false}));
        CHECK(!shouldViewportReceiveInput({true, false, false, /*popup*/true}));
        const GameplayKeyboardInputContext focusedScene{
            /*scene*/true, /*gameplayFocus*/true, /*window*/true,
            /*textFocus*/false, /*popup*/false};
        CHECK(shouldForwardGameplayKeyboardInput(focusedScene));
        // Mouse hover is not keyboard focus: leaving the viewport must not
        // interrupt TopDown, Platformer, or Logic Board keyboard controls.
        CHECK(shouldForwardGameplayKeyboardInput(focusedScene));
        CHECK(!shouldForwardGameplayKeyboardInput({/*logic*/false, true, true, false, false}));
        CHECK(!shouldForwardGameplayKeyboardInput({/*scene*/true, /*noFocus*/false, true, false, false}));
        CHECK(!shouldForwardGameplayKeyboardInput({true, true, true, /*textFocus*/true, false}));
        CHECK(!shouldForwardGameplayKeyboardInput({true, true, /*window*/false, false, false}));
        CHECK(!shouldForwardGameplayKeyboardInput({true, true, true, false, /*popup*/true}));
        CHECK(shouldForwardGameplayPointerInput({focusedScene, /*inRect*/true}));
        CHECK(!shouldForwardGameplayPointerInput({focusedScene, /*outside*/false}));
    }

    // -- Asset filter: live workspace state, narrow invalidation, idempotent --
    {
        EditorCoordinator c{makeDoc()};
        const uint64_t revision = c.document().revision();
        const std::size_t undo = c.undoSize();
        c.consumeInvalidations();

        const auto first = c.apply(SetAssetFilterIntent{"hero image"});
        CHECK(first.ok);
        CHECK(first.invalidation == EditorInvalidation::Assets);
        CHECK(c.uiState().assetFilter == "hero image");
        CHECK(c.consumeInvalidations() == EditorInvalidation::Assets);
        CHECK(c.document().revision() == revision);
        CHECK(!c.document().isDirty());
        CHECK(c.undoSize() == undo);

        const auto duplicate = c.apply(SetAssetFilterIntent{"hero image"});
        CHECK(duplicate.ok);
        CHECK(duplicate.invalidation == EditorInvalidation::None);
        CHECK(c.consumeInvalidations() == EditorInvalidation::None);

        CHECK(c.playProject().ok);
        c.consumeInvalidations();
        const auto duringPlay = c.apply(SetAssetFilterIntent{"audio"});
        CHECK(duringPlay.ok);
        CHECK(duringPlay.invalidation == EditorInvalidation::Assets);
        CHECK(c.uiState().assetFilter == "audio");
        CHECK(c.document().revision() == revision);
        CHECK(!c.document().isDirty());
        CHECK(c.undoSize() == undo);
        CHECK(c.stopPlaying().ok);

        CHECK(c.replaceProject(ProjectDocument{makeReplacementDoc()}).ok);
        CHECK(c.uiState().assetFilter == "audio");
        CHECK(!c.document().isDirty());
        CHECK(!c.canUndo());
    }

    // -- Replace clears project-scoped editor state ----------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        c.apply(SetActiveToolIntent{EditorTool::Brush});
        c.apply(RevealInspectorPropertyIntent{kHero, InspectorProperty::TilemapCellSize});
        CHECK(c.apply(OpenTilesetEditorIntent{"tiles-1"}).ok);
        CHECK(c.state().activeTool == EditorTool::Brush);
        CHECK(c.state().tilesetEditor.openAssetId.has_value());
        CHECK(c.uiState().inspectorRevealRequest.has_value());

        CHECK(c.replaceProject(ProjectDocument{makeReplacementDoc()}).ok);
        CHECK(c.state().activeTool == EditorTool::Select);
        CHECK(!c.state().tilesetEditor.openAssetId.has_value());
        CHECK(!c.takeInspectorRevealRequest().has_value());
    }

    // -- ┬º24.18  Position X path: UI callback ÔåÆ command ÔåÆ document ÔåÆ invalidation
    {
        EditorCoordinator c{makeDoc()};
        c.apply(SelectEntityIntent{kHero});
        c.consumeInvalidations();

        const auto r = commitInspectorTransformField(
            c, kHero, InspectorTransformField::PositionX, "256");
        CHECK(r.ok);
        CHECK(r.change.kind == DomainChangeKind::EntityChanged);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.x == 256.f);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.y == 20.f);
        const EditorInvalidation inv = c.consumeInvalidations();
        CHECK(inv == (EditorInvalidation::Inspector | EditorInvalidation::Viewport
                      | EditorInvalidation::Toolbar));
        // The change is undoable and the inverse is exact.
        CHECK(c.canUndo());
        c.undo();
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.x == 10.f);
    }

    // -- SetEntityTransformCommand: rotation (degrees UI → radians document) ---
    {
        EditorCoordinator c{makeDoc()};
        c.apply(SelectEntityIntent{kHero});
        const uint64_t revBefore = c.document().revision();

        CHECK(commitInspectorTransformField(
            c, kHero, InspectorTransformField::RotationDegrees, "90").ok);
        const SceneInstanceDef* inst = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(inst != nullptr);
        CHECK(std::fabs(inst->transform.rotation - (90.f * kDegToRad)) < 0.0001f);
        CHECK(c.document().revision() == revBefore + 1);

        CHECK(c.undo().ok);
        CHECK(std::fabs(c.document().findInstanceInScene(kSceneA, kHero)->transform.rotation) < 0.0001f);
        CHECK(c.redo().ok);
        CHECK(std::fabs(c.document().findInstanceInScene(kSceneA, kHero)->transform.rotation
                        - (90.f * kDegToRad)) < 0.0001f);
    }

    // -- SetEntityTransformCommand: independent Scale X/Y + validation --------
    {
        EditorCoordinator c{makeDoc()};
        c.apply(SelectEntityIntent{kHero});
        CHECK(commitInspectorTransformField(
            c, kHero, InspectorTransformField::ScaleX, "2").ok);
        CHECK(commitInspectorTransformField(
            c, kHero, InspectorTransformField::ScaleY, "0.5").ok);
        const SceneInstanceDef* inst = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(inst->transform.scale.x == 2.f);
        CHECK(inst->transform.scale.y == 0.5f);

        const uint64_t rev = c.document().revision();
        CHECK(c.execute(SetEntityTransformCommand{
            kSceneA, kHero, AuthoredTransformPatch{std::nullopt, std::nullopt, Vec2{2.f, 0.5f}}}).ok);
        CHECK(c.document().revision() == rev);   // no-op: same values

        CHECK(!c.execute(SetEntityTransformCommand{
            kSceneA, kHero, AuthoredTransformPatch{std::nullopt, std::nullopt, Vec2{0.f, 1.f}}}).ok);
        CHECK(!c.execute(SetEntityTransformCommand{
            kSceneA, kHero, AuthoredTransformPatch{std::nullopt, std::nullopt, Vec2{-1.f, 1.f}}}).ok);
        CHECK(!c.execute(SetEntityTransformCommand{
            kSceneA, kHero,
            AuthoredTransformPatch{std::nullopt, std::nullopt,
                                   Vec2{std::numeric_limits<float>::quiet_NaN(), 1.f}}}).ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.scale.x == 2.f);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.scale.y == 0.5f);
    }

    // -- Transform snapshot: scale without zero-fallback; OBB pick ------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(SetEntityTransformCommand{
            kSceneA, kHero,
            AuthoredTransformPatch{Vec2{100.f, 100.f}, 45.f * kDegToRad, Vec2{2.f, 0.5f}}}).ok);
        const SceneFrameSnapshot frame =
            collectSceneFrameSnapshot(c.document(), kSceneA, kHero);
        CHECK(!frame.entities.empty());
        CHECK(frame.entities[0].bounds.width == 64.f);    // 32 * scale.x
        CHECK(frame.entities[0].bounds.height == 16.f);   // 32 * scale.y
        CHECK(std::fabs(frame.entities[0].rotationRadians - (45.f * kDegToRad)) < 0.0001f);

        // Center is inside the OBB.
        CHECK(pickEntityAt(frame, Vec2{100.f, 100.f}) == kHero);
        // A corner of the axis-aligned AABB of a 45° box that is outside the OBB
        // must miss (AABB of size 64x16 at 45° extends farther on the diagonal).
        const float halfDiag = 0.5f * (64.f + 16.f) / std::sqrt(2.f);
        CHECK(pickEntityAt(frame, Vec2{100.f + halfDiag + 2.f, 100.f}) == INVALID_ENTITY);
    }

    // -- Transform save/reload preserves position, rotation, scale ------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(SetEntityTransformCommand{
            kSceneA, kHero,
            AuthoredTransformPatch{Vec2{12.5f, -3.f}, 30.f * kDegToRad, Vec2{1.5f, 2.25f}}}).ok);
        const auto saved = ProjectSerializer::serialize(c.document());
        CHECK(saved.ok);
        const auto loaded = ProjectSerializer::deserialize(saved.value);
        CHECK(loaded.ok);
        const SceneInstanceDef* inst =
            loaded.value.findInstanceInScene(kSceneA, kHero);
        CHECK(inst != nullptr);
        CHECK(inst->transform.position.x == 12.5f);
        CHECK(inst->transform.position.y == -3.f);
        CHECK(std::fabs(inst->transform.rotation - (30.f * kDegToRad)) < 0.0001f);
        CHECK(inst->transform.scale.x == 1.5f);
        CHECK(inst->transform.scale.y == 2.25f);
    }

    // -- Bring Into Scene: explicit recovery via SetEntityTransformCommand ------
    {
        EditorCoordinator c{makeInheritedDoc()};
        c.apply(SelectEntityIntent{kHero});
        CHECK(c.execute(SetEntityTransformCommand{kSceneA, kHero, Vec2{562.f, -35.f}}).ok);
        const std::size_t undoBefore = c.undoSize();

        const auto r = bringSelectedEntityIntoScene(c);
        CHECK(r.ok);
        CHECK(c.undoSize() == undoBefore + 1);
        const SceneInstanceDef* inst = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(inst != nullptr);
        CHECK(inst->transform.position.x == 496.f);   // 512 - half the 32 wu placeholder
        CHECK(inst->transform.position.y == 16.f);

        CHECK(c.undo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.x == 562.f);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.y == -35.f);
        CHECK(c.redo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.x == 496.f);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.y == 16.f);
    }

    // -- Bring Into Scene no-ops when already inside; Play still blocks edits --
    {
        EditorCoordinator inside{makeInheritedDoc()};
        inside.apply(SelectEntityIntent{kHero});
        CHECK(inside.execute(SetEntityTransformCommand{kSceneA, kHero, Vec2{100.f, 100.f}}).ok);
        const std::size_t undoBefore = inside.undoSize();
        CHECK(bringSelectedEntityIntoScene(inside).ok);
        CHECK(inside.undoSize() == undoBefore);

        EditorCoordinator playing{makeInheritedDoc()};
        playing.apply(SelectEntityIntent{kHero});
        CHECK(playing.execute(SetEntityTransformCommand{kSceneA, kHero, Vec2{562.f, -35.f}}).ok);
        CHECK(playing.playProject().ok);
        CHECK(!bringSelectedEntityIntoScene(playing).ok);
        CHECK(playing.document().findInstanceInScene(kSceneA, kHero)->transform.position.x == 562.f);
        CHECK(playing.document().findInstanceInScene(kSceneA, kHero)->transform.position.y == -35.f);
    }

    // -- Undo / rename / scene + background commands round-trip ----------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(RenameEntityCommand{kSceneA, kHero, "Champion"}).ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->instanceName == "Champion");
        CHECK(!c.execute(RenameEntityCommand{kSceneA, kHero, ""}).ok); // empty rejected

        CHECK(c.execute(CreateSceneCommand{"scene-c", "Scene C"}).ok);
        CHECK(c.document().hasScene("scene-c"));
        // New scenes default to the dark neutral background; the vendored
        // struct default (white) is only for files saved without the field.
        CHECK(c.document().findScene("scene-c")->backgroundColor.r == 0.118f);
        CHECK(c.document().findScene("scene-c")->backgroundColor.b == 0.141f);
        CHECK(SceneDef{}.backgroundColor.r == 1.f);
        CHECK(!c.execute(CreateSceneCommand{"scene-a", "dup"}).ok); // duplicate rejected

        const EditorOperationResult background = c.execute(
            SetSceneBackgroundCommand{kSceneA, {1.f, 0.f, 0.f, 1.f}});
        CHECK(background.ok);
        CHECK(background.invalidation
              == (EditorInvalidation::Inspector | EditorInvalidation::Viewport));
        CHECK(c.document().findScene(kSceneA)->backgroundColor.r == 1.f);
        c.undo();                                                // undo background
        CHECK(c.document().findScene(kSceneA)->backgroundColor.r == 0.1f);
    }

    // -- CreateEntityCommand: add, invalidation, DomainChange, undo -------------
    {
        EditorCoordinator c{makeDoc()};
        const auto r = c.execute(
            CreateEntityCommand{kSceneA, 100, "Enemy", "Enemy 1", {5.f, 6.f}});
        CHECK(r.ok);
        CHECK(r.change.kind == DomainChangeKind::EntityAdded);
        CHECK(r.change.entityId == 100);
        CHECK(c.consumeInvalidations()
              == (EditorInvalidation::Hierarchy | EditorInvalidation::Inspector
                  | EditorInvalidation::Viewport | EditorInvalidation::Toolbar));
        const SceneInstanceDef* added = c.document().findInstanceInScene(kSceneA, 100);
        CHECK(added != nullptr);
        CHECK(added->objectTypeId == "Enemy");
        CHECK(added->transform.position.x == 5.f);
        // Undo removes exactly the placed instance.
        CHECK(c.canUndo());
        c.undo();
        CHECK(c.document().findInstanceInScene(kSceneA, 100) == nullptr);
    }

    // -- CreateEntityCommand: invalid input does not mutate or invalidate ------
    {
        EditorCoordinator c{makeDoc()};
        c.consumeInvalidations();
        const uint64_t revBefore = c.document().revision();
        // Each failed command returns no invalidation of its own (┬º24.3); the
        // coordinator only raises a Console error, never a structural flag.
        CHECK(c.execute(CreateEntityCommand{kSceneA, kHero, "Dup", "Dup", {}}).invalidation
              == EditorInvalidation::None);                                       // id clash
        CHECK(!c.execute(CreateEntityCommand{kSceneA, 0, "Enemy", "E", {}}).ok);  // zero id
        CHECK(!c.execute(CreateEntityCommand{kSceneA, 5, "", "E", {}}).ok);       // empty type
        CHECK(!c.execute(CreateEntityCommand{"missing", 5, "Enemy", "E", {}}).ok);// no scene
        const EditorInvalidation inv = c.consumeInvalidations();
        CHECK(!has(inv, EditorInvalidation::Hierarchy));
        CHECK(!has(inv, EditorInvalidation::Viewport));
        CHECK(c.document().revision() == revBefore);                  // state untouched
        CHECK(!c.canUndo());
        CHECK(c.document().findScene(kSceneA)->instances.size() == 1); // only kHero
    }

    // -- DeleteEntityCommand: remove, then undo restores order -----------------
    {
        EditorCoordinator c{makeDoc()};
        // Two more instances so order restoration is observable.
        CHECK(c.execute(CreateEntityCommand{kSceneA, 101, "Enemy", "E1", {}}).ok);
        CHECK(c.execute(CreateEntityCommand{kSceneA, 102, "Enemy", "E2", {}}).ok);
        // instances: [kHero, 101, 102]; delete the middle one.
        const auto r = c.execute(DeleteEntityCommand{kSceneA, 101});
        CHECK(r.ok);
        CHECK(r.change.kind == DomainChangeKind::EntityRemoved);
        CHECK(c.document().findInstanceInScene(kSceneA, 101) == nullptr);
        CHECK(c.document().findScene(kSceneA)->instances.size() == 2);
        // Undo restores it at its original index (1), not appended at the end.
        c.undo();
        const auto& instances = c.document().findScene(kSceneA)->instances;
        CHECK(instances.size() == 3);
        CHECK(instances[1].id == 101);
    }

    // -- DeleteEntityCommand: missing instance fails without side effects ------
    {
        EditorCoordinator c{makeDoc()};
        c.consumeInvalidations();
        const uint64_t revBefore = c.document().revision();
        const auto r = c.execute(DeleteEntityCommand{kSceneA, 9999});
        CHECK(!r.ok);
        CHECK(r.invalidation == EditorInvalidation::None);
        const EditorInvalidation inv = c.consumeInvalidations();
        CHECK(!has(inv, EditorInvalidation::Hierarchy));
        CHECK(!has(inv, EditorInvalidation::Viewport));
        CHECK(c.document().revision() == revBefore);
        CHECK(!c.canUndo());
    }

    // -- DeleteSceneCommand: removes scene + instances, undo is exact ----------
    {
        EditorCoordinator c{makeDoc()};
        // kSceneA is the start scene and holds kHero.
        CHECK(c.document().startSceneId() == kSceneA);
        const auto r = c.execute(DeleteSceneCommand{kSceneA});
        CHECK(r.ok);
        CHECK(r.change.kind == DomainChangeKind::SceneRemoved);
        // The command declares its own structural flags (incl. Toolbar: the set
        // of valid Play targets can change) ÔÇª
        CHECK(r.invalidation == (EditorInvalidation::Hierarchy
                                 | EditorInvalidation::Viewport
                                 | EditorInvalidation::Project
                                 | EditorInvalidation::Toolbar));
        // ÔÇª and the coordinator augments them after reconciling the workspace
        // (the active scene changed, so Inspector and Toolbar refresh too).
        const EditorInvalidation consumed = c.consumeInvalidations();
        CHECK(has(consumed, EditorInvalidation::Hierarchy));
        CHECK(has(consumed, EditorInvalidation::Viewport));
        CHECK(has(consumed, EditorInvalidation::Project));
        CHECK(has(consumed, EditorInvalidation::Inspector));
        CHECK(has(consumed, EditorInvalidation::Toolbar));
        CHECK(!c.document().hasScene(kSceneA));
        // Deleting the start scene reassigns it to a surviving scene.
        CHECK(c.document().startSceneId() == kSceneB);
        // Undo restores the scene, its instance, and the original start scene.
        c.undo();
        CHECK(c.document().hasScene(kSceneA));
        CHECK(c.document().findInstanceInScene(kSceneA, kHero) != nullptr);
        CHECK(c.document().startSceneId() == kSceneA);
    }

    // -- DeleteSceneCommand: structural commands never trigger a Replace -------
    {
        EditorCoordinator c{makeDoc()};
        const uint32_t replacesBefore = c.document().replaceCount();
        CHECK(c.execute(CreateEntityCommand{kSceneA, 200, "Enemy", "E", {}}).ok);
        CHECK(c.execute(DeleteEntityCommand{kSceneA, 200}).ok);
        CHECK(c.execute(DeleteSceneCommand{kSceneB}).ok);
        CHECK(c.document().replaceCount() == replacesBefore); // Patch, not Replace
    }

    // == Workspace reconciliation after structural deletes =====================
    // The document is the authority; EditorState must stay valid in the SAME
    // operation, with no follow-up callbacks or observers.

    // -- (1)(2) Deleting the active scene normalizes it and clears selection ----
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.state().activeSceneId == kSceneA);     // start scene is the default focus
        c.apply(SelectEntityIntent{kHero});
        CHECK(c.state().selection.primaryEntity == kHero);
        c.consumeInvalidations();

        CHECK(c.execute(DeleteSceneCommand{kSceneA}).ok);
        // (1) active scene normalized to a surviving scene.
        CHECK(c.state().activeSceneId == kSceneB);
        CHECK(c.document().hasScene(c.state().activeSceneId));
        // (2) selection cleared ÔÇö it belonged to the deleted scene.
        CHECK(!c.state().selection.hasEntity());
        // The coordinator augmented the command flags during reconciliation.
        CHECK(has(c.consumeInvalidations(), EditorInvalidation::Inspector));
    }

    // -- (3) Deleting a non-active scene keeps active scene and selection -------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.apply(SelectSceneIntent{kSceneB}).ok);
        CHECK(c.execute(CreateEntityCommand{kSceneB, 300, "Enemy", "E", {}}).ok);
        CHECK(c.apply(SelectEntityIntent{300}).ok);
        CHECK(c.state().activeSceneId == kSceneB);
        CHECK(c.state().selection.primaryEntity == 300);

        CHECK(c.execute(DeleteSceneCommand{kSceneA}).ok); // delete the OTHER scene
        CHECK(c.state().activeSceneId == kSceneB);        // unchanged
        CHECK(c.state().selection.primaryEntity == 300);  // unchanged
    }

    // -- (4) Deleting the selected entity clears the selection -----------------
    {
        EditorCoordinator c{makeDoc()};
        c.apply(SelectEntityIntent{kHero});
        CHECK(c.state().selection.primaryEntity == kHero);
        c.consumeInvalidations();
        CHECK(c.execute(DeleteEntityCommand{kSceneA, kHero}).ok);
        CHECK(!c.state().selection.hasEntity());
        // DeleteEntity alone returns Hierarchy|Viewport; reconciliation adds
        // Inspector so the now-empty inspector refreshes.
        CHECK(has(c.consumeInvalidations(), EditorInvalidation::Inspector));
    }

    // -- (5) Deleting a different entity preserves the selection ---------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(CreateEntityCommand{kSceneA, 301, "Enemy", "E", {}}).ok);
        c.apply(SelectEntityIntent{kHero});
        CHECK(c.state().selection.primaryEntity == kHero);
        CHECK(c.execute(DeleteEntityCommand{kSceneA, 301}).ok); // delete the OTHER one
        CHECK(c.state().selection.primaryEntity == kHero);      // preserved
    }

    // -- (6) No per-scene view state outlives its scene ------------------------
    {
        EditorCoordinator c{makeDoc()};
        c.apply(SetViewportZoomIntent{kSceneA, 2.f});
        c.apply(SetViewportZoomIntent{kSceneB, 3.f});
        CHECK(c.state().sceneViews.count(kSceneA) == 1);
        CHECK(c.state().sceneViews.count(kSceneB) == 1);
        CHECK(c.execute(DeleteSceneCommand{kSceneA}).ok);
        CHECK(c.state().sceneViews.count(kSceneA) == 0);  // pruned
        CHECK(c.state().sceneViews.count(kSceneB) == 1);  // kept
    }

    // -- (7) No dangling workspace id after execute OR undo --------------------
    // Undo restores the DOCUMENT, not the workspace: the brought-back scene is
    // not re-activated and the brought-back entity is not re-selected.
    {
        EditorCoordinator c{makeDoc()};
        c.apply(SelectEntityIntent{kHero});
        CHECK(c.execute(DeleteSceneCommand{kSceneA}).ok);
        CHECK(c.document().hasScene(c.state().activeSceneId));        // valid after execute
        CHECK(!c.state().selection.hasEntity());

        c.undo();
        CHECK(c.document().hasScene(kSceneA));                        // document restored
        CHECK(c.state().activeSceneId == kSceneB);                   // workspace NOT rewound
        CHECK(c.document().hasScene(c.state().activeSceneId));        // still valid
        CHECK(!c.state().selection.hasEntity());                     // not auto-reselected
    }

    // -- Empty project is a valid state: no fabricated scene -------------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(DeleteSceneCommand{kSceneA}).ok);
        CHECK(c.execute(DeleteSceneCommand{kSceneB}).ok);
        CHECK(c.document().data().scenes.empty());
        CHECK(c.state().activeSceneId.empty());   // no fake scene invented
        CHECK(!c.state().selection.hasEntity());
        CHECK(c.state().sceneViews.empty());
    }

    // == Hierarchy actions (the restricted UI wiring, tested UI-free) ==========
    // The Hierarchy panel is a thin shim over these functions; testing them here
    // proves the RmlUi click -> command -> reconcile -> invalidation path without
    // any RmlUi dependency.

    // (14) No action can reach a mutable ProjectDocument: document() is const-only.
    static_assert(std::is_same_v<decltype(std::declval<EditorCoordinator>().document()),
                                 const ProjectDocument&>,
                  "actions must only see a read-only document");

    // -- (1)(2)(13) Add Entity runs exactly one command with a non-colliding id -
    {
        EditorCoordinator c{makeDoc()};
        CHECK(nextAvailableEntityId(c.document(), kSceneA) == 43); // past kHero (42)
        const auto r = addEntity(c);
        CHECK(r.ok);
        CHECK(c.undoSize() == 1);                                  // exactly one command
        CHECK(c.document().findInstanceInScene(kSceneA, 43) != nullptr);
        CHECK(c.document().findInstanceInScene(kSceneA, 43)->id != kHero); // no collision
        CHECK(c.document().findInstanceInScene(kSceneA, 43)->transform.position.x == 256.f);
        CHECK(c.document().findInstanceInScene(kSceneA, 43)->transform.position.y == 160.f);
    }

    // -- Default spawn uses the visible viewport centre, not world origin -----
    {
        ViewportRect rect{100, 50, 800, 600};
        EditorSceneViewState view;
        const Vec2 spawn = defaultSpawnPosition(rect, view, Vec2{512.f, 320.f});
        CHECK(spawn.x == 256.f);
        CHECK(spawn.y == 160.f);

        view.zoom = 2.f;
        view.pan = Vec2{40.f, -30.f};
        const Vec2 panned = defaultSpawnPosition(rect, view, Vec2{512.f, 320.f});
        CHECK(panned.x == 296.f);
        CHECK(panned.y == 130.f);
    }

    // -- Spawn normalization snaps then clamps inside the scene ---------------
    {
        SpawnPositionOptions snap;
        snap.snapToGrid = true;
        snap.gridSize = 48.f;
        snap.edgeMargin = 16.f;
        const Vec2 snapped = normalizeSpawnPosition(Vec2{31.f, 73.f}, Vec2{512.f, 320.f}, snap);
        CHECK(snapped.x == 48.f);
        CHECK(snapped.y == 96.f);

        const Vec2 clamped = normalizeSpawnPosition(Vec2{-100.f, 1000.f}, Vec2{512.f, 320.f});
        CHECK(clamped.x == 16.f);
        CHECK(clamped.y == 304.f);
    }

    // -- unoccupiedSpawnPosition: default spawns cascade off occupied spots ---
    {
        const Vec2 sceneSize{512.f, 320.f};
        SceneDef scene;
        scene.id = kSceneA;

        // Free spot: unchanged.
        CHECK(unoccupiedSpawnPosition(scene, Vec2{256.f, 160.f}, sceneSize).x == 256.f);

        // One instance on the candidate: one diagonal step (half the default
        // 32 wu grid cell = 16 wu).
        SceneInstanceDef first;
        first.id = 1;
        first.transform.position = {256.f, 160.f};
        scene.instances.push_back(first);
        const Vec2 nudged = unoccupiedSpawnPosition(scene, Vec2{256.f, 160.f}, sceneSize);
        CHECK(nudged.x == 272.f);
        CHECK(nudged.y == 176.f);

        // The next spot occupied too: cascades a second step.
        SceneInstanceDef second;
        second.id = 2;
        second.transform.position = {272.f, 176.f};
        scene.instances.push_back(second);
        const Vec2 twice = unoccupiedSpawnPosition(scene, Vec2{256.f, 160.f}, sceneSize);
        CHECK(twice.x == 288.f);
        CHECK(twice.y == 192.f);

        // Pinned at the scene corner: the walk terminates and returns the
        // clamped candidate even if it is still occupied.
        SceneInstanceDef corner;
        corner.id = 3;
        corner.transform.position = {496.f, 304.f};   // == clamp target (margin 16)
        scene.instances.push_back(corner);
        const Vec2 pinned = unoccupiedSpawnPosition(scene, Vec2{496.f, 304.f}, sceneSize);
        CHECK(pinned.x == 496.f);
        CHECK(pinned.y == 304.f);

        // An explicit far-away candidate is never dragged toward occupied spots.
        const Vec2 free = unoccupiedSpawnPosition(scene, Vec2{100.f, 100.f}, sceneSize);
        CHECK(free.x == 100.f);
        CHECK(free.y == 100.f);
    }

    // -- Structural commands refresh the Inspector too: the Scene Inspector
    // shows the entity count and the outside-bounds diagnostic, so create and
    // delete must invalidate it even with no entity selected ------------------
    {
        EditorCoordinator c{makeDoc()};
        const auto created = c.execute(
            CreateEntityCommand{kSceneA, 900, "Enemy", "Enemy 900", {5.f, 6.f}});
        CHECK(created.ok);
        CHECK(has(created.invalidation, EditorInvalidation::Inspector));
        CHECK(has(created.invalidation, EditorInvalidation::Hierarchy));
        CHECK(has(created.invalidation, EditorInvalidation::Viewport));

        const auto removed = c.execute(DeleteEntityCommand{kSceneA, 900});
        CHECK(removed.ok);
        CHECK(has(removed.invalidation, EditorInvalidation::Inspector));
    }

    // -- Explicit placement is passed through the single create command path --
    {
        EditorCoordinator c{makeInheritedDoc()};
        CHECK(addEntityAt(c, Vec2{123.f, 234.f}).ok);
        const EntityId id = nextAvailableEntityId(c.document(), kSceneA) - 1;
        const SceneInstanceDef* inst = c.document().findInstanceInScene(kSceneA, id);
        CHECK(inst != nullptr);
        CHECK(inst->transform.position.x == 123.f);
        CHECK(inst->transform.position.y == 234.f);

        CHECK(c.undo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, id) == nullptr);
        CHECK(c.redo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, id)->transform.position.x == 123.f);
        CHECK(c.document().findInstanceInScene(kSceneA, id)->transform.position.y == 234.f);
    }

    // -- (3) Add Entity invalidates only the expected views --------------------
    {
        EditorCoordinator c{makeDoc()};
        c.consumeInvalidations();
        CHECK(addEntity(c).ok);
        // CreateEntity declares Hierarchy|Inspector|Viewport (the Scene
        // Inspector shows the entity count); selection unchanged and active
        // scene valid, so reconciliation adds nothing.
        CHECK(c.consumeInvalidations()
              == (EditorInvalidation::Hierarchy | EditorInvalidation::Inspector
                  | EditorInvalidation::Viewport | EditorInvalidation::Toolbar));
    }

    // -- (4) Add Entity without an active scene mutates nothing -----------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(deleteScene(c, kSceneA).ok);
        CHECK(deleteScene(c, kSceneB).ok);     // now the project has no scenes
        CHECK(c.state().activeSceneId.empty());
        const uint64_t revBefore = c.document().revision();
        const std::size_t undoBefore = c.undoSize();
        c.consumeInvalidations();
        CHECK(!addEntity(c).ok);               // precondition fails, no command runs
        CHECK(c.document().revision() == revBefore);
        CHECK(c.undoSize() == undoBefore);
        CHECK(c.consumeInvalidations() == EditorInvalidation::None);
    }

    // -- Empty catalog: Add Entity creates a real object type + instance -------
    //    Regression for "Unknown object type: Entity": the first entity in a
    //    catalog-less project must produce a real, persisted object type (never a
    //    sentinel id) so object-type-scoped components are usable at once.
    {
        ProjectDoc fresh;
        fresh.projectName = "fresh";
        fresh.activeSceneId = "s";
        SceneDef scene; scene.id = "s"; scene.name = "S";
        fresh.scenes.emplace("s", scene);

        EditorCoordinator c{fresh};
        CHECK(c.document().data().objectTypes.empty());     // empty catalog
        CHECK(addEntity(c).ok);
        CHECK(c.undoSize() == 1);                           // (1) one command / one undo entry

        // (1)(2) a real object type now exists and the instance points to it.
        CHECK(c.document().data().objectTypes.size() == 1);
        const std::string typeId = c.document().data().objectTypes.begin()->first;
        CHECK(typeId != "Entity");                          // (9) no sentinel id as objectTypeId
        const SceneInstanceDef* inst = c.document().findInstanceInScene("s", 1);
        CHECK(inst != nullptr);
        CHECK(inst->objectTypeId == typeId);                // (2) references the real type
        CHECK(c.document().hasObjectType(inst->objectTypeId));
        // Visual name mirrors the first instance ("Entity <entityId>") so
        // auto-created types are tellable apart in the Create menu catalog.
        CHECK(c.document().findObjectType(typeId)->name == "Entity 1");
        // Placeholder fill defaults to neutral zinc, never the struct's white
        // (invisible on light scenes, blinding on the dark default).
        CHECK(c.document().findObjectType(typeId)->sprite.fillColor.x == 0.42f);
        CHECK(c.document().findObjectType(typeId)->sprite.fillColor.z == 0.52f);

        // (3) object-type-scoped component commands work immediately (the type
        //     resolves). BoxCollider is not a movement driver, so it coexists with
        //     one driver; a second driver would be rejected (tested separately).
        CHECK(c.execute(AddTopDownControllerCommand{typeId}).ok);
        CHECK(c.document().data().objectTypes.at(typeId).topDownController.has_value());
        CHECK(c.execute(AddBoxColliderCommand{typeId}).ok);
        CHECK(c.document().data().objectTypes.at(typeId).boxCollider2D.has_value());
    }

    // -- Undo/redo of the first entity restores both type and instance ---------
    {
        ProjectDoc fresh;
        fresh.activeSceneId = "s";
        SceneDef scene; scene.id = "s"; scene.name = "S";
        fresh.scenes.emplace("s", scene);

        EditorCoordinator c{fresh};
        CHECK(addEntity(c).ok);
        const std::string typeId = c.document().data().objectTypes.begin()->first;

        // (4) undo removes BOTH the instance and the object type it created.
        CHECK(c.undo().ok);
        CHECK(c.document().findInstanceInScene("s", 1) == nullptr);
        CHECK(c.document().data().objectTypes.empty());

        // (5) redo restores both with the same ids (no fresh generation).
        CHECK(c.redo().ok);
        CHECK(c.document().findInstanceInScene("s", 1) != nullptr);
        CHECK(c.document().data().objectTypes.size() == 1);
        CHECK(c.document().data().objectTypes.begin()->first == typeId);
        CHECK(c.document().findInstanceInScene("s", 1)->objectTypeId == typeId);
    }

    // -- (6) Add Entity always creates a NEW independent object type -----------
    //    "+Entity" must never reuse an existing type (that is "Add Instance"):
    //    each entity gets its own ObjectTypeId so the object-type-owned components
    //    stay independent across entities.
    {
        EditorCoordinator c{makeInheritedDoc()};            // catalog already has "Hero"
        CHECK(c.document().hasObjectType("Hero"));
        const std::size_t typesBefore = c.document().data().objectTypes.size();

        CHECK(addEntity(c).ok);                             // first +Entity
        const EntityId idA = nextAvailableEntityId(c.document(), kSceneA) - 1;
        const std::string typeA = c.document().findInstanceInScene(kSceneA, idA)->objectTypeId;
        CHECK(addEntity(c).ok);                             // second +Entity
        const EntityId idB = nextAvailableEntityId(c.document(), kSceneA) - 1;
        const std::string typeB = c.document().findInstanceInScene(kSceneA, idB)->objectTypeId;

        // (1)(2)(3) two new, distinct types ÔÇö neither reuses "Hero" nor each other.
        CHECK(c.document().data().objectTypes.size() == typesBefore + 2);
        CHECK(typeA != "Hero");
        CHECK(typeB != "Hero");
        CHECK(typeA != typeB);

        // (4)(5) a component on A's type does not appear on B's type.
        CHECK(c.execute(AddTopDownControllerCommand{typeA}).ok);
        CHECK(c.document().data().objectTypes.at(typeA).topDownController.has_value());
        CHECK(!c.document().data().objectTypes.at(typeB).topDownController.has_value());
        CHECK(c.execute(AddBoxColliderCommand{typeB}).ok);
        CHECK(c.document().data().objectTypes.at(typeB).boxCollider2D.has_value());
        CHECK(!c.document().data().objectTypes.at(typeA).boxCollider2D.has_value());
    }

    // -- (6b) Undo of the second Add removes only its type + instance ----------
    {
        EditorCoordinator c{makeInheritedDoc()};
        CHECK(addEntity(c).ok);
        const EntityId idA = nextAvailableEntityId(c.document(), kSceneA) - 1;
        const std::string typeA = c.document().findInstanceInScene(kSceneA, idA)->objectTypeId;
        CHECK(addEntity(c).ok);
        const EntityId idB = nextAvailableEntityId(c.document(), kSceneA) - 1;
        const std::string typeB = c.document().findInstanceInScene(kSceneA, idB)->objectTypeId;

        // (6) undo the second +Entity: only B's type and instance go.
        CHECK(c.undo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, idB) == nullptr);
        CHECK(!c.document().hasObjectType(typeB));
        CHECK(c.document().findInstanceInScene(kSceneA, idA) != nullptr);
        CHECK(c.document().hasObjectType(typeA));            // A untouched

        // (7) redo restores B with the same ids.
        CHECK(c.redo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, idB)->objectTypeId == typeB);
        CHECK(c.document().hasObjectType(typeB));
    }

    // -- (7) A failed create-with-default-type makes no partial mutation -------
    {
        EditorCoordinator c{makeInheritedDoc()};            // "Hero" already exists
        const std::size_t typesBefore = c.document().data().objectTypes.size();
        const uint64_t revBefore = c.document().revision();
        // objectTypeId collides -> apply fails before any mutation.
        CHECK(!c.execute(CreateEntityWithDefaultTypeCommand{
                  kSceneA, 999, "Hero", "Entity", "X"}).ok);
        CHECK(c.document().data().objectTypes.size() == typesBefore);
        CHECK(c.document().findInstanceInScene(kSceneA, 999) == nullptr);
        CHECK(c.document().revision() == revBefore);        // no partial state
    }

    // -- Create type + instance is one staged authoring mutation --------------
    {
        ProjectDoc fresh;
        SceneDef scene;
        scene.id = "s";
        scene.name = "S";
        scene.layers.push_back(SceneLayerDef{"layer-1", "Layer 1", false});
        scene.defaultLayerId = "layer-1";
        fresh.scenes.emplace("s", scene);
        fresh.activeSceneId = "s";
        EditorCoordinator c{std::move(fresh)};
        const uint64_t revisionBefore = c.document().revision();
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Entity", "Entity", {}, "layer-1"}).ok);
        CHECK(c.document().revision() == revisionBefore + 1);
        CHECK(c.document().hasObjectType("obj-1"));
        CHECK(c.document().findInstanceInScene("s", 1) != nullptr);
        CHECK(c.undo().ok);
        CHECK(!c.document().hasObjectType("obj-1"));
        CHECK(c.document().findInstanceInScene("s", 1) == nullptr);
        CHECK(c.redo().ok);
        CHECK(c.document().hasObjectType("obj-1"));
        CHECK(c.document().findInstanceInScene("s", 1) != nullptr);
    }

    // -- (8) Save/reload keeps the two created types distinct ------------------
    {
        ProjectDoc fresh;
        fresh.activeSceneId = "s";
        SceneDef scene; scene.id = "s"; scene.name = "S";
        fresh.scenes.emplace("s", scene);

        EditorCoordinator c{fresh};
        CHECK(addEntity(c).ok);
        const EntityId idA = nextAvailableEntityId(c.document(), "s") - 1;
        const std::string typeA = c.document().findInstanceInScene("s", idA)->objectTypeId;
        CHECK(addEntity(c).ok);
        const EntityId idB = nextAvailableEntityId(c.document(), "s") - 1;
        const std::string typeB = c.document().findInstanceInScene("s", idB)->objectTypeId;
        CHECK(typeA != typeB);

        const std::filesystem::path path = testTempDir() / "default-type.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);

        EditorCoordinator reloaded{makeReplacementDoc()};
        CHECK(loadProjectFromFile(reloaded, path).ok);
        CHECK(reloaded.document().hasObjectType(typeA));
        CHECK(reloaded.document().hasObjectType(typeB));    // both types survive, still distinct
        CHECK(reloaded.document().findInstanceInScene("s", idA)->objectTypeId == typeA);
        CHECK(reloaded.document().findInstanceInScene("s", idB)->objectTypeId == typeB);
    }

    // -- Add Instance: another instance of the selected entity's type ----------
    //    Unlike +Entity, +Instance reuses the chosen ObjectTypeId (shared
    //    components, no ObjectTypeDef duplicated) and selects the new instance.
    {
        EditorCoordinator c{makeInheritedDoc()};            // kHero -> "Hero" type w/ sprite
        c.apply(SelectEntityIntent{kHero});
        const std::size_t typesBefore = c.document().data().objectTypes.size();

        CHECK(addInstanceOfSelectedType(c).ok);
        const EntityId newId = nextAvailableEntityId(c.document(), kSceneA) - 1;
        const SceneInstanceDef* inst = c.document().findInstanceInScene(kSceneA, newId);
        CHECK(newId != kHero);                                          // (1) new EntityId
        CHECK(inst != nullptr);
        CHECK(inst->objectTypeId == "Hero");                           // (2) reuses the type
        CHECK(c.document().data().objectTypes.size() == typesBefore);  // no type duplicated
        CHECK(c.selection().primaryEntity == newId);                  // (12) selected via intent

        // (3) independent Transform (its own, not the source's {10,20}).
        const SceneInstanceDef* src = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK((inst->transform.position.x != src->transform.position.x)
              || (inst->transform.position.y != src->transform.position.y));

        // (4)(5) a component added to the type materializes on BOTH instances.
        // RU-03: PlaySession no longer exposes per-entity component
        // introspection (D-01), so this only checks Play still starts/stops
        // cleanly with the collider-bearing type - collider behavior itself is
        // GameplaySession/World's own (already characterized by runtime-cpp's
        // test suite), not re-verified here.
        CHECK(c.execute(AddBoxColliderCommand{"Hero"}).ok);
        CHECK(c.playProject().ok);
        CHECK(c.stopPlaying().ok);
    }

    // -- Add Instance uses the same explicit placement path -------------------
    {
        EditorCoordinator c{makeInheritedDoc()};
        c.apply(SelectEntityIntent{kHero});
        CHECK(addInstanceOfSelectedTypeAt(c, Vec2{200.f, 120.f}).ok);
        const EntityId newId = nextAvailableEntityId(c.document(), kSceneA) - 1;
        const SceneInstanceDef* inst = c.document().findInstanceInScene(kSceneA, newId);
        CHECK(inst != nullptr);
        CHECK(inst->objectTypeId == "Hero");
        CHECK(inst->transform.position.x == 200.f);
        CHECK(inst->transform.position.y == 120.f);

        CHECK(c.undo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, newId) == nullptr);
        CHECK(c.redo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, newId)->transform.position.x == 200.f);
        CHECK(c.document().findInstanceInScene(kSceneA, newId)->transform.position.y == 120.f);
    }

    // -- (7)(8) Undo removes only the instance; the type survives; redo restores
    {
        EditorCoordinator c{makeInheritedDoc()};
        c.apply(SelectEntityIntent{kHero});
        CHECK(addInstanceOfSelectedType(c).ok);
        const EntityId newId = nextAvailableEntityId(c.document(), kSceneA) - 1;

        CHECK(c.undo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, newId) == nullptr);  // instance gone
        CHECK(c.document().hasObjectType("Hero"));                            // type kept
        CHECK(c.document().findInstanceInScene(kSceneA, kHero) != nullptr);   // source kept

        CHECK(c.redo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, newId)->objectTypeId == "Hero");
    }

    // -- (9) Save/reload keeps the instance <-> type relation ------------------
    {
        EditorCoordinator c{makeInheritedDoc()};
        c.apply(SelectEntityIntent{kHero});
        CHECK(addInstanceOfSelectedType(c).ok);
        const EntityId newId = nextAvailableEntityId(c.document(), kSceneA) - 1;
        const std::filesystem::path path = testTempDir() / "add-instance.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);
        EditorCoordinator reloaded{ProjectDoc{}};
        CHECK(loadProjectFromFile(reloaded, path).ok);
        CHECK(reloaded.document().findInstanceInScene(kSceneA, newId)->objectTypeId == "Hero");
        CHECK(reloaded.document().findInstanceInScene(kSceneA, kHero)->objectTypeId == "Hero");
        CHECK(reloaded.document().hasObjectType("Hero"));
    }

    // -- Use Existing Type: places an instance of an EXPLICIT type id, no
    // selection required (unlike +Instance, which derives the type from it) ---
    {
        EditorCoordinator c{makeInheritedDoc()};             // catalog has "Hero"
        // Deliberately no SelectEntityIntent applied - proves this path does not
        // depend on the current selection at all.
        CHECK(addInstanceOfType(c, "Hero").ok);
        const EntityId newId = nextAvailableEntityId(c.document(), kSceneA) - 1;
        const SceneInstanceDef* inst = c.document().findInstanceInScene(kSceneA, newId);
        CHECK(inst != nullptr);
        CHECK(inst->objectTypeId == "Hero");
        CHECK(c.selection().primaryEntity == newId);         // still auto-selects the new instance
    }

    // -- Use Existing Type: unknown type id fails without mutation -------------
    {
        EditorCoordinator c{makeInheritedDoc()};
        const std::size_t before = c.document().findScene(kSceneA)->instances.size();
        CHECK(!addInstanceOfType(c, "NoSuchType").ok);
        CHECK(c.document().findScene(kSceneA)->instances.size() == before);
    }

    // -- Use Existing Type at an explicit position (the *At variant) -----------
    {
        EditorCoordinator c{makeInheritedDoc()};
        CHECK(addInstanceOfTypeAt(c, "Hero", Vec2{77.f, 88.f}).ok);
        const EntityId newId = nextAvailableEntityId(c.document(), kSceneA) - 1;
        const SceneInstanceDef* inst = c.document().findInstanceInScene(kSceneA, newId);
        CHECK(inst->transform.position.x == 77.f);
        CHECK(inst->transform.position.y == 88.f);
    }

    // -- Clone Instance: copies type, uniquifies name, honors an explicit
    // position; undo removes only the clone, redo restores it -----------------
    {
        EditorCoordinator c{makeInheritedDoc()};
        const EntityId newId = nextAvailableEntityId(c.document(), kSceneA);
        CHECK(c.execute(CloneInstanceCommand{kSceneA, kHero, newId, "Hero 2", Vec2{34.f, 44.f}}).ok);
        const SceneInstanceDef* clone = c.document().findInstanceInScene(kSceneA, newId);
        CHECK(clone != nullptr);
        CHECK(clone->objectTypeId == "Hero");
        CHECK(clone->instanceName == "Hero 2");
        CHECK(clone->transform.position.x == 34.f);
        CHECK(clone->transform.position.y == 44.f);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero) != nullptr);  // source untouched

        CHECK(c.undo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, newId) == nullptr);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero) != nullptr);
        CHECK(c.redo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, newId)->instanceName == "Hero 2");
    }

    // -- Clone Instance: instance deltas and layer survive the struct copy -----
    {
        EditorCoordinator c{makeInheritedDoc()};
        c.apply(SelectEntityIntent{kHero});
        CHECK(addSpriteRenderer(c).ok);
        CHECK(c.execute(AddSceneLayerCommand{kSceneA, "fg", "Foreground", 0}).ok);
        CHECK(c.execute(SetEntityLayerCommand{kSceneA, kHero, "fg"}).ok);

        const EntityId newId = nextAvailableEntityId(c.document(), kSceneA);
        CHECK(c.execute(CloneInstanceCommand{kSceneA, kHero, newId, "Hero Clone", Vec2{}}).ok);
        const SceneInstanceDef* clone = c.document().findInstanceInScene(kSceneA, newId);
        CHECK(clone != nullptr);
        CHECK(clone && !clone->spriteRendererOverride);
        CHECK(resolveSpriteRenderer(c.document(), kSceneA, newId).present);
        CHECK(clone->layerId == "fg");
    }

    // -- Clone Instance: guards mirror CreateEntityCommand's (id/name/scene),
    // plus its own (unknown source instance); none mutate the scene -----------
    {
        EditorCoordinator c{makeInheritedDoc()};
        const std::size_t before = c.document().findScene(kSceneA)->instances.size();
        CHECK(!c.execute(CloneInstanceCommand{kSceneA, kHero, kHero, "Dup", {}}).ok);   // id clash
        CHECK(!c.execute(CloneInstanceCommand{kSceneA, kHero, 0, "Zero", {}}).ok);      // zero id
        CHECK(!c.execute(CloneInstanceCommand{kSceneA, kHero, 900, "", {}}).ok);        // empty name
        CHECK(!c.execute(CloneInstanceCommand{"missing", kHero, 900, "X", {}}).ok);     // no scene
        CHECK(!c.execute(CloneInstanceCommand{kSceneA, 9999, 900, "X", {}}).ok);        // no source
        CHECK(c.document().findScene(kSceneA)->instances.size() == before);
    }

    // -- cloneSelectedEntity: no selection -> failure without mutation ---------
    {
        EditorCoordinator c{makeInheritedDoc()};
        const std::size_t before = c.document().findScene(kSceneA)->instances.size();
        CHECK(!cloneSelectedEntity(c).ok);
        CHECK(c.document().findScene(kSceneA)->instances.size() == before);
    }

    // -- cloneSelectedEntity: clones the selection, offsets the position, and
    // selects the new instance --------------------------------------------------
    {
        EditorCoordinator c{makeInheritedDoc()};
        c.apply(SelectEntityIntent{kHero});
        CHECK(cloneSelectedEntity(c).ok);
        const EntityId newId = nextAvailableEntityId(c.document(), kSceneA) - 1;
        const SceneInstanceDef* clone = c.document().findInstanceInScene(kSceneA, newId);
        CHECK(clone != nullptr);
        CHECK(clone->objectTypeId == "Hero");
        CHECK(clone->instanceName != "Hero");             // uniquified
        CHECK(clone->transform.position.x != 10.f);        // offset from source (10, 20)
        CHECK(clone->transform.position.y != 20.f);
        CHECK(c.selection().primaryEntity == newId);       // auto-selected
        CHECK(c.document().findInstanceInScene(kSceneA, kHero) != nullptr);  // source kept
    }

    // -- (10) Empty catalog: Add Instance is a no-op, never a placeholder ------
    {
        ProjectDoc fresh; fresh.activeSceneId = "s";
        SceneDef scene; scene.id = "s"; scene.name = "S";
        fresh.scenes.emplace("s", scene);
        EditorCoordinator c{fresh};
        CHECK(!addInstanceOfSelectedType(c).ok);                  // no selection -> fail
        CHECK(c.document().data().objectTypes.empty());           // no "Entity" placeholder
        CHECK(c.document().findScene("s")->instances.empty());    // no instance
    }

    // -- (11) During Play, Add Instance is rejected without mutation -----------
    {
        EditorCoordinator c{makeInheritedDoc()};
        c.apply(SelectEntityIntent{kHero});
        CHECK(c.playProject().ok);
        const std::size_t before = c.document().findScene(kSceneA)->instances.size();
        CHECK(!addInstanceOfSelectedType(c).ok);
        CHECK(c.document().findScene(kSceneA)->instances.size() == before);
        CHECK(c.stopPlaying().ok);
    }

    // -- (5) Delete Entity uses the authoritative scene + selection ------------
    {
        EditorCoordinator c{makeDoc()};
        c.apply(SelectEntityIntent{kHero});
        CHECK(deleteSelectedEntity(c).ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero) == nullptr);
    }

    // -- (6) Deleting the selected entity empties the selection (reconcile) -----
    {
        EditorCoordinator c{makeDoc()};
        c.apply(SelectEntityIntent{kHero});
        c.consumeInvalidations();
        CHECK(deleteSelectedEntity(c).ok);
        CHECK(!c.state().selection.hasEntity());
        CHECK(has(c.consumeInvalidations(), EditorInvalidation::Inspector));
    }

    // -- (7) A failed Delete Entity triggers no panel refresh ------------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(!c.selection().hasEntity());
        c.consumeInvalidations();
        const std::size_t undoBefore = c.undoSize();
        CHECK(!deleteSelectedEntity(c).ok);    // nothing selected -> no command
        const EditorInvalidation inv = c.consumeInvalidations();
        CHECK(!has(inv, EditorInvalidation::Hierarchy));
        CHECK(!has(inv, EditorInvalidation::Viewport));
        CHECK(c.undoSize() == undoBefore);
    }

    // -- (8) Add Scene opens the new scene (create-and-open), via an Intent, not
    // by the command reaching into EditorState itself --------------------------
    {
        EditorCoordinator c{makeDoc()};
        const SceneId expectedId = makeUniqueSceneId(c.document());   // "scene-1"
        const auto r = addScene(c);
        CHECK(r.ok);
        CHECK(c.state().activeSceneId == expectedId);      // new scene is now open
        CHECK(!c.state().selection.hasEntity());
        CHECK(c.document().hasScene(expectedId));          // the new scene now exists
    }

    // -- (9) Delete active scene normalizes the workspace in the same cycle ----
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.state().activeSceneId == kSceneA);
        CHECK(deleteScene(c, kSceneA).ok);
        CHECK(c.state().activeSceneId == kSceneB);
        CHECK(c.document().hasScene(c.state().activeSceneId));
    }

    // -- (10) Delete non-active scene preserves scene + selection --------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.apply(SelectSceneIntent{kSceneB}).ok);
        CHECK(addEntity(c).ok);                            // an entity in B
        const EntityId placed = nextAvailableEntityId(c.document(), kSceneB) - 1;
        CHECK(c.apply(SelectEntityIntent{placed}).ok);
        CHECK(deleteScene(c, kSceneA).ok);                 // delete the OTHER scene
        CHECK(c.state().activeSceneId == kSceneB);
        CHECK(c.state().selection.primaryEntity == placed);
    }

    // -- (11)(12) Undo refreshes via command invalidations, not the workspace --
    {
        EditorCoordinator c{makeDoc()};
        c.apply(SelectEntityIntent{kHero});
        CHECK(deleteSelectedEntity(c).ok);                 // selection cleared
        c.consumeInvalidations();
        c.undo();                                          // entity comes back
        // (11) the Hierarchy is told to refresh by the command's own invalidation.
        CHECK(has(c.consumeInvalidations(), EditorInvalidation::Hierarchy));
        CHECK(c.document().findInstanceInScene(kSceneA, kHero) != nullptr);
        // (12) the brought-back entity is NOT auto-reselected.
        CHECK(!c.state().selection.hasEntity());
    }

    // -- (12b) Undo of delete-active-scene does not re-activate the scene ------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(deleteScene(c, kSceneA).ok);                 // active -> kSceneB
        CHECK(c.state().activeSceneId == kSceneB);
        c.undo();                                          // scene A back in document
        CHECK(c.document().hasScene(kSceneA));
        CHECK(c.state().activeSceneId == kSceneB);         // workspace not rewound
    }

    // -- (15) No Hierarchy action path performs a Replace ----------------------
    {
        EditorCoordinator c{makeDoc()};
        const uint32_t replacesBefore = c.document().replaceCount();
        CHECK(addScene(c).ok);
        const SceneId newScene = c.state().activeSceneId;   // addScene opens what it created
        CHECK(addEntity(c).ok);                             // lands in the new scene
        c.apply(SelectEntityIntent{nextAvailableEntityId(c.document(), newScene) - 1});
        CHECK(deleteSelectedEntity(c).ok);
        CHECK(deleteScene(c, kSceneB).ok);
        CHECK(c.document().replaceCount() == replacesBefore);   // Patch, never Replace
    }

    // == Play guards: an action without a valid target must be unavailable =====
    // The guard lives in the coordinator (the point that creates the session),
    // so a shortcut, menu or programmatic call cannot bypass a disabled button.

    // -- (1)(2)(3)(4) Empty project: both modes unavailable and rejected -------
    {
        EditorCoordinator c{ProjectDoc{}};             // zero scenes
        CHECK(c.document().data().scenes.empty());
        CHECK(!c.canPlayProject());                    // (1)
        CHECK(!c.canPlayCurrentScene());               // (2)
        c.consumeInvalidations();
        const auto r = c.playProject();
        CHECK(!r.ok);                                  // (3) application-level rejection
        CHECK(!c.isPlaying());                         // (4) no PlaySession created
        CHECK(c.playSession() == nullptr);
        // The rejection is logged to the Console rather than silently dropped,
        // which is itself an invalidation.
        CHECK(c.consumeInvalidations() == EditorInvalidation::Console);
    }

    // -- (5) Creating the first scene invalidates the Toolbar ------------------
    {
        EditorCoordinator c{ProjectDoc{}};
        c.consumeInvalidations();
        CHECK(c.execute(CreateSceneCommand{"scene-1", "Scene 1"}).ok);
        CHECK(has(c.consumeInvalidations(), EditorInvalidation::Toolbar));
    }

    // -- (6) Play Project enables when the start scene is valid ----------------
    {
        EditorCoordinator c{makeDoc()};                // startSceneId == kSceneA
        CHECK(c.canPlayProject());
        const auto r = c.playProject();
        CHECK(r.ok);
        CHECK(c.isPlaying());
        CHECK(c.playSession() != nullptr);
        CHECK(c.playSession()->sceneId() == kSceneA);  // Play Project target = start scene
        // Play does not touch the authoring document.
        CHECK(!c.document().isDirty());
    }

    // -- (7) Without an active scene, Play Current Scene stays disabled --------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.state().activeSceneId.empty());
        CHECK(!c.canPlayCurrentScene());
        CHECK(!c.playCurrentScene().ok);
        CHECK(!c.isPlaying());
    }

    // -- Start/Stop Play re-render the authoring-affordance panels -------------
    //    The authoring document is frozen during Play, so the Inspector,
    //    Hierarchy and Assets panels must re-render to disable their authoring
    //    controls on Start ÔÇö and re-enable them on Stop.
    {
        EditorCoordinator c{makeDoc()};
        c.consumeInvalidations();
        CHECK(c.playProject().ok);
        const EditorInvalidation started = c.consumeInvalidations();
        CHECK(has(started, EditorInvalidation::Inspector));
        CHECK(has(started, EditorInvalidation::Hierarchy));
        CHECK(has(started, EditorInvalidation::Assets));
        CHECK(c.stopPlaying().ok);
        const EditorInvalidation stopped = c.consumeInvalidations();
        CHECK(has(stopped, EditorInvalidation::Inspector));
        CHECK(has(stopped, EditorInvalidation::Hierarchy));
        CHECK(has(stopped, EditorInvalidation::Assets));
    }

    // -- (8) Selecting a scene enables Play Current Scene ----------------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.apply(SelectSceneIntent{kSceneB}).ok);
        CHECK(c.canPlayCurrentScene());
        const auto r = c.playCurrentScene();
        CHECK(r.ok);
        CHECK(c.playSession()->sceneId() == kSceneB);  // target = active scene
    }

    // -- (9) Deleting the last scene disables both modes in the same cycle -----
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(DeleteSceneCommand{kSceneA}).ok);
        CHECK(c.execute(DeleteSceneCommand{kSceneB}).ok);
        CHECK(c.document().data().scenes.empty());
        CHECK(!c.canPlayProject());
        CHECK(!c.canPlayCurrentScene());
    }

    // -- (10) A direct (shortcut/programmatic) Play cannot bypass the guard ----
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(!c.playProject().ok);        // no button involved ÔÇö the app path itself rejects
        CHECK(!c.playCurrentScene().ok);
        CHECK(!c.isPlaying());
    }

    // -- (11) A dangling startSceneId prevents Play Project --------------------
    {
        EditorCoordinator c{makeInvalidStartDoc()};    // startSceneId references no scene
        CHECK(!c.document().hasScene(c.document().startSceneId()));
        CHECK(!c.canPlayProject());
        CHECK(!c.playProject().ok);
    }

    // -- (12) A dangling activeSceneId prevents Play Current Scene -------------
    {
        EditorCoordinator c{makeInvalidStartDoc()};    // active scene also dangling
        CHECK(!c.document().hasScene(c.state().activeSceneId));
        CHECK(!c.canPlayCurrentScene());
        CHECK(!c.playCurrentScene().ok);
    }

    // -- Play/Stop round-trip: Stop only while playing, document untouched -----
    {
        EditorCoordinator c{makeDoc()};
        CHECK(!c.stopPlaying().ok);        // not playing yet
        CHECK(c.playProject().ok);
        CHECK(c.isPlaying());
        CHECK(c.stopPlaying().ok);
        CHECK(!c.isPlaying());
        CHECK(!c.document().isDirty());    // never mutated by Play/Stop
    }

    // -- RU-04: Play-start parses through the real canonical AssetLoader, not
    // just document.data() in memory - leaves no scratch files behind -------
    {
        // Count pre-existing scratch directories from any earlier Play in this
        // process (PlaySession never reuses one - see makeScratchDir()) so a
        // net-zero delta after Play/Stop is a real "nothing leaked" signal,
        // not a false pass from a directory some other test already left.
        const auto countScratchDirs = [] {
            std::error_code ec;
            std::size_t count = 0;
            for (const auto& entry : std::filesystem::directory_iterator(
                     std::filesystem::temp_directory_path(), ec)) {
                if (entry.path().filename().string().rfind("artcade-play-", 0) == 0) ++count;
            }
            return count;
        };
        const std::size_t before = countScratchDirs();
        EditorCoordinator c{makeInheritedDoc()};
        CHECK(c.playProject().ok);
        CHECK(countScratchDirs() == before);   // torn down synchronously in materialize()
        CHECK(c.stopPlaying().ok);
        CHECK(countScratchDirs() == before);
    }

    // -- RU-04: a scene/instance the canonical loader would reject (e.g. an
    // empty layers array, or an instance pointing at no real layer) fails
    // Play atomically with an explicit diagnostic - never a silent fallback
    // to the in-memory ProjectDoc, and never a crash ------------------------
    {
        ProjectDoc noLayerDoc = makeDoc();
        noLayerDoc.scenes.at(kSceneA).layers.clear();
        noLayerDoc.scenes.at(kSceneA).defaultLayerId.clear();
        EditorCoordinator noLayer{noLayerDoc};
        const EditorOperationResult noLayerResult = noLayer.playProject();
        CHECK(!noLayerResult.ok);
        CHECK(!noLayerResult.error.empty());
        CHECK(!noLayer.isPlaying());

        ProjectDoc danglingLayerDoc = makeDoc();
        danglingLayerDoc.scenes.at(kSceneA).instances.front().layerId = "no-such-layer";
        EditorCoordinator danglingLayer{danglingLayerDoc};
        const EditorOperationResult danglingLayerResult = danglingLayer.playProject();
        CHECK(!danglingLayerResult.ok);
        CHECK(!danglingLayerResult.error.empty());
        CHECK(!danglingLayer.isPlaying());
    }

    // -- Play materializes runtime sprite data and used assets only ------------
    // RU-03: PlaySession no longer exposes raw entity/asset introspection
    // (D-01) - the render hand-off (buildFrame() via collectSceneFrameSnapshot)
    // is the only public surface, so that's what this now checks.
    {
        EditorCoordinator c{makeInheritedDoc()};
        CHECK(c.playProject().ok);
        CHECK(c.playSession() != nullptr);

        const SceneFrameSnapshot playFrame = collectSceneFrameSnapshot(*c.playSession());
        CHECK(playFrame.hasScene);
        CHECK(playFrame.sprites.size() == 1);
        CHECK(playFrame.sprites[0].assetId == "img-hero");
        CHECK(playFrame.sprites[0].destination.x == -6.f); // x=10, width=32
    }

    // -- A materialized PlaySession is independent from later authoring edits --
    {
        EditorCoordinator c{makeInheritedDoc()};
        std::optional<PlaySession> session = PlaySession::startProject(c.document());
        CHECK(session.has_value()); // inherited img-hero at x=10
        CHECK(c.execute(SetEntityTransformCommand{kSceneA, kHero, {50.f, 20.f}}).ok);
        SpriteRendererOverride delta;
        delta.imageAssetId = "img-alt";
        CHECK(c.execute(SetInstanceSpriteOverrideCommand{kSceneA, kHero, delta}).ok);

        const SceneFrameSnapshot editFrame =
            collectSceneFrameSnapshot(c.document(), kSceneA, INVALID_ENTITY);
        CHECK(editFrame.sprites.size() == 1);
        CHECK(editFrame.sprites[0].assetId == "img-alt");
        CHECK(editFrame.sprites[0].destination.x == 34.f); // x=50, width=32

        const SceneFrameSnapshot playFrame = collectSceneFrameSnapshot(*session);
        CHECK(playFrame.sprites.size() == 1);
        CHECK(playFrame.sprites[0].assetId == "img-hero");
        CHECK(playFrame.sprites[0].destination.x == -6.f); // still x=10

        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.x == 50.f);
        CHECK(c.document().revision() > 0);
    }

    // -- Instance visibility gates the whole entity in Edit and Play ----------
    {
        EditorCoordinator c{makeInheritedDoc()};
        CHECK(collectSceneFrameSnapshot(
            c.document(), kSceneA, kHero).entities.size() == 1);
        CHECK(c.execute(SetInstanceVisibleCommand{kSceneA, kHero, false}).ok);
        CHECK(!c.document().findInstanceInScene(kSceneA, kHero)->visible);
        const SceneFrameSnapshot hiddenEdit =
            collectSceneFrameSnapshot(c.document(), kSceneA, kHero);
        CHECK(hiddenEdit.entities.empty());
        CHECK(hiddenEdit.sprites.empty());
        CHECK(hiddenEdit.tilemaps.empty());
        CHECK(hiddenEdit.colliders.empty());
        CHECK(c.undo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->visible);
        CHECK(c.redo().ok);

        CHECK(c.playProject().ok);
        const SceneFrameSnapshot hiddenPlay =
            collectSceneFrameSnapshot(*c.playSession());
        CHECK(hiddenPlay.entities.empty());
        CHECK(hiddenPlay.sprites.empty());
        CHECK(hiddenPlay.tilemaps.empty());
    }

    // -- Play materialization covers absence, visibility, and dangling assets ---
    // RU-03: a spriteless entity gets no SpriteComponent in World, and
    // visible=false resolves to the shared visibleInGame tag - either way the
    // entity is absent from GameplaySession's renderables (D-01: the same rule
    // game.exe/WASM already apply), not "present but flagged", so the frame
    // snapshot is simply empty in both cases.
    {
        EditorCoordinator noSprite{makeSpriteDoc()};
        CHECK(noSprite.playProject().ok);
        const SceneFrameSnapshot noSpriteFrame =
            collectSceneFrameSnapshot(*noSprite.playSession());
        CHECK(noSpriteFrame.sprites.empty());

        EditorCoordinator invisible{makeInheritedDoc()};
        SpriteRendererOverride invisibleDelta;
        invisibleDelta.imageAssetId = "img-alt";
        invisibleDelta.visible = false;
        CHECK(invisible.execute(SetInstanceSpriteOverrideCommand{
            kSceneA, kHero, invisibleDelta}).ok);
        CHECK(invisible.playProject().ok);
        const SceneFrameSnapshot invisibleFrame =
            collectSceneFrameSnapshot(*invisible.playSession());
        CHECK(invisibleFrame.sprites.empty());
        CHECK(invisibleFrame.entities.empty());

        ProjectDoc danglingDoc = makeInheritedDoc();
        danglingDoc.objectTypes.at("Hero").spriteRenderer->imageAssetId = "missing-image";
        EditorCoordinator dangling{danglingDoc};
        CHECK(!dangling.playProject().ok);
        CHECK(!dangling.isPlaying());
    }

    // -- Project replace has an explicit policy during Play --------------------
    {
        EditorCoordinator c{makeInheritedDoc()};
        CHECK(c.playProject().ok);
        const uint32_t replacesBefore = c.document().replaceCount();
        CHECK(!c.replaceProject(ProjectDocument{makeReplacementDoc()}).ok);
        CHECK(c.isPlaying());
        CHECK(c.document().replaceCount() == replacesBefore);
    }

    // -- Authoring commands and undo are blocked while Play is running --------
    {
        EditorCoordinator c{makeInheritedDoc()};
        CHECK(c.execute(SetEntityTransformCommand{kSceneA, kHero, {25.f, 20.f}}).ok);
        CHECK(c.canUndo());
        const uint64_t revisionBefore = c.document().revision();
        const std::size_t undoBefore = c.undoSize();

        CHECK(c.playProject().ok);
        CHECK(c.isPlaying());
        CHECK(!c.execute(SetEntityTransformCommand{kSceneA, kHero, {99.f, 20.f}}).ok);
        CHECK(!c.execute(SetInstanceSpriteOverrideCommand{
            kSceneA, kHero, SpriteRendererOverride{}}).ok);
        CHECK(!c.undo().ok);
        CHECK(c.document().revision() == revisionBefore);
        CHECK(c.undoSize() == undoBefore);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.x == 25.f);
    }

    // -- A blocked authoring command only emits an intentional Console warning -
    {
        EditorCoordinator c{makeInheritedDoc()};
        CHECK(c.playProject().ok);
        c.consumeInvalidations(); // discard Start Play Toolbar | Viewport | Console
        const std::size_t logBefore = c.consoleLog().size();
        const uint64_t revisionBefore = c.document().revision();

        CHECK(!c.execute(SetEntityTransformCommand{kSceneA, kHero, {99.f, 20.f}}).ok);
        CHECK(c.document().revision() == revisionBefore);
        CHECK(c.consoleLog().size() == logBefore + 1);
        CHECK(c.consoleLog().back().level == ConsoleMessage::Level::Warning);
        CHECK(c.consumeInvalidations() == EditorInvalidation::Console);
    }

    // -- A rejected Intent is just as visible as a rejected Command: every
    // apply(SomeIntent) overload funnels through finishIntent(), which logs a
    // Console warning on failure. Unlike execute(), an Intent's return value
    // is fire-and-forget at every UI call site, so this is the only place a
    // rejected intent is ever surfaced. --
    {
        EditorCoordinator c{makeInheritedDoc()};
        c.consumeInvalidations();
        const std::size_t logBefore = c.consoleLog().size();

        CHECK(!c.apply(SelectSceneIntent{"no-such-scene"}).ok);
        CHECK(c.consoleLog().size() == logBefore + 1);
        CHECK(c.consoleLog().back().level == ConsoleMessage::Level::Warning);
        CHECK(c.consumeInvalidations() == EditorInvalidation::Console);

        // A successful apply() logs nothing new.
        const std::size_t logAfterFailure = c.consoleLog().size();
        CHECK(c.apply(SelectSceneIntent{kSceneA}).ok);
        CHECK(c.consoleLog().size() == logAfterFailure);
    }

    // -- Workspace intents remain allowed during Play and do not retarget Play -
    {
        EditorCoordinator c{makeInheritedDoc()};
        CHECK(c.execute(CreateEntityCommand{kSceneB, 100, "Enemy", "Enemy B", {5.f, 6.f}}).ok);
        CHECK(c.playProject().ok);
        const SceneFrameSnapshot playBefore = collectSceneFrameSnapshot(*c.playSession());
        CHECK(playBefore.sceneId == kSceneA);
        CHECK(playBefore.sprites.size() == 1);

        CHECK(c.apply(SelectSceneIntent{kSceneB}).ok);
        CHECK(c.state().activeSceneId == kSceneB);
        CHECK(c.playSession()->sceneId() == kSceneA);
        const SceneFrameSnapshot playAfterIntent = collectSceneFrameSnapshot(*c.playSession());
        CHECK(playAfterIntent.sceneId == kSceneA);
        CHECK(playAfterIntent.sprites.size() == 1);

        CHECK(c.apply(SelectEntityIntent{100}).ok);
        CHECK(c.selection().primaryEntity == 100);
        const SceneFrameSnapshot playAfterSelection = collectSceneFrameSnapshot(*c.playSession());
        CHECK(playAfterSelection.sceneId == kSceneA);
        CHECK(!playAfterSelection.entities.empty());
        CHECK(!playAfterSelection.entities[0].selected);

        CHECK(c.stopPlaying().ok);
        CHECK(!c.isPlaying());
        const SceneFrameSnapshot editAfterStop =
            collectSceneFrameSnapshot(c.document(), c.state().activeSceneId,
                                      c.selection().primaryEntity);
        CHECK(editAfterStop.sceneId == kSceneB);
        CHECK(editAfterStop.entities.size() == 1);
        CHECK(editAfterStop.entities[0].entityId == 100);
        CHECK(editAfterStop.entities[0].selected);
        CHECK(editAfterStop.sprites.empty());
    }

    // == TopDownController / PlatformerController / AABB collision runtime ==
    // RU-03 (D-01): these characterized PlaySession's own hand-written
    // kinematic movement, AABB collision resolution, platformer physics, and
    // LinearMover resolution - all removed. GameplaySession's real World/
    // Physics now own this, driven only through Logic Board/Script authoring
    // (never hardcoded host input), and are already characterized by
    // runtime-cpp's own test suite (e.g.
    // gameplay-tick-order-characterization-test.cpp) - not re-verified here.

    // == Undo availability + toolbar refresh ==================================

    // -- A successful command enables Undo and refreshes the toolbar ----------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(!c.canUndo());
        c.consumeInvalidations();
        const EditorOperationResult moved =
            c.execute(SetEntityTransformCommand{kSceneA, kHero, Vec2{99.f, 20.f}});
        CHECK(moved.ok);
        CHECK(c.canUndo());
        // Toolbar is invalidated so the Undo button can re-derive its state.
        CHECK(has(c.consumeInvalidations(), EditorInvalidation::Toolbar));
    }

    // -- Undo restores the document, refreshes toolbar, and re-disables -------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(SetEntityTransformCommand{kSceneA, kHero, Vec2{99.f, 20.f}}).ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.x == 99.f);
        c.consumeInvalidations();

        const EditorOperationResult undone = c.undo();
        CHECK(undone.ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.x == 10.f);
        const EditorInvalidation inv = c.consumeInvalidations();
        CHECK(has(inv, EditorInvalidation::Toolbar));
        CHECK(has(inv, EditorInvalidation::Inspector));   // the edited field re-reads
        CHECK(!c.canUndo());                              // back to empty history
    }

    // -- replaceProject clears the history (Undo disabled) -------------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(SetEntityTransformCommand{kSceneA, kHero, Vec2{99.f, 20.f}}).ok);
        CHECK(c.canUndo());
        CHECK(c.replaceProject(ProjectDocument{makeReplacementDoc()}).ok);
        CHECK(!c.canUndo());
    }

    // -- Play disables Undo as affordance; Stop restores the existing history -
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(SetEntityTransformCommand{kSceneA, kHero, Vec2{99.f, 20.f}}).ok);
        CHECK(c.canUndo());
        CHECK(c.playProject().ok);
        CHECK(c.isPlaying());
        CHECK(!c.undo().ok);                 // coordinator guard rejects during Play
        CHECK(c.canUndo());                  // history survives Play
        CHECK(c.stopPlaying().ok);
        CHECK(!c.isPlaying());
        CHECK(c.canUndo());                  // derived state returns after Stop
    }

    // == Redo (history walk) ==================================================

    // -- (1) Execute -> Undo -> Redo restores the document exactly ------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(SetEntityTransformCommand{kSceneA, kHero, Vec2{99.f, 20.f}}).ok);
        CHECK(c.undo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.x == 10.f);
        CHECK(c.canRedo());
        CHECK(c.redo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.x == 99.f);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.y == 20.f);
        CHECK(!c.canRedo());
        CHECK(c.canUndo());
    }

    // -- (2) Redo restores the entry's recorded revision ----------------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(SetEntityTransformCommand{kSceneA, kHero, Vec2{99.f, 20.f}}).ok);
        const uint64_t revAfter = c.document().revision();
        CHECK(c.undo().ok);
        CHECK(c.document().revision() != revAfter);     // moved off the post-state
        CHECK(c.redo().ok);
        CHECK(c.document().revision() == revAfter);      // exactly the recorded id
    }

    // -- (3) Undo/Redo cross savedRevision (dirty correctness) ---------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(SetEntityTransformCommand{kSceneA, kHero, Vec2{99.f, 20.f}}).ok);
        CHECK(c.markProjectSaved().ok);
        CHECK(!c.document().isDirty());
        CHECK(c.undo().ok);
        CHECK(c.document().isDirty());                   // undone away from saved
        CHECK(c.redo().ok);
        CHECK(!c.document().isDirty());                  // redo back to the saved state
    }

    // -- (3b) Save taken at the post-undo state: redo dirties, undo cleans ----
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(SetEntityTransformCommand{kSceneA, kHero, Vec2{99.f, 20.f}}).ok);
        CHECK(c.undo().ok);                              // back to state A
        CHECK(c.markProjectSaved().ok);                 // save AT the post-undo state
        CHECK(!c.document().isDirty());
        CHECK(c.redo().ok);                             // to state B
        CHECK(c.document().isDirty());                  // away from the saved state
        CHECK(c.undo().ok);                             // back to state A
        CHECK(!c.document().isDirty());                 // saved state again
    }

    // -- (4) A new command after Undo discards the redo branch ----------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(SetEntityTransformCommand{kSceneA, kHero, Vec2{99.f, 20.f}}).ok);
        CHECK(c.undo().ok);
        CHECK(c.canRedo());
        CHECK(c.execute(SetEntityTransformCommand{kSceneA, kHero, Vec2{50.f, 20.f}}).ok);
        CHECK(!c.canRedo());                             // B is no longer redoable
        CHECK(!c.redo().ok);
    }

    // -- (5) replaceProject clears redo; (6) Save keeps it --------------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(SetEntityTransformCommand{kSceneA, kHero, Vec2{99.f, 20.f}}).ok);
        CHECK(c.undo().ok);
        CHECK(c.canRedo());
        CHECK(c.markProjectSaved().ok);
        CHECK(c.canRedo());                              // (6) save does not clear redo
        CHECK(c.replaceProject(ProjectDocument{makeReplacementDoc()}).ok);
        CHECK(!c.canRedo());                             // (5) replace clears redo
    }

    // -- (7) Redo with no entry fails without effect --------------------------
    {
        EditorCoordinator c{makeDoc()};
        const uint64_t revisionBefore = c.document().revision();
        CHECK(!c.redo().ok);
        CHECK(c.document().revision() == revisionBefore);
    }

    // -- (8) Redo is rejected during Play ------------------------------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(SetEntityTransformCommand{kSceneA, kHero, Vec2{99.f, 20.f}}).ok);
        CHECK(c.undo().ok);
        CHECK(c.playProject().ok);
        CHECK(!c.redo().ok);                             // coordinator guard
        CHECK(c.canRedo());                              // entry preserved
        CHECK(c.stopPlaying().ok);
        CHECK(c.canRedo());
    }

    // -- (10) Redo's invalidation matches the original command ----------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(SetEntityTransformCommand{kSceneA, kHero, Vec2{99.f, 20.f}}).ok);
        CHECK(c.undo().ok);
        c.consumeInvalidations();
        CHECK(c.redo().ok);
        const EditorInvalidation inv = c.consumeInvalidations();
        CHECK(has(inv, EditorInvalidation::Inspector));
        CHECK(has(inv, EditorInvalidation::Viewport));
        CHECK(has(inv, EditorInvalidation::Toolbar));
    }

    // == Unsaved-changes guard (decision matrix) ==============================
    {
        // A clean project proceeds immediately, whatever the (unused) choice.
        CHECK(resolveUnsavedGuard(false, UnsavedChoice::Cancel, false) == GuardOutcome::Proceed);
        CHECK(resolveUnsavedGuard(false, UnsavedChoice::Save, false) == GuardOutcome::Proceed);

        // Dirty: Cancel aborts; Discard proceeds.
        CHECK(resolveUnsavedGuard(true, UnsavedChoice::Cancel, false) == GuardOutcome::Abort);
        CHECK(resolveUnsavedGuard(true, UnsavedChoice::Discard, false) == GuardOutcome::Proceed);

        // Dirty + Save: proceed only when the save succeeded.
        CHECK(resolveUnsavedGuard(true, UnsavedChoice::Save, true) == GuardOutcome::Proceed);
        CHECK(resolveUnsavedGuard(true, UnsavedChoice::Save, false) == GuardOutcome::Abort);
    }

    // == P0-06 pending-edit classification and ordering =======================
    {
        CHECK(classifyPendingEdit("commit-transform-position-x", "12.5").status
              == PendingEditStatus::Resolved);
        CHECK(classifyPendingEdit("commit-project-name", "Arcade").status
              == PendingEditStatus::Resolved);
        CHECK(classifyPendingEdit("commit-console-filter", "").status
              == PendingEditStatus::Resolved); // an empty filter is a valid clear
        CHECK(classifyPendingEdit("select-entity", "-").status
              == PendingEditStatus::Resolved); // only commit fields participate

        for (const std::string& text : {"", "-", "+", ".", "-.", "+.",
                                        "1e", "1E", "1e+", "1e-", "12."}) {
            const PendingEditResult result = classifyPendingEdit("commit-transform-position-x", text);
            CHECK(result.status == PendingEditStatus::Incomplete);
            CHECK(!result.message.empty());
        }

        for (const std::string& text : {"NaN", "nan", "inf", "infinity",
                                        "12px", "1e9999", "abc", "1e."}) {
            const PendingEditResult result = classifyPendingEdit("commit-transform-position-y", text);
            CHECK(result.status == PendingEditStatus::Invalid);
            CHECK(!result.message.empty());
        }
        CHECK(classifyPendingEdit("commit-project-name", "").status
              == PendingEditStatus::Invalid);
        CHECK(classifyPendingEdit("commit-layer-rename", "").status
              == PendingEditStatus::Invalid);
    }

    // A valid focused value commits first, making the document dirty before
    // Cancel/Discard/Save is decided. Invalid/incomplete buffers generate no
    // command, so revision and dirty state stay exact.
    {
        EditorCoordinator c{makeDoc()};
        const uint64_t revisionBefore = c.document().revision();
        const PendingEditResult incomplete =
            classifyPendingEdit("commit-transform-position-x", "1e");
        if (incomplete.resolved()) {
            commitInspectorTransformField(c, kHero, InspectorTransformField::PositionX, "1e");
        }
        CHECK(c.document().revision() == revisionBefore);
        CHECK(!c.document().isDirty());

        const PendingEditResult valid = classifyPendingEdit("commit-transform-position-x", "33");
        CHECK(valid.resolved());
        if (valid.resolved()) {
            CHECK(commitInspectorTransformField(
                c, kHero, InspectorTransformField::PositionX, "33").ok);
        }
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.x == 33.f);
        CHECK(c.document().isDirty());
        CHECK(resolveUnsavedGuard(c.document().isDirty(), UnsavedChoice::Cancel, false)
              == GuardOutcome::Abort);
        CHECK(resolveUnsavedGuard(c.document().isDirty(), UnsavedChoice::Discard, false)
              == GuardOutcome::Proceed);
    }

    // Escape restores the original field value. Resolving that restored buffer
    // is a no-op; changing selection only happens after a real pending commit.
    {
        EditorCoordinator escaped{makeDoc()};
        const uint64_t revisionBefore = escaped.document().revision();
        CHECK(classifyPendingEdit("commit-transform-position-x", "10").resolved());
        CHECK(!pendingEditNeedsCommit("commit-transform-position-x", "10", "10"));
        if (pendingEditNeedsCommit("commit-transform-position-x", "10", "10")) {
            commitInspectorTransformField(
                escaped, kHero, InspectorTransformField::PositionX, "10");
        }
        CHECK(escaped.document().revision() == revisionBefore);
        CHECK(!escaped.document().isDirty());
        CHECK(pendingEditNeedsCommit("commit-layer-rename", "Gameplay", "Gameplay"));

        EditorCoordinator navigated{makeDoc()};
        CHECK(commitInspectorTransformField(
            navigated, kHero, InspectorTransformField::PositionX, "44").ok);
        navigated.apply(SelectSceneIntent{kSceneB});
        CHECK(navigated.state().activeSceneId == kSceneB);
        CHECK(navigated.document().findInstanceInScene(kSceneA, kHero)->transform.position.x
              == 44.f);
    }

    // == LinearMover editing + persistence ====================================

    // -- Add resolves a default mover and invalidates Inspector only ----------
    {
        EditorCoordinator c{makeInheritedDoc()};
        c.consumeInvalidations();
        const EditorOperationResult r = c.execute(AddLinearMoverCommand{"Hero"});
        CHECK(r.ok);
        CHECK(r.invalidation == EditorInvalidation::Inspector);   // no edit-viewport visual
        const EntityDef& hero = c.document().data().objectTypes.at("Hero");
        CHECK(hero.linearMover.has_value());
        CHECK(hero.linearMover->speed == 300.f);                  // component default
    }

    // -- Set speed / direction, then exact undo ------------------------------
    {
        EditorCoordinator c{makeMoverDoc()};                      // Hero mover (3,0) @ 100
        CHECK(c.execute(SetLinearMoverSpeedCommand{"Hero", 250.f}).ok);
        CHECK(c.document().data().objectTypes.at("Hero").linearMover->speed == 250.f);
        CHECK(c.execute(SetLinearMoverDirectionCommand{"Hero", Vec2{0.f, 1.f}}).ok);
        const auto& m = *c.document().data().objectTypes.at("Hero").linearMover;
        CHECK(m.directionX == 0.f);
        CHECK(m.directionY == 1.f);
        CHECK(c.undo().ok);                                       // undo direction
        CHECK(c.document().data().objectTypes.at("Hero").linearMover->directionX == 3.f);
        CHECK(c.undo().ok);                                       // undo speed
        CHECK(c.document().data().objectTypes.at("Hero").linearMover->speed == 100.f);
    }

    // -- Invalid edits are rejected and mutate nothing -----------------------
    {
        EditorCoordinator c{makeMoverDoc()};
        const uint64_t revisionBefore = c.document().revision();
        CHECK(!c.execute(SetLinearMoverSpeedCommand{"Hero", -5.f}).ok);
        CHECK(!c.execute(SetLinearMoverDirectionCommand{"Hero",
            Vec2{std::numeric_limits<float>::infinity(), 0.f}}).ok);
        CHECK(c.document().revision() == revisionBefore);
        CHECK(c.document().data().objectTypes.at("Hero").linearMover->speed == 100.f);
    }

    // -- Inspector action is a no-op without a selected object type -----------
    {
        EditorCoordinator c{makeInheritedDoc()};                  // nothing selected
        CHECK(!addLinearMover(c).ok);
    }

    // -- Mover survives save/load; _paused is not persisted ------------------
    {
        EditorCoordinator c{makeMoverDoc()};
        const std::filesystem::path path = testTempDir() / "mover.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);
        EditorCoordinator reloaded{ProjectDoc{}};
        CHECK(loadProjectFromFile(reloaded, path).ok);
        const auto& m = reloaded.document().data().objectTypes.at("Hero").linearMover;
        CHECK(m.has_value());
        CHECK(m->directionX == 3.f);
        CHECK(m->speed == 100.f);
        CHECK(m->_paused == false);
    }

    // == AutoDestroy editing, persistence, and isolated Play =================

    // -- Add uses an editor-safe one-second default; all mutations are
    // command-backed, exact under Undo/Redo, and never need viewport redraw. --
    {
        EditorCoordinator c{makeInheritedDoc()};
        c.consumeInvalidations();
        const EditorOperationResult added = c.execute(AddAutoDestroyCommand{"Hero"});
        CHECK(added.ok);
        CHECK(added.invalidation == EditorInvalidation::Inspector);
        CHECK(c.document().data().objectTypes.at("Hero").autoDestroy.has_value());
        CHECK(c.document().data().objectTypes.at("Hero").autoDestroy->lifespan == 1.f);

        CHECK(c.execute(SetAutoDestroyLifespanCommand{"Hero", 2.5f}).ok);
        CHECK(c.document().data().objectTypes.at("Hero").autoDestroy->lifespan == 2.5f);
        CHECK(c.undo().ok);
        CHECK(c.document().data().objectTypes.at("Hero").autoDestroy->lifespan == 1.f);
        CHECK(c.redo().ok);
        CHECK(c.document().data().objectTypes.at("Hero").autoDestroy->lifespan == 2.5f);

        CHECK(c.execute(RemoveAutoDestroyCommand{"Hero"}).ok);
        CHECK(!c.document().data().objectTypes.at("Hero").autoDestroy.has_value());
        CHECK(c.undo().ok);
        CHECK(c.document().data().objectTypes.at("Hero").autoDestroy.has_value());
        CHECK(c.document().data().objectTypes.at("Hero").autoDestroy->lifespan == 2.5f);
    }

    // -- Zero explicitly disables expiry. Negative/non-finite lifetimes never
    // mutate authoring state, and no-selection helpers fail visibly. ----------
    {
        EditorCoordinator c{makeInheritedDoc()};
        CHECK(c.execute(AddAutoDestroyCommand{"Hero"}).ok);
        CHECK(c.execute(SetAutoDestroyLifespanCommand{"Hero", 0.f}).ok);
        const uint64_t revision = c.document().revision();
        CHECK(!c.execute(SetAutoDestroyLifespanCommand{"Hero", -0.01f}).ok);
        CHECK(!c.execute(SetAutoDestroyLifespanCommand{
            "Hero", std::numeric_limits<float>::infinity()}).ok);
        CHECK(c.document().revision() == revision);
        CHECK(c.document().data().objectTypes.at("Hero").autoDestroy->lifespan == 0.f);

        EditorCoordinator noSelection{makeInheritedDoc()};
        CHECK(!addAutoDestroy(noSelection).ok);
    }

    // -- Serialization preserves authored lifetime only: the runtime elapsed
    // counter is reset when a project is read back. ---------------------------
    {
        ProjectDoc doc = makeInheritedDoc();
        AutoDestroyComponent component;
        component.lifespan = 3.25f;
        component._timeAlive = 99.f;
        doc.objectTypes.at("Hero").autoDestroy = component;
        const SerializeResult saved = ProjectSerializer::serialize(ProjectDocument{doc});
        CHECK(saved.ok);
        const DeserializeResult loaded = ProjectSerializer::deserialize(saved.value);
        CHECK(loaded.ok);
        const AutoDestroyComponent& restored =
            *loaded.value.data().objectTypes.at("Hero").autoDestroy;
        CHECK(restored.lifespan == 3.25f);
        CHECK(restored._timeAlive == 0.f);
        CHECK(!ProjectSerializer::deserialize(
            R"({"activeSceneId":"s","scenes":[{"id":"s","instances":[{"id":1,"objectTypeId":"Hero","instanceName":"Hero"}]}],"objectTypes":[{"id":"Hero","autoDestroy":{"lifespan":-1}}]})").ok);
    }

    // -- The runtime timer expires only in the isolated PlaySession. Stopping
    // Play leaves the Object Type and its authored lifetime unchanged. --------
    {
        EditorCoordinator c{makeInheritedDoc()};
        CHECK(c.execute(AddAutoDestroyCommand{"Hero"}).ok);
        CHECK(c.execute(SetAutoDestroyLifespanCommand{"Hero", 0.01f}).ok);
        const uint64_t revision = c.document().revision();
        std::optional<PlaySession> session = PlaySession::startProject(c.document());
        CHECK(session.has_value());
        CHECK(collectSceneFrameSnapshot(*session).entities.size() == 1);
        session->tick(RuntimeInputSnapshot{}, 0.02f);
        CHECK(collectSceneFrameSnapshot(*session).entities.empty());
        CHECK(c.document().findObjectType("Hero")->autoDestroy.has_value());
        CHECK(c.document().findObjectType("Hero")->autoDestroy->lifespan == 0.01f);
        CHECK(c.document().revision() == revision);
    }

    // == CameraTarget: scene-instance authority, Undo, persistence, Play ====
    {
        // ADR-0018: without a CameraTarget, Play starts at cameraStart+viewport/2
        // (defaults equal world centre when viewport == world and cameraStart is 0).
        EditorCoordinator noTarget{makeInheritedDoc()};
        std::optional<PlaySession> centredSession = PlaySession::startProject(noTarget.document());
        CHECK(centredSession.has_value());
        const Vec2 centred = centredSession->cameraCenter();
        const SceneDef* centredScene = noTarget.document().findScene(kSceneA);
        CHECK(centredScene != nullptr);
        CHECK(centredSession->scene().viewportSize.x == centredScene->viewportSize.x);
        CHECK(centredSession->scene().cameraStart.x == centredScene->cameraStart.x);
        const Vec2 expectedNoTarget{
            centredScene->cameraStart.x + centredScene->viewportSize.x * 0.5f,
            centredScene->cameraStart.y + centredScene->viewportSize.y * 0.5f,
        };
        CHECK(centred.x == expectedNoTarget.x);
        CHECK(centred.y == expectedNoTarget.y);

        EditorCoordinator c{makeInheritedDoc()};
        c.apply(SelectEntityIntent{kHero});
        c.consumeInvalidations();
        const EditorOperationResult added = c.execute(AddCameraTargetCommand{kSceneA, kHero});
        CHECK(added.ok);
        CHECK(added.invalidation == EditorInvalidation::Inspector);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->cameraTarget.has_value());
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->cameraTarget->followSpeed == 8.f);

        CHECK(c.execute(SetCameraTargetOffsetCommand{kSceneA, kHero, Vec2{3.f, -4.f}}).ok);
        CHECK(c.execute(SetCameraTargetFollowSpeedCommand{kSceneA, kHero, 0.f}).ok);
        const CameraTargetComponent& configured =
            *c.document().findInstanceInScene(kSceneA, kHero)->cameraTarget;
        CHECK(configured.offsetX == 3.f);
        CHECK(configured.offsetY == -4.f);
        CHECK(configured.followSpeed == 0.f);

        CHECK(c.undo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->cameraTarget->followSpeed == 8.f);
        CHECK(c.redo().ok);

        const SerializeResult saved = ProjectSerializer::serialize(c.document());
        CHECK(saved.ok);
        const DeserializeResult loaded = ProjectSerializer::deserialize(saved.value);
        CHECK(loaded.ok);
        const CameraTargetComponent& restored =
            *loaded.value.findInstanceInScene(kSceneA, kHero)->cameraTarget;
        CHECK(restored.offsetX == 3.f && restored.offsetY == -4.f && restored.followSpeed == 0.f);

        // Enlarge world past Game View so clamp allows an interior target (ADR-0018).
        CHECK(c.execute(SetSceneSizeCommand{kSceneA, {1024.f, 640.f}}).ok);
        CHECK(c.execute(SetSceneViewportSizeCommand{kSceneA, {512.f, 320.f}}).ok);
        // Place the hero well inside the clamp band (half-viewport from each edge).
        CHECK(c.execute(SetEntityTransformCommand{kSceneA, kHero, {400.f, 300.f}}).ok);

        std::optional<PlaySession> session = PlaySession::startProject(c.document());
        CHECK(session.has_value());
        session->tick(RuntimeInputSnapshot{}, 0.016f);
        const Vec2 camera = session->cameraCenter();
        // transform (400,300) + offset (3,-4)
        CHECK(camera.x == 403.f && camera.y == 296.f);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->cameraTarget->offsetX == 3.f);

        CHECK(c.execute(RemoveCameraTargetCommand{kSceneA, kHero}).ok);
        CHECK(c.undo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->cameraTarget.has_value());
    }

    // -- ADR-0018: viewportSize + cameraStart Save/Load round-trip ------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(SetSceneSizeCommand{kSceneA, {1024.f, 320.f}}).ok);
        CHECK(c.execute(SetSceneViewportSizeCommand{kSceneA, {512.f, 320.f}}).ok);
        ProjectDoc doc = c.document().data();
        doc.scenes.at(kSceneA).cameraStart = {64.f, 16.f};
        doc.scenes.at(kSceneA).viewportSize = {512.f, 320.f};
        doc.scenes.at(kSceneA).worldSize = {1024.f, 640.f};
        const SerializeResult saved = ProjectSerializer::serialize(ProjectDocument{doc});
        CHECK(saved.ok);
        CHECK(saved.value.find("\"cameraStart\"") != std::string::npos);
        CHECK(saved.value.find("\"viewportSize\"") != std::string::npos);
        const DeserializeResult loaded = ProjectSerializer::deserialize(saved.value);
        CHECK(loaded.ok);
        const SceneDef* restored = loaded.value.findScene(kSceneA);
        CHECK(restored != nullptr);
        CHECK(restored->worldSize.x == 1024.f && restored->worldSize.y == 640.f);
        CHECK(restored->viewportSize.x == 512.f);
        CHECK(restored->cameraStart.x == 64.f && restored->cameraStart.y == 16.f);

        std::optional<PlaySession> session = PlaySession::startProject(loaded.value);
        CHECK(session.has_value());
        CHECK(session->scene().viewportSize.x == 512.f);
        CHECK(session->scene().cameraStart.x == 64.f);
        const Vec2 cam = session->cameraCenter();
        CHECK(cam.x == 64.f + 256.f);
        CHECK(cam.y == 16.f + 160.f);
    }

    // A target is not type-owned and there is never a second automatic target
    // in one scene. Invalid authoring data is rejected at the load boundary.
    {
        ProjectDoc doc = makeInheritedDoc();
        SceneInstanceDef second = doc.scenes.at(kSceneA).instances.front();
        second.id = 99;
        second.instanceName = "Second";
        doc.scenes.at(kSceneA).instances.push_back(second);
        EditorCoordinator c{std::move(doc)};
        CHECK(c.execute(AddCameraTargetCommand{kSceneA, kHero}).ok);
        CHECK(!c.execute(AddCameraTargetCommand{kSceneA, 99}).ok);
        CHECK(!c.execute(SetCameraTargetFollowSpeedCommand{
            kSceneA, kHero, std::numeric_limits<float>::infinity()}).ok);
        CHECK(!ProjectSerializer::deserialize(
            R"({"activeSceneId":"s","scenes":[{"id":"s","instances":[{"id":1,"objectTypeId":"Hero","instanceName":"Hero"}]}],"objectTypes":[{"id":"Hero","cameraTarget":{"followSpeed":1}}]})").ok);

        ProjectDoc duplicate = makeInheritedDoc();
        SceneInstanceDef duplicateInstance = duplicate.scenes.at(kSceneA).instances.front();
        duplicateInstance.id = 100;
        duplicateInstance.cameraTarget = CameraTargetComponent{};
        duplicate.scenes.at(kSceneA).instances.front().cameraTarget = CameraTargetComponent{};
        duplicate.scenes.at(kSceneA).instances.push_back(std::move(duplicateInstance));
        const SerializeResult duplicateSaved = ProjectSerializer::serialize(ProjectDocument{duplicate});
        CHECK(duplicateSaved.ok);
        CHECK(!ProjectSerializer::deserialize(duplicateSaved.value).ok);
    }

    // ===== PlatformerController authoring ====================================
    {
        // Add creates the component with the editor's recommended starting values.
        {
            EditorCoordinator c{makeInheritedDoc()};
            CHECK(c.execute(AddPlatformerControllerCommand{"Hero"}).ok);
            const auto& pc = c.document().data().objectTypes.at("Hero").platformerController;
            CHECK(pc.has_value());
            CHECK(pc->maxSpeed == 180.f);
            CHECK(pc->jumpForce == 420.f);
            CHECK(pc->customGravity == 1200.f);
        }

        // One movement writer: incompatible drivers are rejected both ways.
        {
            EditorCoordinator c{makeInheritedDoc()};
            CHECK(c.execute(AddTopDownControllerCommand{"Hero"}).ok);
            CHECK(!c.execute(AddPlatformerControllerCommand{"Hero"}).ok);   // topdown present
            CHECK(!c.execute(AddLinearMoverCommand{"Hero"}).ok);
            CHECK(c.execute(RemoveTopDownControllerCommand{"Hero"}).ok);
            CHECK(c.execute(AddPlatformerControllerCommand{"Hero"}).ok);    // now free
            CHECK(!c.execute(AddTopDownControllerCommand{"Hero"}).ok);      // platformer present
            CHECK(!c.execute(AddLinearMoverCommand{"Hero"}).ok);
        }

        // Set values: validation, undo/redo.
        {
            EditorCoordinator c{makeInheritedDoc()};
            CHECK(c.execute(AddPlatformerControllerCommand{"Hero"}).ok);
            CHECK(!c.execute(SetPlatformerValueCommand{"Hero", PlatformerField::Gravity, -1.f}).ok);
            CHECK(!c.execute(SetPlatformerValueCommand{"Hero", PlatformerField::CoyoteTime, -0.1f}).ok);
            CHECK(c.execute(SetPlatformerValueCommand{"Hero", PlatformerField::JumpSpeed, 500.f}).ok);
            CHECK(c.document().data().objectTypes.at("Hero").platformerController->jumpForce == 500.f);
            CHECK(c.undo().ok);
            CHECK(c.document().data().objectTypes.at("Hero").platformerController->jumpForce == 420.f);
            CHECK(c.redo().ok);
            CHECK(c.document().data().objectTypes.at("Hero").platformerController->jumpForce == 500.f);

            CHECK(c.execute(SetPlatformerValueCommand{"Hero", PlatformerField::CoyoteTime, 0.25f}).ok);
            CHECK(c.execute(SetPlatformerValueCommand{"Hero", PlatformerField::JumpBuffer, 0.2f}).ok);
            CHECK(c.execute(SetPlatformerValueCommand{"Hero", PlatformerField::ClimbSpeed, 90.f}).ok);
            CHECK(c.document().data().objectTypes.at("Hero").platformerController->coyoteTime == 0.25f);
            CHECK(c.document().data().objectTypes.at("Hero").platformerController->jumpBuffer == 0.2f);
            CHECK(c.document().data().objectTypes.at("Hero").platformerController->climbSpeed == 90.f);
            CHECK(c.undo().ok);
            CHECK(c.document().data().objectTypes.at("Hero").platformerController->climbSpeed == 120.f);
        }

        // Save/reload preserves the authored subset.
        {
            EditorCoordinator c{makeInheritedDoc()};
            CHECK(c.execute(AddPlatformerControllerCommand{"Hero"}).ok);
            CHECK(c.execute(SetPlatformerValueCommand{"Hero", PlatformerField::MoveSpeed, 90.f}).ok);
            CHECK(c.execute(SetPlatformerValueCommand{"Hero", PlatformerField::CoyoteTime, 0.3f}).ok);
            CHECK(c.execute(SetPlatformerValueCommand{"Hero", PlatformerField::JumpBuffer, 0.15f}).ok);
            CHECK(c.execute(SetPlatformerValueCommand{"Hero", PlatformerField::ClimbSpeed, 80.f}).ok);
            const std::filesystem::path path = testTempDir() / "platformer.artcade-project";
            CHECK(saveProjectToFile(c, path).ok);
            EditorCoordinator reloaded{ProjectDoc{}};
            CHECK(loadProjectFromFile(reloaded, path).ok);
            const auto& pc = reloaded.document().data().objectTypes.at("Hero").platformerController;
            CHECK(pc.has_value());
            CHECK(pc->maxSpeed == 90.f);
            CHECK(pc->jumpForce == 420.f);
            CHECK(pc->customGravity == 1200.f);
            CHECK(pc->coyoteTime == 0.3f);
            CHECK(pc->jumpBuffer == 0.15f);
            CHECK(pc->climbSpeed == 80.f);
        }
    }

    // -- save/load preserves the component; Add/Remove/SetSpeed undo+redo -----
    {
        EditorCoordinator c{makeTopDownDoc(140.f)};
        CHECK(c.execute(
            SetTopDownControllerAccelerationCommand{"Hero", 700.f}).ok);
        CHECK(c.execute(SetTopDownControllerFrictionCommand{"Hero", 800.f}).ok);
        CHECK(c.execute(
            SetTopDownControllerFourDirectionsCommand{"Hero", true}).ok);
        const std::filesystem::path path = testTempDir() / "topdown.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);
        EditorCoordinator reloaded{ProjectDoc{}};
        CHECK(loadProjectFromFile(reloaded, path).ok);
        const auto& tdc = reloaded.document().data().objectTypes.at("Hero").topDownController;
        CHECK(tdc.has_value());
        CHECK(tdc->maxSpeed == 140.f);
        CHECK(tdc->acceleration == 700.f);
        CHECK(tdc->friction == 800.f);
        CHECK(tdc->fourDirections);
    }
    {
        EditorCoordinator c{makeInheritedDoc()};
        CHECK(c.execute(AddTopDownControllerCommand{"Hero"}).ok);
        CHECK(c.execute(SetTopDownControllerSpeedCommand{"Hero", 250.f}).ok);
        CHECK(c.document().data().objectTypes.at("Hero").topDownController->maxSpeed == 250.f);
        CHECK(c.execute(
            SetTopDownControllerAccelerationCommand{"Hero", 900.f}).ok);
        CHECK(c.execute(SetTopDownControllerFrictionCommand{"Hero", 1200.f}).ok);
        CHECK(c.execute(
            SetTopDownControllerFourDirectionsCommand{"Hero", true}).ok);
        const TopDownControllerComponent& edited =
            *c.document().data().objectTypes.at("Hero").topDownController;
        CHECK(edited.acceleration == 900.f);
        CHECK(edited.friction == 1200.f);
        CHECK(edited.fourDirections);
        CHECK(c.undo().ok);   // undo fourDirections
        CHECK(!c.document().data().objectTypes.at("Hero")
                   .topDownController->fourDirections);
        CHECK(c.undo().ok);   // undo friction
        CHECK(c.document().data().objectTypes.at("Hero")
                  .topDownController->friction == 2200.f);
        CHECK(c.undo().ok);   // undo acceleration
        CHECK(c.document().data().objectTypes.at("Hero")
                  .topDownController->acceleration == 1600.f);
        CHECK(c.undo().ok);   // undo speed
        CHECK(c.document().data().objectTypes.at("Hero").topDownController->maxSpeed == 260.f);
        CHECK(c.undo().ok);   // undo add
        CHECK(!c.document().data().objectTypes.at("Hero").topDownController.has_value());
        CHECK(c.redo().ok);   // redo add
        CHECK(c.document().data().objectTypes.at("Hero").topDownController.has_value());
        CHECK(c.redo().ok);   // redo speed
        CHECK(c.document().data().objectTypes.at("Hero").topDownController->maxSpeed == 250.f);
    }

    // -- Invalid speed rejected; Add invalidates Inspector only --------------
    {
        EditorCoordinator c{makeTopDownDoc(100.f)};
        const uint64_t revisionBefore = c.document().revision();
        CHECK(!c.execute(SetTopDownControllerSpeedCommand{"Hero", -1.f}).ok);
        CHECK(!c.execute(
            SetTopDownControllerAccelerationCommand{"Hero", -1.f}).ok);
        CHECK(!c.execute(SetTopDownControllerFrictionCommand{
            "Hero", std::numeric_limits<float>::infinity()}).ok);
        CHECK(c.document().revision() == revisionBefore);
        EditorCoordinator c2{makeInheritedDoc()};
        c2.consumeInvalidations();
        const EditorOperationResult r = c2.execute(AddTopDownControllerCommand{"Hero"});
        CHECK(r.ok);
        CHECK(r.invalidation == EditorInvalidation::Inspector);
    }

    // == Image asset catalog (import target) ==================================

    // -- Add: catalog gains the asset; duplicate rejected; undo/redo ----------
    {
        EditorCoordinator c{makeDoc()};
        c.consumeInvalidations();
        const EditorOperationResult r =
            c.execute(AddImageAssetCommand{"img-x", "assets/images/x.png"});
        CHECK(r.ok);
        CHECK(r.invalidation == (EditorInvalidation::Assets | EditorInvalidation::Inspector));
        CHECK(c.document().hasImageAsset("img-x"));
        CHECK(c.document().findImageAsset("img-x")->name == "img-x");
        CHECK(c.document().findImageAsset("img-x")->sourcePath == "assets/images/x.png");

        CHECK(!c.execute(AddImageAssetCommand{"img-x", "assets/images/y.png"}).ok);  // dup
        CHECK(c.undo().ok);
        CHECK(!c.document().hasImageAsset("img-x"));
        CHECK(c.redo().ok);
        CHECK(c.document().hasImageAsset("img-x"));
    }

    // -- Remove: exact undo restores the source path; unknown id fails --------
    {
        EditorCoordinator c{makeSpriteDoc()};   // img-hero -> sprites/hero.ppm
        CHECK(c.document().hasImageAsset("img-hero"));
        CHECK(c.execute(RemoveImageAssetCommand{"img-hero"}).ok);
        CHECK(!c.document().hasImageAsset("img-hero"));
        CHECK(c.undo().ok);
        CHECK(c.document().findImageAsset("img-hero")->sourcePath == "sprites/hero.ppm");
        CHECK(!c.execute(RemoveImageAssetCommand{"nope"}).ok);
    }

    // -- Remove clears the sprite-renderer reference (delete means delete) -----
    {
        EditorCoordinator c{makeInheritedDoc()};
        CHECK(c.document().data().objectTypes.at("Hero")
                  .spriteRenderer->imageAssetId == "img-hero");
        const uint64_t revisionBefore = c.document().revision();
        // Removing the image drops the reference - the entity keeps no dangling id.
        CHECK(c.execute(RemoveImageAssetCommand{"img-hero"}).ok);
        CHECK(c.document().revision() == revisionBefore + 1);
        CHECK(!c.document().hasImageAsset("img-hero"));
        CHECK(c.document().data().objectTypes.at("Hero")
                  .spriteRenderer->imageAssetId.empty());
        // Undo restores the asset AND the exact reference; redo clears it again.
        CHECK(c.undo().ok);
        CHECK(c.document().hasImageAsset("img-hero"));
        CHECK(c.document().data().objectTypes.at("Hero")
                  .spriteRenderer->imageAssetId == "img-hero");
        CHECK(c.redo().ok);
        CHECK(c.document().data().objectTypes.at("Hero")
                  .spriteRenderer->imageAssetId.empty());
    }

    // -- save/load preserves the catalog with the relative path ---------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(AddImageAssetCommand{"img-x", "assets/images/x.png"}).ok);
        const std::filesystem::path path = testTempDir() / "assets.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);
        EditorCoordinator reloaded{ProjectDoc{}};
        CHECK(loadProjectFromFile(reloaded, path).ok);
        const ImageAssetDef* asset = reloaded.document().findImageAsset("img-x");
        CHECK(asset != nullptr);
        CHECK(asset->name == "img-x");
        CHECK(asset->sourcePath == "assets/images/x.png");
    }

    // == Audio + Font catalogs (commands + persistence) =======================
    {
        EditorCoordinator c{makeDoc()};
        c.consumeInvalidations();
        const EditorOperationResult ra =
            c.execute(AddAudioAssetCommand{"sfx", "assets/audio/sfx.wav",
                                           AudioLoadMode::StaticSound});
        CHECK(ra.ok);
        CHECK(ra.invalidation == EditorInvalidation::Assets);
        CHECK(c.document().findAudioAsset("sfx")->name == "sfx");
        CHECK(c.document().findAudioAsset("sfx")->loadMode == AudioLoadMode::StaticSound);
        CHECK(!c.execute(AddAudioAssetCommand{"sfx", "a/b.ogg", AudioLoadMode::Stream}).ok); // dup

        const EditorOperationResult rf =
            c.execute(AddFontAssetCommand{"ui", "assets/fonts/ui.ttf", 32,
                                          FontGlyphPreset::European});
        CHECK(rf.ok);
        CHECK(c.document().findFontAsset("ui")->name == "ui");
        CHECK(c.document().findFontAsset("ui")->defaultPixelSize == 32);

        // undo/redo across both catalogs
        CHECK(c.undo().ok);  CHECK(!c.document().hasFontAsset("ui"));
        CHECK(c.redo().ok);  CHECK(c.document().hasFontAsset("ui"));

        const std::filesystem::path path = testTempDir() / "media.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);
        EditorCoordinator reloaded{ProjectDoc{}};
        CHECK(loadProjectFromFile(reloaded, path).ok);
        CHECK(reloaded.document().findAudioAsset("sfx")->name == "sfx");
        CHECK(reloaded.document().findAudioAsset("sfx")->sourcePath == "assets/audio/sfx.wav");
        CHECK(reloaded.document().findAudioAsset("sfx")->loadMode == AudioLoadMode::StaticSound);
        CHECK(reloaded.document().findFontAsset("ui")->name == "ui");
        CHECK(reloaded.document().findFontAsset("ui")->glyphPreset == FontGlyphPreset::European);
    }

    // -- Project asset catalog is saved from ProjectDocument, not cache state -
    {
        const std::filesystem::path base = testTempDir();
        const std::filesystem::path root = base / "catalog-roundtrip";
        std::error_code ec;
        std::filesystem::create_directories(root, ec);
        const std::filesystem::path image = base / "hero.png";
        const std::filesystem::path audio = base / "jump.wav";
        const std::filesystem::path font = base / "ui.ttf";
        { std::ofstream f(image, std::ios::binary); f << "PNG"; }
        { std::ofstream f(audio, std::ios::binary); f << "WAV"; }
        { std::ofstream f(font, std::ios::binary); f << "TTF"; }

        ProjectDoc catalogDoc = makeDoc();
        EntityDef heroType; heroType.className = "Hero"; heroType.name = "Hero";
        catalogDoc.objectTypes.emplace("Hero", std::move(heroType));
        EditorCoordinator c{std::move(catalogDoc)};
        CHECK(importAsset(c, root, {AssetKind::Image, image}).ok);
        CHECK(importAsset(c, root, {AssetKind::Audio, audio}).ok);
        CHECK(importAsset(c, root, {AssetKind::Font, font}).ok);
        CHECK(c.execute(AddSpriteRendererToObjectTypeCommand{"Hero"}).ok);
        CHECK(c.execute(SetObjectTypeSpriteSourceCommand{
            "Hero", ObjectTypeSpriteSourceKind::Image, "hero"}).ok);

        const std::filesystem::path path = root / "catalog.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);
        EditorCoordinator reloaded{ProjectDoc{}};
        CHECK(loadProjectFromFile(reloaded, path).ok);
        const ImageAssetDef* reloadedImage = reloaded.document().findImageAsset("hero");
        const AudioAssetDef* reloadedAudio = reloaded.document().findAudioAsset("jump");
        const FontAssetDef* reloadedFont = reloaded.document().findFontAsset("ui");
        CHECK(reloadedImage != nullptr);
        CHECK(reloadedAudio != nullptr);
        CHECK(reloadedFont != nullptr);
        CHECK(reloadedImage && reloadedImage->name == "hero");
        CHECK(reloadedImage && reloadedImage->sourcePath == "assets/images/hero.png");
        CHECK(reloadedAudio && reloadedAudio->sourcePath == "assets/audio/jump.wav");
        CHECK(reloadedAudio && reloadedAudio->loadMode == AudioLoadMode::StaticSound);
        CHECK(reloadedFont && reloadedFont->sourcePath == "assets/fonts/ui.ttf");
        const SpriteRenderView reloadedSprite =
            resolveSpriteRenderer(reloaded.document(), kSceneA, kHero);
        CHECK(reloadedSprite.present);
        CHECK(reloadedSprite.assetId == "hero");
        CHECK(reloadedImage && std::filesystem::exists(root / reloadedImage->sourcePath));

        std::filesystem::remove(root / "assets" / "images" / "hero.png", ec);
        EditorCoordinator missingFileReload{ProjectDoc{}};
        CHECK(loadProjectFromFile(missingFileReload, path).ok);
        const ImageAssetDef* missingImage =
            missingFileReload.document().findImageAsset("hero");
        CHECK(missingImage != nullptr);
        CHECK(missingImage && missingImage->sourcePath == "assets/images/hero.png");
    }

    // -- P0-04: project paths remain inside their canonical root --------------
    {
        const std::filesystem::path base = testTempDir();
        const std::filesystem::path root = base / "project";
        const std::filesystem::path outside = base / "project-outside";
        std::error_code ec;
        std::filesystem::create_directories(root / "assets" / "images", ec);
        CHECK(!ec);
        std::filesystem::create_directories(outside, ec);
        CHECK(!ec);
        { std::ofstream file(outside / "secret.png", std::ios::binary); file << "secret"; }

        const PathConfinementResult inside =
            resolvePathInsideRoot(root, "assets/images/hero.png");
        CHECK(inside.ok);
        CHECK(inside.value.parent_path().filename() == "images");

        // A write destination can have an as-yet absent tail. Its deepest
        // existing ancestor is still canonicalized, so the result remains
        // confined without requiring callers to pre-create folders.
        const PathConfinementResult newNested =
            resolvePathInsideRoot(root, "scripts/generated/player.lua");
        CHECK(newNested.ok);
        CHECK(newNested.value.filename() == "player.lua");

        const PathConfinementResult mixed =
            resolvePathInsideRoot(root, std::filesystem::u8path("assets\\images/hero.png"));
        CHECK(mixed.ok);
        CHECK(mixed.value == inside.value);

        for (const std::filesystem::path& hostile : {
                 std::filesystem::u8path("../secret.png"),
                 std::filesystem::u8path("assets/../../secret.png"),
                 std::filesystem::u8path("..\\project-outside\\secret.png"),
                 std::filesystem::absolute(outside / "secret.png"),
                 std::filesystem::u8path("Z:/secret.png"),
                 std::filesystem::u8path("assets/images/hero.png:payload")}) {
            const PathConfinementResult rejected = resolvePathInsideRoot(root, hostile);
            CHECK(!rejected.ok);
            CHECK(!rejected.error.empty());
            CHECK(!rejected.remediation.empty());
        }

        const std::filesystem::path unicodeRelative =
            std::filesystem::u8path("assets/immagini-\xC3\xA8/eroe.png");
        const PathConfinementResult unicode = resolvePathInsideRoot(root, unicodeRelative);
        CHECK(unicode.ok);

        // A same-prefix sibling is not a child. The lexical traversal is
        // rejected before any I/O, independently from string-prefix tricks.
        CHECK(!resolvePathInsideRoot(
            root, std::filesystem::u8path("../project-outside/secret.png")).ok);

        const std::filesystem::path link = root / "assets" / "external-link";
        std::filesystem::create_directory_symlink(outside, link, ec);
        if (!ec) {
            CHECK(!resolvePathInsideRoot(root, "assets/external-link/secret.png").ok);
        } else {
            // Windows without Developer Mode/SeCreateSymbolicLinkPrivilege cannot
            // create the fixture. Do not weaken production behavior: assert no
            // partial reparse point exists; CI hosts that permit symlinks execute
            // the escape assertion above.
            CHECK(!std::filesystem::exists(link));
        }

        ProjectDoc hostileDoc = makeDoc();
        ImageAssetDef escaped;
        escaped.assetId = "escaped";
        escaped.sourcePath = "../secret.png";
        hostileDoc.imageAssets.push_back(escaped);
        CHECK(!ProjectValidator::validate(ProjectDocument{hostileDoc}).ok);

        EditorCoordinator c{makeDoc()};
        const uint64_t revisionBefore = c.document().revision();
        CHECK(!c.execute(AddImageAssetCommand{"escaped", "../secret.png"}).ok);
        CHECK(c.document().revision() == revisionBefore);
        CHECK(!c.document().hasImageAsset("escaped"));
    }

    // -- Asset enum values are validated instead of defaulted silently --------
    {
        const std::string modernKeys =
            R"({"activeSceneId":"s","scenes":[{"id":"s"}],"imageAssets":[{"id":"img","name":"Hero","relativePath":"assets/images/hero.png"}]})";
        const std::string badAudio =
            R"({"activeSceneId":"s","scenes":[{"id":"s"}],"audioAssets":[{"assetId":"a","sourcePath":"assets/audio/a.wav","loadMode":"mmap"}]})";
        const std::string badFont =
            R"({"activeSceneId":"s","scenes":[{"id":"s"}],"fontAssets":[{"assetId":"f","sourcePath":"assets/fonts/f.ttf","glyphPreset":"kanji"}]})";
        const auto loaded = ProjectSerializer::deserialize(modernKeys);
        CHECK(loaded.ok);
        const ImageAssetDef* loadedImage =
            loaded.ok ? loaded.value.findImageAsset("img") : nullptr;
        CHECK(loadedImage != nullptr);
        CHECK(loadedImage && loadedImage->name == "Hero");
        CHECK(loadedImage && loadedImage->sourcePath == "assets/images/hero.png");
        CHECK(!ProjectSerializer::deserialize(badAudio).ok);
        CHECK(!ProjectSerializer::deserialize(badFont).ok);
    }

    // -- import audio/font: copy, kind dir, defaults, override ----------------
    {
        const std::filesystem::path base = testTempDir();   // call once: it wipes on each call
        const std::filesystem::path root = base / "import-media";
        std::error_code ec;
        std::filesystem::create_directories(root, ec);
        const std::filesystem::path ogg = base / "theme.ogg";
        { std::ofstream f(ogg, std::ios::binary); f << "OGG"; }
        const std::filesystem::path ttf = base / "type.ttf";
        { std::ofstream f(ttf, std::ios::binary); f << "TTF"; }

        EditorCoordinator c{makeDoc()};
        const ImportAssetResult a = importAsset(c, root, {AssetKind::Audio, ogg});
        CHECK(a.ok);
        CHECK(c.document().findAudioAsset("theme")->sourcePath == "assets/audio/theme.ogg");
        CHECK(c.document().findAudioAsset("theme")->loadMode == AudioLoadMode::Stream); // ogg default
        CHECK(std::filesystem::exists(root / "assets" / "audio" / "theme.ogg"));

        const ImportAssetResult f = importAsset(c, root, {AssetKind::Font, ttf});
        CHECK(f.ok);
        CHECK(c.document().findFontAsset("type")->sourcePath == "assets/fonts/type.ttf");
        CHECK(std::filesystem::exists(root / "assets" / "fonts" / "type.ttf"));

        // explicit load-mode override wins over the extension default
        const std::filesystem::path ogg2 = base / "blip.ogg";
        { std::ofstream g(ogg2, std::ios::binary); g << "OGG"; }
        ImportAssetRequest req;
        req.kind = AssetKind::Audio;
        req.sourcePath = ogg2;
        req.audioMode = AudioLoadMode::StaticSound;
        CHECK(importAsset(c, root, req).ok);
        CHECK(c.document().findAudioAsset("blip")->loadMode == AudioLoadMode::StaticSound);

        // wrong kind/format rejected
        CHECK(!importAsset(c, root, {AssetKind::Audio, ttf}).ok);   // .ttf as audio
    }

    // == Asset import pipeline (single canonical entry point) =================
    {
        const std::filesystem::path base = testTempDir();   // call once: it wipes on each call
        const std::filesystem::path root = base / "import-basic";
        std::error_code ec;
        std::filesystem::create_directories(root, ec);
        const std::filesystem::path src = base / "src.png";
        { std::ofstream f(src, std::ios::binary); f << "PNGDATA"; }

        EditorCoordinator c{makeDoc()};
        const ImportAssetResult r = importAsset(c, root, {AssetKind::Image, src});
        CHECK(r.ok);
        CHECK(r.assetId == "src");
        CHECK(c.document().findImageAsset("src")->sourcePath == "assets/images/src.png");
        CHECK(std::filesystem::exists(root / "assets" / "images" / "src.png"));
        CHECK(c.canUndo());   // import is an authoring command

        // Re-import the same source: unique file name + AssetId, no overwrite.
        const ImportAssetResult r2 = importAsset(c, root, {AssetKind::Image, src});
        CHECK(r2.ok);
        CHECK(r2.assetId == "src_2");
        CHECK(std::filesystem::exists(root / "assets" / "images" / "src_2.png"));
    }

    // -- import rejects: unsaved project, unsupported kind/format, during Play -
    {
        const std::filesystem::path base = testTempDir();   // call once: it wipes on each call
        const std::filesystem::path root = base / "import-reject";
        std::error_code ec; std::filesystem::create_directories(root, ec);
        const std::filesystem::path src = base / "reject.png";
        { std::ofstream f(src, std::ios::binary); f << "PNGDATA"; }
        const std::filesystem::path gif = base / "x.gif";
        { std::ofstream f(gif, std::ios::binary); f << "GIF"; }

        EditorCoordinator c{makeDoc()};
        CHECK(!importAsset(c, {}, {AssetKind::Image, src}).ok);          // unsaved
        CHECK(!importAsset(c, root, {AssetKind::Audio, src}).ok);        // .png as audio
        CHECK(!importAsset(c, root, {AssetKind::Image, gif}).ok);        // bad image format
        CHECK(c.document().data().imageAssets.empty());

        CHECK(c.playProject().ok);
        CHECK(!importAsset(c, root, {AssetKind::Image, src}).ok);        // during Play
    }

    // == Viewport camera transform + picking ==================================

    // -- screenToWorld inverts the renderer camera ----------------------------
    {
        ViewportRect rect{100, 50, 800, 600};
        EditorSceneViewState view;
        view.zoom = 2.f;
        view.pan = Vec2{10.f, -20.f};
        const SceneViewCamera cam = makeSceneViewCamera(rect, view, Vec2{640.f, 480.f});
        // Viewport centre maps to the camera target (world centre + pan).
        const Vec2 centre = screenToWorld(cam, Vec2{500.f, 350.f});
        CHECK(centre.x == 330.f);   // 640/2 + 10
        CHECK(centre.y == 220.f);   // 480/2 - 20
        // Two screen pixels right of centre is 1 world unit at zoom 2.
        const Vec2 off = screenToWorld(cam, Vec2{502.f, 350.f});
        CHECK(off.x == 331.f);
    }

    // -- computeFitZoom: the shared formula behind Fit View and Play's own
    // from-scratch camera (both must agree, so neither keeps a private copy) ---
    {
        // Wider-than-tall world in a wider-than-tall viewport: width-bound.
        ViewportRect rect{0, 0, 1000, 500};
        const float zoomW = computeFitZoom(Vec2{2000.f, 400.f}, rect, 0.f);
        CHECK(zoomW == 0.5f);   // 1000/2000, height would allow 1.25
        // Tall world: height-bound.
        const float zoomH = computeFitZoom(Vec2{400.f, 2000.f}, rect, 0.f);
        CHECK(zoomH == 0.25f);  // 500/2000, width would allow 2.5
        // Padding shrinks the available area on both axes.
        const float padded = computeFitZoom(Vec2{100.f, 100.f}, rect, 100.f);
        CHECK(padded == 3.f);   // avail 800x300 -> 800/100=8, 300/100=3, min wins
        // Degenerate world size: no crash, a defined fallback.
        CHECK(computeFitZoom(Vec2{0.f, 100.f}, rect, 0.f) == 1.f);
        CHECK(computeFitZoom(Vec2{100.f, -1.f}, rect, 0.f) == 1.f);
    }

    // -- ADR-0018: Play zoom follows Game View, not world bounds --------------
    {
        ViewportRect rect{0, 0, 1000, 500};
        const auto a = resolvePlayView(PlayViewportProjectionInput{
            {512.f, 320.f}, {512.f, 320.f}, {256.f, 160.f}, rect, 0.f});
        const auto b = resolvePlayView(PlayViewportProjectionInput{
            {1024.f, 320.f}, {512.f, 320.f}, {256.f, 160.f}, rect, 0.f});
        CHECK(a.zoom == b.zoom);
        CHECK(a.zoom == computeFitZoom(Vec2{512.f, 320.f}, rect, 0.f));
        // Larger world with same center shifts pan (relative to world mid).
        CHECK(b.pan.x != a.pan.x);
    }

    // -- pickEntityAt: topmost hit; miss returns INVALID ----------------------
    {
        SceneFrameSnapshot f;
        f.hasScene = true;
        f.entities.push_back(SceneFrameEntity{7, "A", {}, SceneFrameRect{0, 0, 50, 50}, false});
        f.entities.push_back(SceneFrameEntity{8, "B", {}, SceneFrameRect{40, 40, 50, 50}, false});
        CHECK(pickEntityAt(f, Vec2{10.f, 10.f}) == 7);
        CHECK(pickEntityAt(f, Vec2{45.f, 45.f}) == 8);   // overlap -> later wins
        CHECK(pickEntityAt(f, Vec2{200.f, 200.f}) == INVALID_ENTITY);
    }

    // -- pickEntityAt: a visible sprite occludes a placeholder -----------------
    // frame.entities is the pick order authority (collectSceneFrameSnapshot
    // always emits every instance into it, in render order) - 1 is background,
    // 3 and 2 are foreground of it, so this exercises both "later in
    // frame.entities wins" and "invisible sprite never hits regardless".
    {
        SceneFrameSnapshot f;
        f.hasScene = true;
        f.entities.push_back(SceneFrameEntity{1, "P", {}, SceneFrameRect{0, 0, 50, 50}, false});
        f.entities.push_back(SceneFrameEntity{3, "S3", {}, SceneFrameRect{10, 10, 50, 50}, false});
        f.entities.push_back(SceneFrameEntity{2, "S2", {}, SceneFrameRect{10, 10, 50, 50}, false});
        f.sprites.push_back(SceneFrameSprite{2, "img", SceneFrameRect{10, 10, 50, 50}, {}, true, false});
        f.sprites.push_back(SceneFrameSprite{3, "img", SceneFrameRect{10, 10, 50, 50}, {}, false, false});
        CHECK(pickEntityAt(f, Vec2{20.f, 20.f}) == 2);   // sprite over placeholder
        CHECK(pickEntityAt(f, Vec2{5.f, 5.f}) == 1);     // invisible sprite ignored
    }

    // -- pickEntityAt: empty tilemap / name-band still pick via placeholder ----
    {
        SceneFrameSnapshot f;
        f.hasScene = true;
        f.entities.push_back(SceneFrameEntity{9, "EmptyTM", {}, SceneFrameRect{0, 0, 32, 32}, false});
        f.tilemaps.push_back(SceneFrameTilemap{9, "img", {}, false});
        CHECK(pickEntityAt(f, Vec2{16.f, 16.f}) == 9);
        CHECK(pickEntityAt(f, Vec2{16.f, -6.f}) == 9);   // name chip band above the box
        CHECK(pickEntityAt(f, Vec2{16.f, -20.f}) == INVALID_ENTITY);
    }

    // -- editorBoundsForEntity: sprite/collider union, else placeholder -------
    {
        SceneFrameSnapshot f;
        f.hasScene = true;
        f.worldSize = Vec2{100.f, 100.f};
        f.entities.push_back(SceneFrameEntity{1, "P", {}, SceneFrameRect{0, 0, 10, 10}, false});
        f.entities.push_back(SceneFrameEntity{2, "Q", {}, SceneFrameRect{70, 70, 10, 10}, false});
        f.sprites.push_back(SceneFrameSprite{1, "img", SceneFrameRect{20, 0, 10, 10}, {}, true, false});
        f.sprites.push_back(SceneFrameSprite{1, "hidden", SceneFrameRect{-100, -100, 10, 10}, {}, false, false});
        f.colliders.push_back(SceneFrameCollider{
            1, WorldRect{0, 20, 10, 10}, true, BoxColliderMode::Solid, false});

        const std::optional<WorldRect> bounds = editorBoundsForEntity(f, 1);
        CHECK(bounds.has_value());
        CHECK(bounds->x == 0.f);
        CHECK(bounds->y == 0.f);
        CHECK(bounds->width == 30.f);
        CHECK(bounds->height == 30.f);

        const std::optional<WorldRect> fallback = editorBoundsForEntity(f, 2);
        CHECK(fallback.has_value());
        CHECK(fallback->x == 70.f);
        CHECK(fallback->y == 70.f);
    }

    // -- pickEntityAt: a tilemap cell is hit-testable, and a sprite-owning
    // entity later in frame.entities (foreground) still wins over one earlier
    // (background) that owns a tilemap ------------------------------------------
    {
        SceneFrameSnapshot f;
        f.hasScene = true;
        f.entities.push_back(SceneFrameEntity{1, "Ground", {}, SceneFrameRect{0, 0, 10, 10}, false});
        f.entities.push_back(SceneFrameEntity{2, "Hero", {}, SceneFrameRect{10, 10, 10, 10}, false});
        f.tilemaps.push_back(SceneFrameTilemap{
            1, "tiles-img",
            {SceneFrameTilemapCell{SceneFrameRect{0, 0, 32, 32}, SceneFrameRect{0, 0, 16, 16}}},
            false});
        f.sprites.push_back(SceneFrameSprite{2, "img", SceneFrameRect{10, 10, 10, 10}, {}, true, false});
        CHECK(pickEntityAt(f, Vec2{5.f, 5.f}) == 1);      // hits the tile, not just the placeholder
        CHECK(pickEntityAt(f, Vec2{15.f, 15.f}) == 2);    // sprite entity, later in render order, wins
        CHECK(pickEntityAt(f, Vec2{500.f, 500.f}) == INVALID_ENTITY);
    }

    // -- editorBoundsForEntity: tilemap cells union like sprite/collider
    // bounds, and an empty tilemap (no cells) falls through to the
    // placeholder, same as any other content-less entity ----------------------
    {
        SceneFrameSnapshot f;
        f.hasScene = true;
        f.entities.push_back(SceneFrameEntity{1, "T", {}, SceneFrameRect{5, 5, 1, 1}, false});
        f.tilemaps.push_back(SceneFrameTilemap{
            1, "tiles-img",
            {SceneFrameTilemapCell{SceneFrameRect{0, 0, 20, 20}, {}},
             SceneFrameTilemapCell{SceneFrameRect{20, 20, 20, 20}, {}}},
            false});
        const std::optional<WorldRect> bounds = editorBoundsForEntity(f, 1);
        CHECK(bounds.has_value());
        CHECK(bounds->x == 0.f);
        CHECK(bounds->y == 0.f);
        CHECK(bounds->width == 40.f);
        CHECK(bounds->height == 40.f);

        SceneFrameSnapshot empty;
        empty.hasScene = true;
        empty.entities.push_back(SceneFrameEntity{1, "T", {}, SceneFrameRect{5, 5, 1, 1}, false});
        empty.tilemaps.push_back(SceneFrameTilemap{1, "tiles-img", {}, false});
        const std::optional<WorldRect> emptyBounds = editorBoundsForEntity(empty, 1);
        CHECK(emptyBounds.has_value());
        CHECK(emptyBounds->x == 5.f);   // no cells -> falls through to the placeholder bounds
    }

    // -- applyDragPreviewOffset: every drawn representation of the dragged
    // entity moves by the same delta - a Tilemap's cells included, or the
    // painted tiles stay frozen at the pre-drag spot for the whole gesture
    // and only jump to the new position on release (looks like stutter even
    // though frame time itself is unaffected). Regression for exactly that
    // bug: editor_app.cpp's live drag preview offset entities/sprites/
    // colliders but forgot tilemaps ---------------------------------------------
    {
        SceneFrameSnapshot f;
        f.hasScene = true;
        f.entities.push_back(SceneFrameEntity{1, "TM", {}, SceneFrameRect{10, 10, 5, 5}, true});
        f.entities.push_back(SceneFrameEntity{2, "Other", {}, SceneFrameRect{0, 0, 5, 5}, false});
        f.sprites.push_back(SceneFrameSprite{1, "img", SceneFrameRect{10, 10, 5, 5}, {}, true, true});
        f.colliders.push_back(SceneFrameCollider{1, WorldRect{10, 10, 5, 5},
                                                  true, BoxColliderMode::Solid, true});
        f.tilemaps.push_back(SceneFrameTilemap{
            1, "tiles-img",
            {SceneFrameTilemapCell{SceneFrameRect{10, 10, 32, 32}, SceneFrameRect{0, 0, 16, 16}},
             SceneFrameTilemapCell{SceneFrameRect{42, 10, 32, 32}, SceneFrameRect{16, 0, 16, 16}}},
            true});
        f.tilemaps.push_back(SceneFrameTilemap{
            2, "tiles-img",
            {SceneFrameTilemapCell{SceneFrameRect{0, 0, 32, 32}, SceneFrameRect{0, 0, 16, 16}}},
            false});

        applyDragPreviewOffset(f, 1, Vec2{7.f, -3.f});

        CHECK(f.entities[0].bounds.x == 17.f);
        CHECK(f.entities[0].bounds.y == 7.f);
        CHECK(f.entities[1].bounds.x == 0.f);   // untouched: not the dragged entity
        CHECK(f.sprites[0].destination.x == 17.f);
        CHECK(f.sprites[0].destination.y == 7.f);
        CHECK(f.colliders[0].worldBounds.x == 17.f);
        CHECK(f.colliders[0].worldBounds.y == 7.f);
        CHECK(f.tilemaps[0].cells[0].destination.x == 17.f);
        CHECK(f.tilemaps[0].cells[0].destination.y == 7.f);
        CHECK(f.tilemaps[0].cells[1].destination.x == 49.f);
        CHECK(f.tilemaps[0].cells[1].destination.y == 7.f);
        // Untouched: entity 2's tilemap is not the one being dragged.
        CHECK(f.tilemaps[1].cells[0].destination.x == 0.f);
        CHECK(f.tilemaps[1].cells[0].destination.y == 0.f);
    }

    // -- collectSceneFrameSnapshot: an instance's TilemapComponent resolves
    // into snapshot.tilemaps with world-space cells; an empty component still
    // produces an entry, just with no cells --------------------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        sliceTilesOne(c);   // "tiles-1" now has "tile-1".."tile-4"
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.cellSize = {32.f, 32.f};
        tm.chunkSize = 2;
        TilemapChunk chunk;
        chunk.cells = {TilemapCellValue{"tile-1", TileTransformFlags::None},
                      std::nullopt, std::nullopt, std::nullopt};
        tm.chunks.push_back(chunk);
        CHECK(c.execute(AddTilemapComponentCommand{kSceneA, kHero, tm}).ok);
        // kHero's transform.position is {10,20} (makeDoc's fixture) - the world
        // origin for this tilemap's cell (0,0).
        CHECK(c.execute(SetEntityTransformCommand{kSceneA, kHero, {10.f, 20.f}}).ok);

        const SceneFrameSnapshot snap = collectSceneFrameSnapshot(c.document(), kSceneA, INVALID_ENTITY);
        const auto it = std::find_if(snap.tilemaps.begin(), snap.tilemaps.end(),
            [](const SceneFrameTilemap& t) { return t.entityId == kHero; });
        CHECK(it != snap.tilemaps.end());
        CHECK(it->imageAssetId == "img-hero");
        CHECK(it->cells.size() == 1);
        CHECK(it->cells[0].destination.x == 10.f);
        CHECK(it->cells[0].destination.y == 20.f);

        CHECK(c.execute(RemoveTilemapComponentCommand{kSceneA, kHero}).ok);
        TilemapComponent emptyTm;
        emptyTm.tilesetAssetId = "tiles-1";
        CHECK(c.execute(AddTilemapComponentCommand{kSceneA, kHero, emptyTm}).ok);
        const SceneFrameSnapshot emptySnap =
            collectSceneFrameSnapshot(c.document(), kSceneA, INVALID_ENTITY);
        const auto emptyIt = std::find_if(emptySnap.tilemaps.begin(), emptySnap.tilemaps.end(),
            [](const SceneFrameTilemap& t) { return t.entityId == kHero; });
        CHECK(emptyIt != emptySnap.tilemaps.end());
        CHECK(emptyIt->cells.empty());
    }

    // -- scene containment and explicit bring-inside math ---------------------
    {
        CHECK(classifySceneContainment(WorldRect{10, 10, 20, 20}, Vec2{100, 100})
              == SceneContainment::Inside);
        CHECK(classifySceneContainment(WorldRect{90, 10, 20, 20}, Vec2{100, 100})
              == SceneContainment::PartiallyOutside);
        CHECK(classifySceneContainment(WorldRect{120, 10, 20, 20}, Vec2{100, 100})
              == SceneContainment::FullyOutside);

        const std::optional<Vec2> right =
            positionToBringBoundsInsideScene(WorldRect{90, 10, 20, 20},
                                             Vec2{100, 20}, Vec2{100, 100});
        CHECK(right.has_value());
        CHECK(right->x == 90.f);
        CHECK(right->y == 20.f);

        const std::optional<Vec2> large =
            positionToBringBoundsInsideScene(WorldRect{-30, 0, 200, 20},
                                             Vec2{70, 10}, Vec2{100, 100});
        CHECK(large.has_value());
        CHECK(large->x == 50.f);   // entity wider than scene -> centered on X
        CHECK(large->y == 10.f);

        const std::optional<Vec2> bad =
            positionToBringBoundsInsideScene(WorldRect{0, 0, 10, 10},
                                             Vec2{std::numeric_limits<float>::quiet_NaN(), 0},
                                             Vec2{100, 100});
        CHECK(!bad.has_value());
    }

    // == Scene properties (Scene Inspector) ===================================

    // -- Rename scene: applies, rejects empty, undo/redo ----------------------
    {
        EditorCoordinator c{makeDoc()};                  // kSceneA name "Scene A"
        CHECK(c.execute(RenameSceneCommand{kSceneA, "Level 1"}).ok);
        CHECK(c.document().findScene(kSceneA)->name == "Level 1");
        CHECK(!c.execute(RenameSceneCommand{kSceneA, ""}).ok);   // empty rejected
        CHECK(c.undo().ok);
        CHECK(c.document().findScene(kSceneA)->name == "Scene A");
        CHECK(c.redo().ok);
        CHECK(c.document().findScene(kSceneA)->name == "Level 1");
    }

    // -- Rename object type: applies (shared by every instance of that type),
    // rejects empty/unknown id, same name is a no-op, undo/redo ---------------
    {
        EditorCoordinator c{makeInheritedDoc()};   // catalog already has "Hero"
        CHECK(c.execute(RenameObjectTypeCommand{"Hero", "Champion"}).ok);
        CHECK(c.document().findObjectType("Hero")->name == "Champion");

        CHECK(!c.execute(RenameObjectTypeCommand{"Hero", ""}).ok);        // empty rejected
        CHECK(!c.execute(RenameObjectTypeCommand{"Missing", "X"}).ok);    // unknown id rejected
        CHECK(c.document().findObjectType("Hero")->name == "Champion");  // unaffected by rejects

        // Same name is a no-op: succeeds, but nothing invalidates and no undo entry.
        const std::size_t undoBefore = c.undoSize();
        CHECK(c.execute(RenameObjectTypeCommand{"Hero", "Champion"}).ok);
        CHECK(c.undoSize() == undoBefore);

        CHECK(c.undo().ok);
        CHECK(c.document().findObjectType("Hero")->name == "Hero");      // back to original
        CHECK(c.redo().ok);
        CHECK(c.document().findObjectType("Hero")->name == "Champion");
    }

    // -- Set scene size: validation, integer normalize, never moves instances -
    {
        EditorCoordinator c{makeDoc()};
        const Vec2 heroBefore = c.document().findInstanceInScene(kSceneA, kHero)->transform.position;

        CHECK(c.execute(SetSceneSizeCommand{kSceneA, {320.f, 180.f}}).ok);
        CHECK(c.document().findScene(kSceneA)->worldSize.x == 320.f);
        CHECK(c.document().findScene(kSceneA)->worldSize.y == 180.f);
        // Resizing must not move an instance (Outside Scene UX flags it instead).
        const Vec2 heroAfter = c.document().findInstanceInScene(kSceneA, kHero)->transform.position;
        CHECK(heroAfter.x == heroBefore.x && heroAfter.y == heroBefore.y);

        // Non-positive / non-finite are rejected without mutation.
        CHECK(!c.execute(SetSceneSizeCommand{kSceneA, {0.f, 100.f}}).ok);
        CHECK(!c.execute(SetSceneSizeCommand{kSceneA, {100.f, -5.f}}).ok);
        CHECK(!c.execute(SetSceneSizeCommand{
            kSceneA, {std::numeric_limits<float>::infinity(), 100.f}}).ok);
        CHECK(!c.execute(SetSceneSizeCommand{
            kSceneA, {100.f, std::numeric_limits<float>::quiet_NaN()}}).ok);
        CHECK(c.document().findScene(kSceneA)->worldSize.x == 320.f);   // unchanged

        // Committed values normalize to whole pixels.
        CHECK(c.execute(SetSceneSizeCommand{kSceneA, {199.4f, 150.6f}}).ok);
        CHECK(c.document().findScene(kSceneA)->worldSize.x == 199.f);
        CHECK(c.document().findScene(kSceneA)->worldSize.y == 151.f);

        CHECK(c.undo().ok);
        CHECK(c.document().findScene(kSceneA)->worldSize.x == 320.f);
        CHECK(c.redo().ok);
        CHECK(c.document().findScene(kSceneA)->worldSize.x == 199.f);
    }

    // -- Set scene Game View size (ADR-0018): distinct from world bounds -----
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.document().findScene(kSceneA)->viewportSize.x == 512.f);
        CHECK(c.execute(SetSceneViewportSizeCommand{kSceneA, {640.f, 360.f}}).ok);
        CHECK(c.document().findScene(kSceneA)->viewportSize.x == 640.f);
        CHECK(c.document().findScene(kSceneA)->viewportSize.y == 360.f);
        CHECK(c.document().findScene(kSceneA)->worldSize.x == 512.f);  // world untouched

        CHECK(!c.execute(SetSceneViewportSizeCommand{kSceneA, {0.f, 100.f}}).ok);
        CHECK(!c.execute(SetSceneViewportSizeCommand{kSceneA, {-1.f, 100.f}}).ok);
        CHECK(c.document().findScene(kSceneA)->viewportSize.x == 640.f);

        CHECK(c.execute(SetSceneViewportSizeCommand{kSceneA, {199.4f, 150.6f}}).ok);
        CHECK(c.document().findScene(kSceneA)->viewportSize.x == 199.f);
        CHECK(c.document().findScene(kSceneA)->viewportSize.y == 151.f);

        const std::size_t undoBeforeNoop = c.undoSize();
        CHECK(c.execute(SetSceneViewportSizeCommand{kSceneA, {199.f, 151.f}}).ok);
        CHECK(c.undoSize() == undoBeforeNoop);  // no-op

        CHECK(c.undo().ok);
        CHECK(c.document().findScene(kSceneA)->viewportSize.x == 640.f);
        CHECK(c.redo().ok);
        CHECK(c.document().findScene(kSceneA)->viewportSize.x == 199.f);
    }

    // -- Persistent numeric boundaries reject NaN/Inf atomically ------------
    {
        EditorCoordinator c{makeDoc()};
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        const float negInf = -std::numeric_limits<float>::infinity();
        const uint64_t revisionBefore = c.document().revision();
        const std::size_t undoBefore = c.undoSize();
        const Vec2 positionBefore =
            c.document().findInstanceInScene(kSceneA, kHero)->transform.position;
        const Vec4 backgroundBefore = c.document().findScene(kSceneA)->backgroundColor;

        CHECK(!c.execute(SetEntityTransformCommand{kSceneA, kHero, {nan, 10.f}}).ok);
        CHECK(!c.execute(SetEntityTransformCommand{kSceneA, kHero, {10.f, inf}}).ok);
        CHECK(!c.execute(SetEntityTransformCommand{kSceneA, kHero, {negInf, 10.f}}).ok);
        CHECK(!c.execute(CreateEntityCommand{kSceneA, 1001, "Hero", "Bad", {inf, 0.f}}).ok);
        CHECK(!c.execute(CloneInstanceCommand{kSceneA, kHero, 1002, "Bad clone", {0.f, nan}}).ok);
        CHECK(!c.execute(CreateEntityWithDefaultTypeCommand{
            kSceneA, 1003, "BadType", "Bad", "Bad", {nan, 0.f}}).ok);
        CHECK(!c.execute(SetSceneBackgroundCommand{kSceneA, {0.f, nan, 0.f, 1.f}}).ok);

        const SceneInstanceDef* hero = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(hero->transform.position.x == positionBefore.x);
        CHECK(hero->transform.position.y == positionBefore.y);
        CHECK(c.document().findInstanceInScene(kSceneA, 1001) == nullptr);
        CHECK(c.document().findInstanceInScene(kSceneA, 1002) == nullptr);
        CHECK(c.document().findInstanceInScene(kSceneA, 1003) == nullptr);
        CHECK(!c.document().hasObjectType("BadType"));
        const Vec4 backgroundAfter = c.document().findScene(kSceneA)->backgroundColor;
        CHECK(backgroundAfter.r == backgroundBefore.r && backgroundAfter.g == backgroundBefore.g);
        CHECK(backgroundAfter.b == backgroundBefore.b && backgroundAfter.a == backgroundBefore.a);
        CHECK(c.document().revision() == revisionBefore);
        CHECK(c.undoSize() == undoBefore);
    }

    // -- Scene name + size survive save/reload --------------------------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(RenameSceneCommand{kSceneA, "Arena"}).ok);
        CHECK(c.execute(SetSceneSizeCommand{kSceneA, {640.f, 360.f}}).ok);
        const std::filesystem::path path = testTempDir() / "scene-props.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);
        EditorCoordinator reloaded{ProjectDoc{}};
        CHECK(loadProjectFromFile(reloaded, path).ok);
        CHECK(reloaded.document().findScene(kSceneA)->name == "Arena");
        CHECK(reloaded.document().findScene(kSceneA)->worldSize.x == 640.f);
        CHECK(reloaded.document().findScene(kSceneA)->worldSize.y == 360.f);
    }

    // == Scene view navigation (workspace camera) =============================

    // -- Zoom under the cursor keeps the world point beneath the mouse fixed ---
    //    Mirrors routeViewportInput: read world-before, apply zoom, compensate
    //    with one pan. Uses the single makeSceneViewCamera/screenToWorld.
    {
        const ViewportRect rect{0, 0, 800, 600};
        const Vec2 worldSize{1000.f, 1000.f};
        const Vec2 mouse{220.f, 140.f};   // off-centre, so a centre-zoom would drift
        EditorCoordinator c{makeDoc()};
        c.apply(SetViewportZoomIntent{kSceneA, 1.0f});
        c.apply(PanViewportIntent{kSceneA, {30.f, -20.f}});

        const EditorSceneViewState before = c.sceneView(kSceneA);
        const Vec2 worldBefore = screenToWorld(makeSceneViewCamera(rect, before, worldSize), mouse);
        c.apply(SetViewportZoomIntent{kSceneA, before.zoom * 1.5f});
        const EditorSceneViewState mid = c.sceneView(kSceneA);
        const Vec2 worldMid = screenToWorld(makeSceneViewCamera(rect, mid, worldSize), mouse);
        c.apply(PanViewportIntent{kSceneA, {worldBefore.x - worldMid.x, worldBefore.y - worldMid.y}});

        const EditorSceneViewState fixed = c.sceneView(kSceneA);
        const Vec2 worldFinal = screenToWorld(makeSceneViewCamera(rect, fixed, worldSize), mouse);
        CHECK(std::abs(worldFinal.x - worldBefore.x) < 0.01f);
        CHECK(std::abs(worldFinal.y - worldBefore.y) < 0.01f);
    }

    // -- Zoom is clamped to [10%, 800%] ---------------------------------------
    {
        EditorCoordinator c{makeDoc()};
        c.apply(SetViewportZoomIntent{kSceneA, 100.0f});
        CHECK(c.sceneView(kSceneA).zoom == SceneViewLimits::kZoomMax);
        c.apply(SetViewportZoomIntent{kSceneA, 0.0001f});
        CHECK(c.sceneView(kSceneA).zoom == SceneViewLimits::kZoomMin);
    }

    // -- Workspace numeric state rejects NaN/Inf; finite extremes clamp ------
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        EditorCoordinator c{makeAnimationDoc()};
        const uint64_t revisionBefore = c.document().revision();

        CHECK(!c.apply(SetViewportZoomIntent{kSceneA, nan}).ok);
        CHECK(!c.apply(SetViewportZoomIntent{kSceneA, 0.f}).ok);
        CHECK(!c.apply(PanViewportIntent{kSceneA, {inf, 0.f}}).ok);
        CHECK(c.sceneView(kSceneA).zoom == 1.f);
        CHECK(c.sceneView(kSceneA).pan.x == 0.f);

        CHECK(c.apply(OpenSpriteAnimationEditorIntent{"hero.anim"}).ok);
        CHECK(!c.apply(SetSpriteSheetZoomIntent{nan}).ok);
        CHECK(!c.apply(PanSpriteSheetIntent{{0.f, inf}}).ok);
        CHECK(c.state().spriteAnimationEditor.sheetZoom == 1.f);
        CHECK(c.state().spriteAnimationEditor.sheetPan.y == 0.f);

        CHECK(c.apply(OpenTilesetEditorIntent{"tiles-1"}).ok);
        CHECK(!c.apply(SetTilesetEditorZoomIntent{inf}).ok);
        CHECK(!c.apply(PanTilesetEditorIntent{{nan, 0.f}}).ok);
        CHECK(c.state().tilesetEditor.zoom == 1.f);
        CHECK(c.state().tilesetEditor.pan.x == 0.f);

        const float panelBefore = c.uiState().leftPanelWidth;
        CHECK(!c.apply(ResizePanelIntent{ResizePanelIntent::Panel::Left, nan}).ok);
        CHECK(c.uiState().leftPanelWidth == panelBefore);
        CHECK(c.apply(ResizePanelIntent{
            ResizePanelIntent::Panel::Left, std::numeric_limits<float>::max()}).ok);
        CHECK(c.uiState().leftPanelWidth == PanelLimits::kLeftMax);
        CHECK(c.document().revision() == revisionBefore);
        CHECK(c.undoSize() == 0);
    }

    // -- Reset to 100% changes only the zoom, never the target (pan) ----------
    {
        EditorCoordinator c{makeDoc()};
        c.apply(PanViewportIntent{kSceneA, {50.f, 30.f}});
        c.apply(SetViewportZoomIntent{kSceneA, 3.0f});
        c.apply(SetViewportZoomIntent{kSceneA, 1.0f});   // "Reset 100%"
        CHECK(c.sceneView(kSceneA).zoom == 1.0f);
        CHECK(c.sceneView(kSceneA).pan.x == 50.f);       // target unchanged
        CHECK(c.sceneView(kSceneA).pan.y == 30.f);
    }

    // -- Each scene keeps its own pan and zoom --------------------------------
    {
        EditorCoordinator c{makeDoc()};
        c.apply(SetViewportZoomIntent{kSceneA, 2.0f});
        c.apply(PanViewportIntent{kSceneA, {10.f, 0.f}});
        c.apply(SetViewportZoomIntent{kSceneB, 0.5f});
        c.apply(PanViewportIntent{kSceneB, {-5.f, 5.f}});
        CHECK(c.sceneView(kSceneA).zoom == 2.0f);
        CHECK(c.sceneView(kSceneA).pan.x == 10.f);
        CHECK(c.sceneView(kSceneB).zoom == 0.5f);
        CHECK(c.sceneView(kSceneB).pan.y == 5.f);
    }

    // -- Camera ops are workspace-only: no dirty / revision / undo ------------
    {
        EditorCoordinator c{makeDoc()};
        const uint64_t rev = c.document().revision();
        c.apply(SetViewportZoomIntent{kSceneA, 2.0f});
        c.apply(PanViewportIntent{kSceneA, {5.f, 5.f}});
        CHECK(!c.document().isDirty());
        CHECK(c.document().revision() == rev);
        CHECK(c.undoSize() == 0);
    }

    // -- Scene grid/snap intents reject a scene that doesn't exist -------------
    // (defence in depth: the toolbar/menu also disable these with no active
    // scene, but the coordinator must not rely on that alone).
    {
        EditorCoordinator c{makeDoc()};
        const SceneId bogus{"no-such-scene"};
        CHECK(!c.apply(SetSceneGridVisibilityIntent{bogus, false}).ok);
        CHECK(!c.apply(SetSceneGridSnapEnabledIntent{bogus, true}).ok);
        CHECK(!c.apply(SetSceneGridCellSizeIntent{bogus, 32.0f}).ok);
        CHECK(c.sceneView(bogus).gridVisible);   // default, untouched by the map
    }

    // -- Zoom/Pan intents reject a scene that doesn't exist, same reason ------
    {
        EditorCoordinator c{makeDoc()};
        const SceneId bogus{"no-such-scene"};
        CHECK(!c.apply(SetViewportZoomIntent{bogus, 2.0f}).ok);
        CHECK(!c.apply(PanViewportIntent{bogus, {10.f, 10.f}}).ok);
        CHECK(c.sceneView(bogus).zoom == 1.0f);   // default, untouched by the map
        CHECK(c.sceneView(bogus).pan.x == 0.f);
    }

    // -- Scene grid/snap toggles are workspace-only and per-scene -------------
    {
        EditorCoordinator c{makeDoc()};
        const uint64_t rev = c.document().revision();
        CHECK(c.sceneView(kSceneA).gridVisible);
        CHECK(!c.sceneView(kSceneA).gridSnapEnabled);
        CHECK(c.sceneView(kSceneA).gridCellSize == 32.0f);

        CHECK(c.apply(SetSceneGridVisibilityIntent{kSceneA, false}).ok);
        CHECK(c.apply(SetSceneGridSnapEnabledIntent{kSceneA, true}).ok);
        CHECK(c.apply(SetSceneGridCellSizeIntent{kSceneA, 48.0f}).ok);
        CHECK(!c.sceneView(kSceneA).gridVisible);
        CHECK(c.sceneView(kSceneA).gridSnapEnabled);
        CHECK(c.sceneView(kSceneA).gridCellSize == 48.0f);
        CHECK(c.sceneView(kSceneB).gridVisible);
        CHECK(!c.sceneView(kSceneB).gridSnapEnabled);
        CHECK(c.sceneView(kSceneB).gridCellSize == 32.0f);
        CHECK(!c.document().isDirty());
        CHECK(c.document().revision() == rev);
        CHECK(c.undoSize() == 0);

        CHECK(c.apply(SetSceneGridVisibilityIntent{kSceneB, false}).ok);
        CHECK(c.apply(SetSceneGridCellSizeIntent{kSceneB, 64.0f}).ok);
        CHECK(!c.sceneView(kSceneB).gridVisible);
        CHECK(!c.sceneView(kSceneB).gridSnapEnabled);
        CHECK(c.sceneView(kSceneB).gridCellSize == 64.0f);

        const std::filesystem::path path = testTempDir() / "grid-snap-workspace.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);
        EditorCoordinator loaded{ProjectDoc{}};
        CHECK(loadProjectFromFile(loaded, path).ok);
        CHECK(loaded.sceneView(kSceneA).gridVisible);
        CHECK(!loaded.sceneView(kSceneA).gridSnapEnabled);
        CHECK(loaded.sceneView(kSceneA).gridCellSize == 32.0f);

        CHECK(c.replaceProject(ProjectDocument{makeReplacementDoc()}).ok);
        CHECK(c.state().sceneViews.count(kSceneA) == 0);
        CHECK(c.sceneView(kSceneA).gridVisible);
        CHECK(!c.sceneView(kSceneA).gridSnapEnabled);
        CHECK(c.sceneView(kSceneA).gridCellSize == 32.0f);
    }

    // -- Grid cell size rejects invalid values and no-op changes stay quiet ---
    {
        EditorCoordinator c{makeDoc()};
        const uint64_t rev = c.document().revision();
        c.consumeInvalidations();

        CHECK(!c.apply(SetSceneGridCellSizeIntent{kSceneA, 0.0f}).ok);
        CHECK(!c.apply(SetSceneGridCellSizeIntent{kSceneA, -4.0f}).ok);
        CHECK(!c.apply(SetSceneGridCellSizeIntent{
            kSceneA, std::numeric_limits<float>::quiet_NaN()}).ok);
        CHECK(!c.apply(SetSceneGridCellSizeIntent{
            kSceneA, std::numeric_limits<float>::infinity()}).ok);
        CHECK(c.sceneView(kSceneA).gridCellSize == 32.0f);
        // Each rejected intent is still a real, user-visible failure (never
        // silent) - it logs a Console warning, which is itself an invalidation.
        CHECK(c.pendingInvalidations() == EditorInvalidation::Console);
        c.consumeInvalidations();

        const EditorOperationResult same =
            c.apply(SetSceneGridCellSizeIntent{kSceneA, 32.0f});
        CHECK(same.ok);
        CHECK(same.invalidation == EditorInvalidation::None);
        CHECK(c.pendingInvalidations() == EditorInvalidation::None);
        CHECK(!c.document().isDirty());
        CHECK(c.document().revision() == rev);
        CHECK(c.undoSize() == 0);
    }

    // -- Grid snap uses nearest world grid point, including negative values ---
    {
        const SceneGridDefinition grid = worldAuthoringGrid(EditorSceneViewState{});
        CHECK(grid.kind == SceneGridKind::World);
        CHECK(grid.cellSize.x == 32.0f);
        CHECK(grid.cellSize.y == 32.0f);
        CHECK(snapWorldPositionToGrid(Vec2{31.f, 73.f}, grid).x == 32.f);
        CHECK(snapWorldPositionToGrid(Vec2{31.f, 73.f}, grid).y == 64.f);
        CHECK(snapWorldPositionToGrid(Vec2{-15.f, -26.f}, grid).x == 0.f);
        CHECK(snapWorldPositionToGrid(Vec2{-25.f, -26.f}, grid).x == -32.f);
        CHECK(snapWorldPositionToGrid(Vec2{-25.f, -26.f}, grid).y == -32.f);
        CHECK(snapWorldPositionToGrid(Vec2{96.f, 144.f}, grid).x == 96.f);
        CHECK(snapWorldPositionToGrid(Vec2{16.f, -16.f}, grid).x == 32.f);
        CHECK(snapWorldPositionToGrid(Vec2{16.f, -16.f}, grid).y == -32.f);

        const SceneGridDefinition shifted{
            SceneGridKind::World,
            Vec2{16.0f, 16.0f},
            Vec2{8.0f, 8.0f},
        };
        const Vec2 snapped = snapWorldPositionToGrid(Vec2{15.f, -1.f}, shifted);
        CHECK(snapped.x == 8.f);
        CHECK(snapped.y == -8.f);

        EditorSceneViewState view;
        view.gridCellSize = 32.0f;
        const SceneGridDefinition fromView = worldAuthoringGrid(view);
        CHECK(fromView.cellSize.x == 32.0f);
        CHECK(fromView.origin.x == 0.0f);
        CHECK(fromView.origin.y == 0.0f);
        CHECK(snapWorldPositionToGrid(Vec2{47.f, -15.f}, fromView).x == 32.f);
        CHECK(snapWorldPositionToGrid(Vec2{47.f, -15.f}, fromView).y == 0.f);
        CHECK(snapWorldPositionToGrid(Vec2{47.f, -17.f}, fromView).y == -32.f);

        CHECK(visualGridStrideForZoom(1.0f, 0.1f) > 1);
        const SceneGridDefinition tiny{
            SceneGridKind::World,
            Vec2{1.0f, 1.0f},
            Vec2{0.0f, 0.0f},
        };
        CHECK(snapWorldPositionToGrid(Vec2{2.4f, 0.0f}, tiny).x == 2.0f);
    }

    // -- Contextual grid resolver: world vs tilemap by active tool -------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        CHECK(c.execute(SetEntityTransformCommand{kSceneA, kHero, {213.f, 119.f}}).ok);
        CHECK(c.execute(SetTilemapCellSizeCommand{kSceneA, kHero, {16.f, 16.f}}).ok);
        c.apply(SetSceneGridCellSizeIntent{kSceneA, 32.0f});

        const auto worldToCell = [](Vec2 world, Vec2 origin, Vec2 cellSize) {
            return TilemapCellCoord{
                static_cast<int>(std::floor((world.x - origin.x) / cellSize.x)),
                static_cast<int>(std::floor((world.y - origin.y) / cellSize.y)),
            };
        };

        c.apply(SetActiveToolIntent{EditorTool::Brush});
        CHECK(c.state().activeTool == EditorTool::Brush);
        CHECK(selectionSupportsTilemapEditing(c.document(), c.state(), kSceneA));
        const SceneGridDefinition brushGrid =
            viewportDisplayGrid(c.document(), c.state(), kSceneA);
        CHECK(brushGrid.kind == SceneGridKind::Tilemap);
        CHECK(brushGrid.origin.x == 213.f);
        CHECK(brushGrid.origin.y == 119.f);
        CHECK(brushGrid.cellSize.x == 16.f);
        CHECK(brushGrid.cellSize.y == 16.f);

        const SceneGridDefinition worldWhileBrush = worldAuthoringGrid(c.sceneView(kSceneA));
        CHECK(worldWhileBrush.kind == SceneGridKind::World);
        CHECK(worldWhileBrush.cellSize.x == 32.f);
        CHECK(snapWorldPositionToGrid(Vec2{220.f, 130.f}, worldWhileBrush).x == 224.f);
        CHECK(snapWorldPositionToGrid(Vec2{220.f, 130.f}, worldWhileBrush).y == 128.f);

        const TilemapCellCoord painted =
            worldToCell(Vec2{220.f, 130.f}, brushGrid.origin, brushGrid.cellSize);
        CHECK(painted.cellX == 0);
        CHECK(painted.cellY == 0);

        const TilemapCellCoord withSnapOn = painted;
        c.apply(SetSceneGridSnapEnabledIntent{kSceneA, true});
        CHECK(withSnapOn == worldToCell(Vec2{220.f, 130.f}, brushGrid.origin, brushGrid.cellSize));
        c.apply(SetSceneGridSnapEnabledIntent{kSceneA, false});
        CHECK(withSnapOn == worldToCell(Vec2{220.f, 130.f}, brushGrid.origin, brushGrid.cellSize));

        const uint64_t revBeforeContext = c.document().revision();
        c.apply(SetActiveToolIntent{EditorTool::Select});
        CHECK(c.document().revision() == revBeforeContext);
        c.apply(SetActiveToolIntent{EditorTool::Brush});
        CHECK(c.document().revision() == revBeforeContext);
        c.apply(SetSceneGridVisibilityIntent{kSceneA, false});
        CHECK(viewportDisplayGrid(c.document(), c.state(), kSceneA).kind == SceneGridKind::Tilemap);
        c.apply(SetSceneGridVisibilityIntent{kSceneA, true});

        c.apply(SetActiveToolIntent{EditorTool::Select});
        const SceneGridDefinition selectGrid =
            viewportDisplayGrid(c.document(), c.state(), kSceneA);
        CHECK(selectGrid.kind == SceneGridKind::World);
        CHECK(selectGrid.origin.x == 0.f);
        CHECK(selectGrid.origin.y == 0.f);
        CHECK(selectGrid.cellSize.x == 32.f);

        c.apply(SetActiveToolIntent{EditorTool::Brush});
        CHECK(c.execute(SetEntityTransformCommand{kSceneA, kHero, {100.f, 50.f}}).ok);
        const SceneGridDefinition moved =
            viewportDisplayGrid(c.document(), c.state(), kSceneA);
        CHECK(moved.origin.x == 100.f);
        CHECK(moved.origin.y == 50.f);

        CHECK(c.execute(SetTilemapCellSizeCommand{kSceneA, kHero, {16.f, 32.f}}).ok);
        const SceneGridDefinition rectangular =
            viewportDisplayGrid(c.document(), c.state(), kSceneA);
        CHECK(rectangular.cellSize.x == 16.f);
        CHECK(rectangular.cellSize.y == 32.f);

        CHECK(c.execute(SetEntityTransformCommand{kSceneA, kHero, {-48.f, -32.f}}).ok);
        const SceneGridDefinition negative =
            viewportDisplayGrid(c.document(), c.state(), kSceneA);
        CHECK(negative.origin.x == -48.f);
        CHECK(negative.origin.y == -32.f);
        const TilemapCellCoord negCell =
            worldToCell(Vec2{-40.f, -20.f}, negative.origin, negative.cellSize);
        CHECK(negCell.cellX == 0);
        CHECK(negCell.cellY == 0);

        c.apply(SetActiveToolIntent{EditorTool::Select});
        CHECK(tilemapCellGrid(c.document(), c.state(), kSceneA).has_value());
        CHECK(viewportDisplayGrid(c.document(), c.state(), kSceneA).kind == SceneGridKind::World);
    }

    // -- selectionSupportsTilemapEditing: default layer, layer scope, locks ---
    {
        // Every current scene has a default layer and a selected instance is
        // scoped to it. Empty-layer fixtures belonged to the retired format.
        {
            EditorCoordinator c{makeSpriteDoc()};
            const SceneDef* scene = c.document().findScene(kSceneA);
            CHECK(scene != nullptr);
            CHECK(!scene->layers.empty());
            CHECK(!scene->defaultLayerId.empty());
            CHECK(scene->instances.front().layerId == scene->defaultLayerId);
            CHECK(c.state().activeSceneId == kSceneA);
            setUpTilemapForPainting(c);
            CHECK(selectionSupportsTilemapEditing(c.document(), c.state(), kSceneA));
        }

        // Legacy scene, entity without TilemapComponent.
        {
            EditorCoordinator c{makeSpriteDoc()};
            CHECK(c.state().activeSceneId == kSceneA);
            CHECK(c.apply(SelectEntityIntent{kHero}).ok);
            CHECK(!selectionSupportsTilemapEditing(c.document(), c.state(), kSceneA));
        }

        // Modern scene: active layer matches entity layer.
        {
            EditorCoordinator c{ProjectDoc{}};
            CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
            c.apply(SelectSceneIntent{"s"});
            CHECK(c.state().activeSceneId == "s");
            CHECK(c.execute(AddImageAssetCommand{"img-1", "some/path.png"}).ok);
            CHECK(c.execute(AddTilesetAssetCommand{"tiles-1", "Tiles", "img-1", TilesetSlicing{}}).ok);
            sliceTilesOne(c);
            TilemapComponent tm;
            tm.tilesetAssetId = "tiles-1";
            tm.chunkSize = 16;
            CHECK(c.execute(CreateEntityCommand{"s", 1, "Hero", "Hero", {}}).ok);
            CHECK(c.execute(AddTilemapComponentCommand{"s", 1, tm}).ok);
            CHECK(c.apply(SelectEntityIntent{1}).ok);
            CHECK(selectionSupportsTilemapEditing(c.document(), c.state(), "s"));
        }

        // Modern scene: entity layer differs from the workspace active layer.
        {
            EditorCoordinator c{ProjectDoc{}};
            CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
            c.apply(SelectSceneIntent{"s"});
            CHECK(c.execute(AddImageAssetCommand{"img-1", "some/path.png"}).ok);
            CHECK(c.execute(AddTilesetAssetCommand{"tiles-1", "Tiles", "img-1", TilesetSlicing{}}).ok);
            CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
            sliceTilesOne(c);
            TilemapComponent tm;
            tm.tilesetAssetId = "tiles-1";
            tm.chunkSize = 16;
            CHECK(c.execute(CreateEntityCommand{"s", 1, "Hero", "Hero", {}, "layer-1"}).ok);
            CHECK(c.execute(AddTilemapComponentCommand{"s", 1, tm}).ok);
            CHECK(c.apply(SelectEntityIntent{1}).ok);
            EditorState mismatched = c.state();
            mismatched.sceneViews["s"].activeLayerId = "fg";
            CHECK(!selectionSupportsTilemapEditing(c.document(), mismatched, "s"));
        }

        // Matching layer but locked.
        {
            ProjectDoc doc = makeSpriteDoc();
            SceneDef& scene = doc.scenes.at(kSceneA);
            SceneLayerDef locked;
            locked.id = "locked-layer";
            locked.name = "Locked";
            locked.locked = true;
            scene.layers.push_back(locked);
            scene.defaultLayerId = "locked-layer";
            TilemapComponent tm;
            tm.tilesetAssetId = "tiles-1";
            tm.chunkSize = 16;
            scene.instances.front().tilemap = tm;
            scene.instances.front().layerId = "locked-layer";
            EditorCoordinator c{doc};
            CHECK(c.apply(SelectEntityIntent{kHero}).ok);
            CHECK(!selectionSupportsTilemapEditing(c.document(), c.state(), kSceneA));
        }

        // sceneId must match the active scene.
        {
            EditorCoordinator c{makeSpriteDoc()};
            setUpTilemapForPainting(c);
            CHECK(!selectionSupportsTilemapEditing(c.document(), c.state(), kSceneB));
        }

        // No selection.
        {
            EditorCoordinator c{makeSpriteDoc()};
            setUpTilemapForPainting(c);
            CHECK(c.apply(SelectEntityIntent{INVALID_ENTITY}).ok);
            CHECK(!selectionSupportsTilemapEditing(c.document(), c.state(), kSceneA));
        }
    }

    // -- Grid hidden: overlay off, tilemap paint input unchanged ---------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        c.apply(SetActiveToolIntent{EditorTool::Brush});
        c.apply(SetSceneGridVisibilityIntent{kSceneA, false});
        CHECK(!c.sceneView(kSceneA).gridVisible);
        CHECK(viewportDisplayGrid(c.document(), c.state(), kSceneA).kind == SceneGridKind::Tilemap);
        CHECK(c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Brush, {0, 0}}).ok);
        CHECK(c.state().tilemapEditor.pendingStroke.has_value());
        c.apply(EndTilePaintStrokeIntent{});
    }

    // -- SceneGridPresentation: toolbar projection (no document mutation) ------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        CHECK(c.execute(SetEntityTransformCommand{kSceneA, kHero, {213.f, 119.f}}).ok);
        CHECK(c.execute(SetTilemapCellSizeCommand{kSceneA, kHero, {16.f, 32.f}}).ok);
        c.apply(SetActiveToolIntent{EditorTool::Brush});

        const SceneGridPresentation brushPres =
            makeSceneGridPresentation(c.document(), c.state(), kSceneA);
        CHECK(brushPres.kind == SceneGridKind::Tilemap);
        CHECK(brushPres.contextName == "Hero");
        CHECK(brushPres.toolbarContextName == "Hero");
        CHECK(brushPres.sizeEditable == false);
        CHECK(brushPres.cellSize.x == 16.f);
        CHECK(brushPres.cellSize.y == 32.f);
        CHECK(brushPres.sourceEntityId == kHero);
        CHECK(brushPres.toolbarTooltip == "Tilemap grid from \"Hero\"");

        CHECK(c.execute(RenameEntityCommand{kSceneA, kHero, "Terrain"}).ok);
        const SceneGridPresentation renamed =
            makeSceneGridPresentation(c.document(), c.state(), kSceneA);
        CHECK(renamed.contextName == "Terrain");
        CHECK(renamed.toolbarContextName == "Terrain");

        c.apply(SetActiveToolIntent{EditorTool::Select});
        const SceneGridPresentation selectPres =
            makeSceneGridPresentation(c.document(), c.state(), kSceneA);
        CHECK(selectPres.kind == SceneGridKind::World);
        CHECK(selectPres.contextName == "World");
        CHECK(selectPres.sizeEditable == true);
        CHECK(selectPres.sourceEntityId == std::nullopt);

        CHECK(c.execute(RenameEntityCommand{kSceneA, kHero, "Environment Decorations"}).ok);
        c.apply(SetActiveToolIntent{EditorTool::Brush});
        const SceneGridPresentation longName =
            makeSceneGridPresentation(c.document(), c.state(), kSceneA);
        CHECK(longName.contextName == "Environment Decorations");
        CHECK(longName.toolbarContextName.size() < longName.contextName.size());
        CHECK(longName.toolbarContextName != longName.contextName);

        CHECK(c.execute(RenameEntityCommand{kSceneA, kHero, "Decorazioni citt├á meravigliosa"}).ok);
        const SceneGridPresentation accented =
            makeSceneGridPresentation(c.document(), c.state(), kSceneA);
        CHECK(accented.contextName == "Decorazioni citt├á meravigliosa");
        CHECK(accented.toolbarContextName.size() < accented.contextName.size());
        CHECK(accented.toolbarContextName.find("Decorazioni") == 0);
        // Truncation must not split the multibyte "├á" (0xC3 0xA0).
        CHECK(accented.toolbarContextName.find("cit\xC3") == std::string::npos);
        CHECK(accented.toolbarContextName.find("\xE2\x80\xA6") != std::string::npos);
    }

    // -- ViewportPointerReadout: world + cell from contextual grid -------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        CHECK(c.execute(SetEntityTransformCommand{kSceneA, kHero, {213.f, 119.f}}).ok);
        CHECK(c.execute(SetTilemapCellSizeCommand{kSceneA, kHero, {16.f, 16.f}}).ok);
        c.apply(SetActiveToolIntent{EditorTool::Brush});

        const ViewportRect rect{0, 0, 800, 600};
        const SceneViewCamera camera =
            makeSceneViewCamera(rect, c.sceneView(kSceneA), Vec2{1920.f, 1080.f});
        const Vec2 screen = Vec2{400.f, 300.f};
        const ViewportPointerReadout readout =
            makePointerReadout(screen, camera, c.document(), c.state(), kSceneA);
        CHECK(readout.valid);
        CHECK(readout.tilemapCell.has_value());
        CHECK(readout.tilemapEntityId == kHero);
        CHECK(readout.tilemapCell->cellX
              == worldPositionToTilemapCell(readout.worldPosition, {213.f, 119.f}, {16.f, 16.f}).cellX);
        CHECK(readout.tilemapCell->cellY
              == worldPositionToTilemapCell(readout.worldPosition, {213.f, 119.f}, {16.f, 16.f}).cellY);

        const TilemapCellCoord fromWorld =
            worldPositionToTilemapCell(Vec2{245.f, 183.f}, {213.f, 119.f}, {16.f, 16.f});
        CHECK(fromWorld.cellX == 2);
        CHECK(fromWorld.cellY == 4);

        const std::string formatted = formatViewportPointerReadout(readout);
        CHECK(formatted.find("World ") == 0);
        CHECK(formatted.find("Cell ") != std::string::npos);

        c.apply(SetActiveToolIntent{EditorTool::Select});
        const ViewportPointerReadout selectOnly =
            makePointerReadout(screen, camera, c.document(), c.state(), kSceneA);
        CHECK(!selectOnly.tilemapCell.has_value());
        CHECK(formatViewportPointerReadout(selectOnly).find("Cell ") == std::string::npos);

        c.apply(SetActiveToolIntent{EditorTool::Brush});
        c.apply(SetSceneGridVisibilityIntent{kSceneA, false});
        const ViewportPointerReadout gridHidden =
            makePointerReadout(screen, camera, c.document(), c.state(), kSceneA);
        CHECK(gridHidden.tilemapCell.has_value());

        const TilemapCellCoord neg =
            worldPositionToTilemapCell(Vec2{-40.f, -20.f}, {213.f, 119.f}, {16.f, 16.f});
        CHECK(neg.cellX == -16);
        CHECK(neg.cellY == -9);
    }

    // -- RevealInspectorPropertyIntent: navigation only, no dirty/history ------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        const uint64_t revision = c.document().revision();
        const std::size_t undoBefore = c.undoSize();
        const bool dirtyBefore = c.document().isDirty();

        CHECK(c.apply(RevealInspectorPropertyIntent{kHero, InspectorProperty::TilemapCellSize}).ok);
        CHECK(c.uiState().inspectorRevealRequest.has_value());
        CHECK(c.uiState().inspectorRevealRequest->entityId == kHero);
        CHECK(c.uiState().inspectorRevealRequest->property == InspectorProperty::TilemapCellSize);
        CHECK(c.selection().primaryEntity == kHero);
        CHECK(c.document().revision() == revision);
        CHECK(c.undoSize() == undoBefore);
        CHECK(c.document().isDirty() == dirtyBefore);

        const std::optional<InspectorRevealRequest> taken = c.takeInspectorRevealRequest();
        CHECK(taken.has_value());
        CHECK(!c.uiState().inspectorRevealRequest.has_value());

        CHECK(!c.apply(RevealInspectorPropertyIntent{9999, InspectorProperty::TilemapCellSize}).ok);
    }

    // -- Auto-fit flag lives in the scene view state, cleared by replaceProject
    {
        EditorCoordinator c{makeDoc()};
        CHECK(!c.sceneView(kSceneA).initialized);
        c.markSceneViewInitialized(kSceneA);
        CHECK(c.sceneView(kSceneA).initialized);
        c.apply(SetViewportZoomIntent{kSceneA, 2.0f});
        CHECK(c.sceneView(kSceneA).initialized);          // survives camera edits
        CHECK(c.replaceProject(ProjectDocument{makeReplacementDoc()}).ok);
        CHECK(!c.sceneView(kSceneA).initialized);          // sceneViews cleared on replace
    }

    // -- Start/Stop Play does not disturb the Edit camera ---------------------
    {
        EditorCoordinator c{makeDoc()};
        c.apply(SetViewportZoomIntent{kSceneA, 2.5f});
        c.apply(PanViewportIntent{kSceneA, {12.f, -8.f}});
        c.apply(SetSceneGridVisibilityIntent{kSceneA, false});
        c.apply(SetSceneGridSnapEnabledIntent{kSceneA, true});
        c.apply(SetSceneGridCellSizeIntent{kSceneA, 8.0f});
        CHECK(c.playProject().ok);
        CHECK(c.stopPlaying().ok);
        CHECK(c.sceneView(kSceneA).zoom == 2.5f);
        CHECK(c.sceneView(kSceneA).pan.x == 12.f);
        CHECK(c.sceneView(kSceneA).pan.y == -8.f);
        CHECK(!c.sceneView(kSceneA).gridVisible);
        CHECK(c.sceneView(kSceneA).gridSnapEnabled);
        CHECK(c.sceneView(kSceneA).gridCellSize == 8.0f);
    }

    // == Scene layers =========================================================

    // -- New scene has Layer 1 as the default layer; new entities use it -------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        const SceneDef* scene = c.document().findScene("s");
        CHECK(scene->layers.size() == 1);
        CHECK(scene->defaultLayerId == "layer-1");
        CHECK(scene->layers[0].name == "Layer 1");
        CHECK(c.document().hasLayer("s", "layer-1"));
        CHECK(!c.document().hasLayer("s", "nope"));

        c.apply(SelectSceneIntent{"s"});
        CHECK(addEntity(c).ok);
        const EntityId id = nextAvailableEntityId(c.document(), "s") - 1;
        CHECK(c.document().findInstanceInScene("s", id)->layerId == "layer-1");
    }

    // -- Add / rename / move / remove; default + non-empty protected ----------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        CHECK(c.execute(AddSceneLayerCommand{"s", "bg", "Background", 0}).ok);
        {
            const SceneDef* s = c.document().findScene("s");
            CHECK(s->layers.size() == 3);
            CHECK(s->layers[0].id == "bg");
            CHECK(s->layers[1].id == "layer-1");
            CHECK(s->layers[2].id == "fg");
        }
        CHECK(!c.execute(AddSceneLayerCommand{"s", "fg", "Dup", 0}).ok);       // dup id
        CHECK(!c.execute(AddSceneLayerCommand{"s", "x", "Foreground", 0}).ok); // dup name
        CHECK(c.execute(RenameSceneLayerCommand{"s", "fg", "Top"}).ok);
        CHECK(c.document().findScene("s")->layers[2].name == "Top");
        CHECK(c.execute(MoveSceneLayerCommand{"s", "bg", 2}).ok);              // bg -> top
        CHECK(c.document().findScene("s")->layers[2].id == "bg");
        CHECK(!c.execute(RemoveSceneLayerCommand{"s", "layer-1"}).ok);        // default protected
        CHECK(c.execute(RemoveSceneLayerCommand{"s", "fg"}).ok);              // empty -> removed
        CHECK(c.document().findScene("s")->layers.size() == 2);
        // undo the remove restores it at its original index
        CHECK(c.undo().ok);
        CHECK(c.document().hasLayer("s", "fg"));
    }

    // -- A non-empty layer cannot be removed ----------------------------------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Hero", "Hero", {}, "fg"}).ok);
        CHECK(!c.execute(RemoveSceneLayerCommand{"s", "fg"}).ok);
    }

    // -- Rename validates names and default identity is by id ------------------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        CHECK(c.execute(AddSceneLayerCommand{"s", "bg", "Background", 0}).ok);

        CHECK(!c.execute(RenameSceneLayerCommand{"s", "fg", ""}).ok);
        CHECK(!c.execute(RenameSceneLayerCommand{"s", "fg", "background"}).ok);
        CHECK(!c.execute(RenameSceneLayerCommand{"s", "missing", "Gameplay"}).ok);

        const uint64_t beforeNoopRevision = c.document().revision();
        const std::size_t beforeNoopUndo = c.undoSize();
        CHECK(c.execute(RenameSceneLayerCommand{"s", "fg", "Foreground"}).ok);
        CHECK(c.document().revision() == beforeNoopRevision);
        CHECK(c.undoSize() == beforeNoopUndo);

        CHECK(c.execute(RenameSceneLayerCommand{"s", "layer-1", "Gameplay"}).ok);
        const SceneDef* scene = c.document().findScene("s");
        CHECK(scene->defaultLayerId == "layer-1");
        CHECK(scene->layers[1].id == "layer-1");
        CHECK(scene->layers[1].name == "Gameplay");
        CHECK(!c.execute(RemoveSceneLayerCommand{"s", "layer-1"}).ok);

        CHECK(c.undo().ok);
        CHECK(c.document().findScene("s")->defaultLayerId == "layer-1");
        CHECK(c.document().findScene("s")->layers[1].name == "Layer 1");
        CHECK(c.redo().ok);
        CHECK(c.document().findScene("s")->defaultLayerId == "layer-1");
        CHECK(c.document().findScene("s")->layers[1].name == "Gameplay");
    }

    // -- SetEntityLayer: same type on different layers; undo/redo -------------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Hero", "A", {}, "layer-1"}).ok);
        CHECK(c.execute(CreateEntityCommand{"s", 2, "obj-1", "B", {}, "fg"}).ok);  // shares type
        CHECK(c.document().findInstanceInScene("s", 1)->objectTypeId
              == c.document().findInstanceInScene("s", 2)->objectTypeId);
        CHECK(c.document().findInstanceInScene("s", 1)->layerId == "layer-1");
        CHECK(c.document().findInstanceInScene("s", 2)->layerId == "fg");

        CHECK(c.execute(SetEntityLayerCommand{"s", 1, "fg"}).ok);
        CHECK(c.document().findInstanceInScene("s", 1)->layerId == "fg");
        CHECK(c.undo().ok);
        CHECK(c.document().findInstanceInScene("s", 1)->layerId == "layer-1");
        CHECK(c.redo().ok);
        CHECK(c.document().findInstanceInScene("s", 1)->layerId == "fg");
        CHECK(!c.execute(SetEntityLayerCommand{"s", 1, "nope"}).ok);   // dest must exist
    }

    // -- Render order follows scene.layers; hidden layers are skipped ---------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);  // [layer-1, fg]
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Bg", "Bg", {}, "layer-1"}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 2, "obj-2", "Fg", "Fg", {}, "fg"}).ok);

        const SceneFrameSnapshot snap = collectSceneFrameSnapshot(c.document(), "s", INVALID_ENTITY);
        CHECK(snap.entities.size() == 2);
        CHECK(snap.entities.front().entityId == 1);   // background drawn first
        CHECK(snap.entities.back().entityId == 2);    // foreground on top

        std::unordered_set<std::string> hidden{"fg"};
        const SceneFrameSnapshot vis =
            collectSceneFrameSnapshot(c.document(), "s", INVALID_ENTITY, hidden);
        CHECK(vis.entities.size() == 1);
        CHECK(vis.entities.front().entityId == 1);    // hidden fg excluded
    }

    // -- Editor visibility is workspace-only; Play renders the layer anyway ----
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Fg", "Fg", {}, "fg"}).ok);
        const uint64_t rev = c.document().revision();
        const bool dirtyBefore = c.document().isDirty();
        c.apply(ToggleLayerEditorVisibilityIntent{"s", "fg"});
        CHECK(c.sceneView("s").hiddenLayerIds.count("fg") == 1);
        CHECK(c.document().isDirty() == dirtyBefore);   // workspace only: dirty unchanged
        CHECK(c.document().revision() == rev);
        CHECK(c.playProject().ok);                     // "s" is the start scene (first scene)
        const SceneFrameSnapshot play = collectSceneFrameSnapshot(*c.playSession());
        CHECK(play.entities.size() == 1);              // editor-hidden does not affect Play
        CHECK(c.stopPlaying().ok);
    }

    // -- replaceProject drops stale active/hidden layer workspace -------------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        c.apply(SetActiveLayerIntent{"s", "fg"});
        c.apply(ToggleLayerEditorVisibilityIntent{"s", "fg"});
        CHECK(c.sceneView("s").activeLayerId == "fg");
        CHECK(c.replaceProject(ProjectDocument{makeReplacementDoc()}).ok);
        CHECK(c.sceneView("s").activeLayerId.empty());
        CHECK(c.sceneView("s").hiddenLayerIds.empty());
    }

    // -- Save/reload preserves layers, order and assignment -------------------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);  // [layer-1, fg]
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Hero", "Hero", {}, "fg"}).ok);
        const std::filesystem::path path = testTempDir() / "layers.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);
        EditorCoordinator r{ProjectDoc{}};
        CHECK(loadProjectFromFile(r, path).ok);
        const SceneDef* s = r.document().findScene("s");
        CHECK(s->layers.size() == 2);
        CHECK(s->layers[0].id == "layer-1");
        CHECK(s->layers[1].id == "fg");
        CHECK(s->defaultLayerId == "layer-1");
        CHECK(r.document().findInstanceInScene("s", 1)->layerId == "fg");
    }

    // -- A legacy file with no layers migrates to Layer 1 ----------------------
    {
        EditorCoordinator c{ProjectDoc{}};
        const auto loaded = loadProjectFromText(c,
            R"({"activeSceneId":"s","scenes":[{"id":"s","instances":[)"
            R"({"id":1,"objectTypeId":"T","instanceName":"T"}]}],)"
            R"("objectTypes":[{"id":"T"}]})");
        CHECK(loaded.ok);
        const SceneDef* s = c.document().findScene("s");
        CHECK(!s->layers.empty());
        CHECK(s->layers.front().id == "layer-1");
        CHECK(s->layers.front().name == "Layer 1");
        CHECK(s->defaultLayerId == s->layers.front().id);
        CHECK(c.document().findInstanceInScene("s", 1)->layerId == s->defaultLayerId);
    }

    // -- The old untouched Default layer migrates to Layer 1 -------------------
    {
        EditorCoordinator c{ProjectDoc{}};
        const auto loaded = loadProjectFromText(c,
            R"({"activeSceneId":"s","scenes":[{"id":"s",)"
            R"("defaultLayerId":"default",)"
            R"("layers":[{"id":"default","name":"Default"}],)"
            R"("instances":[{"id":1,"objectTypeId":"T","instanceName":"T","layerId":"default"}]}],)"
            R"("objectTypes":[{"id":"T"}]})");
        CHECK(loaded.ok);
        const SceneDef* s = c.document().findScene("s");
        CHECK(s->layers.size() == 1);
        CHECK(s->layers[0].id == "layer-1");
        CHECK(s->layers[0].name == "Layer 1");
        CHECK(s->defaultLayerId == "layer-1");
        CHECK(c.document().findInstanceInScene("s", 1)->layerId == "layer-1");
    }

    // == Layer locking ===========================================================

    // -- SetLayerLockedCommand: lock, unlock, no-op, undo/redo, invalid targets -
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        CHECK(!c.document().findScene("s")->layers[0].locked);

        CHECK(!c.execute(SetLayerLockedCommand{"nope", "layer-1", true}).ok);   // missing scene
        CHECK(!c.execute(SetLayerLockedCommand{"s", "missing", true}).ok);      // missing layer

        const uint64_t revBefore = c.document().revision();
        const std::size_t undoBefore = c.undoSize();
        CHECK(c.execute(SetLayerLockedCommand{"s", "layer-1", false}).ok);      // no-op
        CHECK(c.document().revision() == revBefore);
        CHECK(c.undoSize() == undoBefore);

        CHECK(c.execute(SetLayerLockedCommand{"s", "layer-1", true}).ok);
        CHECK(c.document().findScene("s")->layers[0].locked);
        CHECK(c.document().isLayerLocked("s", "layer-1"));
        CHECK(c.undo().ok);
        CHECK(!c.document().findScene("s")->layers[0].locked);
        CHECK(c.redo().ok);
        CHECK(c.document().findScene("s")->layers[0].locked);

        CHECK(c.execute(SetLayerLockedCommand{"s", "layer-1", false}).ok);
        CHECK(!c.document().isLayerLocked("s", "layer-1"));
    }

    // -- Persistence: locked round-trips; an absent field defaults to false ----
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        CHECK(c.execute(SetLayerLockedCommand{"s", "fg", true}).ok);
        const std::filesystem::path path = testTempDir() / "layer-lock.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);
        EditorCoordinator r{ProjectDoc{}};
        CHECK(loadProjectFromFile(r, path).ok);
        const SceneDef* s = r.document().findScene("s");
        CHECK(s->layers[0].id == "layer-1" && !s->layers[0].locked);   // absent -> false
        CHECK(s->layers[1].id == "fg" && s->layers[1].locked);         // round-tripped true
    }

    // -- Locked layer blocks RemoveSceneLayerCommand and both directions of
    // SetEntityLayerCommand ----------------------------------------------------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        CHECK(c.execute(SetLayerLockedCommand{"s", "fg", true}).ok);
        CHECK(!c.execute(RemoveSceneLayerCommand{"s", "fg"}).ok);       // locked, even if empty
        CHECK(c.execute(SetLayerLockedCommand{"s", "fg", false}).ok);
        CHECK(c.execute(RemoveSceneLayerCommand{"s", "fg"}).ok);        // unlocked: removable again

        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Hero", "Hero", {}, "layer-1"}).ok);
        CHECK(c.execute(SetLayerLockedCommand{"s", "fg", true}).ok);
        CHECK(!c.execute(SetEntityLayerCommand{"s", 1, "fg"}).ok);      // target locked
        CHECK(c.execute(SetLayerLockedCommand{"s", "fg", false}).ok);
        CHECK(c.execute(SetLayerLockedCommand{"s", "layer-1", true}).ok);
        CHECK(!c.execute(SetEntityLayerCommand{"s", 1, "fg"}).ok);      // source locked
        CHECK(c.execute(SetLayerLockedCommand{"s", "layer-1", false}).ok);
        CHECK(c.execute(SetEntityLayerCommand{"s", 1, "fg"}).ok);       // both unlocked: succeeds
    }

    // -- Regression: SetEntityLayerCommand's lock checks must gate on captured_,
    // exactly like every other command's lock check - not re-run on redo. Drives
    // the Command objects directly (apply/undo/apply again) to mirror exactly
    // what EditorCoordinator's history does on redo: reuse the same object,
    // call apply() a second time - without going through the coordinator's own
    // undo/redo stack, since executing SetLayerLockedCommand in between would
    // itself clear that stack's redo entry and defeat the point of the test. --
    {
        ProjectDocument doc{ProjectDoc{}};
        CreateSceneCommand createScene{"s", "S"};
        CHECK(createScene.apply(doc).ok);
        AddSceneLayerCommand addFg{"s", "fg", "Foreground", 1};
        CHECK(addFg.apply(doc).ok);
        CreateEntityWithDefaultTypeCommand createHero{
            "s", 1, "obj-1", "Hero", "Hero", {}, "layer-1"};
        CHECK(createHero.apply(doc).ok);

        SetEntityLayerCommand moveToFg{"s", 1, "fg"};
        CHECK(moveToFg.apply(doc).ok);         // both layers unlocked at the time
        CHECK(doc.findInstanceInScene("s", 1)->layerId == "fg");
        CHECK(moveToFg.undo(doc).ok);
        CHECK(doc.findInstanceInScene("s", 1)->layerId == "layer-1");

        SetLayerLockedCommand lockLayer1{"s", "layer-1", true};
        CHECK(lockLayer1.apply(doc).ok);   // now-source is locked
        CHECK(moveToFg.apply(doc).ok);     // "redo": must still succeed, not re-validated
        CHECK(doc.findInstanceInScene("s", 1)->layerId == "fg");
    }

    // -- Regression: RemoveSceneLayerCommand's lock check must gate on captured_
    // too - not re-run on redo (same direct-Command technique as above) --------
    {
        ProjectDocument doc{ProjectDoc{}};
        CreateSceneCommand createScene{"s", "S"};
        CHECK(createScene.apply(doc).ok);
        AddSceneLayerCommand addFg{"s", "fg", "Foreground", 1};
        CHECK(addFg.apply(doc).ok);

        RemoveSceneLayerCommand removeFg{"s", "fg"};
        CHECK(removeFg.apply(doc).ok);   // unlocked and empty at the time
        CHECK(!doc.hasLayer("s", "fg"));
        CHECK(removeFg.undo(doc).ok);    // restores "fg", still unlocked
        CHECK(doc.hasLayer("s", "fg"));

        SetLayerLockedCommand lockFg{"s", "fg", true};
        CHECK(lockFg.apply(doc).ok);
        CHECK(removeFg.apply(doc).ok);   // "redo": must still succeed despite the current lock
        CHECK(!doc.hasLayer("s", "fg"));
    }

    // -- Locked layer blocks every instance-owned mutation ----------------------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Hero", "Hero", {}, "layer-1"}).ok);
        CHECK(c.execute(AddSpriteRendererToObjectTypeCommand{"obj-1"}).ok);
        CHECK(c.execute(SetLayerLockedCommand{"s", "layer-1", true}).ok);

        CHECK(!c.execute(SetEntityTransformCommand{"s", 1, {5.f, 5.f}}).ok);
        CHECK(!c.execute(RenameEntityCommand{"s", 1, "New Name"}).ok);
        CHECK(!c.execute(CloneInstanceCommand{"s", 1, 2, "Clone", {}}).ok);
        SpriteRendererOverride lockedDelta; lockedDelta.visible = false;
        CHECK(!c.execute(SetInstanceSpriteOverrideCommand{"s", 1, lockedDelta}).ok);
        CHECK(!c.execute(AddTilemapComponentCommand{"s", 1, TilemapComponent{}}).ok);
        CHECK(!c.execute(DeleteEntityCommand{"s", 1}).ok);
        // Creating a new instance directly into the locked layer is rejected too.
        CHECK(!c.execute(CreateEntityCommand{"s", 2, "obj-1", "B", {}, "layer-1"}).ok);

        CHECK(c.execute(SetLayerLockedCommand{"s", "layer-1", false}).ok);
        CHECK(c.execute(SetEntityTransformCommand{"s", 1, {5.f, 5.f}}).ok);   // unlocked: succeeds
    }

    // -- Object-type-owned components stay editable regardless of the instance's
    // layer lock - including when the type is shared with an unlocked instance --
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "A", "A", {}, "layer-1"}).ok);
        CHECK(c.execute(CreateEntityCommand{"s", 2, "obj-1", "B", {}, "fg"}).ok);  // shares obj-1
        CHECK(c.execute(SetLayerLockedCommand{"s", "layer-1", true}).ok);
        // Entity 1 sits on the now-locked layer-1, but Box Collider 2D belongs
        // to the shared object type obj-1, not to entity 1 specifically.
        CHECK(c.execute(AddBoxColliderCommand{"obj-1"}).ok);
        CHECK(c.execute(SetBoxColliderSizeCommand{"obj-1", {10.f, 10.f}}).ok);
        CHECK(c.execute(AddLinearMoverCommand{"obj-1"}).ok);
        CHECK(c.execute(SetLinearMoverSpeedCommand{"obj-1", 5.f}).ok);
        CHECK(c.execute(RemoveLinearMoverCommand{"obj-1"}).ok);
    }

    // -- FillTilemapIntent: locked layer rejected (mirrors BeginTilePaintStrokeIntent
    // and BeginTileRectangleIntent's own guards, tested earlier in this file) --
    {
        ProjectDoc doc = makeSpriteDoc();
        SceneDef& scene = doc.scenes.at(kSceneA);
        SceneLayerDef locked;
        locked.id = "locked-layer";
        locked.name = "Locked";
        locked.locked = true;
        scene.layers.push_back(locked);
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        scene.instances.front().tilemap = tm;
        scene.instances.front().layerId = "locked-layer";
        EditorCoordinator c{doc};
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(!c.apply(FillTilemapIntent{kSceneA, kHero, {0, 0}}).ok);
    }

    // -- Play, Hierarchy selection, visibility/rename/reorder/unlock all ignore
    // the lock - only instance-owned authoring commands respect it -------------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        c.apply(SelectSceneIntent{"s"});
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Hero", "Hero", {}, "layer-1"}).ok);
        CHECK(c.execute(SetLayerLockedCommand{"s", "layer-1", true}).ok);

        c.apply(SelectEntityIntent{1});                       // Hierarchy selection unaffected
        CHECK(c.selection().primaryEntity == 1);
        c.apply(ToggleLayerEditorVisibilityIntent{"s", "layer-1"});
        CHECK(c.sceneView("s").hiddenLayerIds.count("layer-1") == 1);
        CHECK(c.execute(RenameSceneLayerCommand{"s", "layer-1", "Gameplay"}).ok);
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        CHECK(c.execute(MoveSceneLayerCommand{"s", "layer-1", 1}).ok);   // reorder still allowed

        CHECK(c.playProject().ok);   // Play is authoring-only protection - never blocked by lock
        CHECK(c.playSession() != nullptr);
        CHECK(c.stopPlaying().ok);

        CHECK(c.execute(SetLayerLockedCommand{"s", "layer-1", false}).ok);
        CHECK(!c.document().isLayerLocked("s", "layer-1"));
    }

    // -- Undo/Redo of an action taken before the lock still work after the layer
    // is locked - the gate only guards the new user action, not history replay -
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Hero", "Hero", {}, "layer-1"}).ok);
        CHECK(c.execute(SetEntityTransformCommand{"s", 1, {5.f, 5.f}}).ok);   // before the lock
        CHECK(c.execute(SetLayerLockedCommand{"s", "layer-1", true}).ok);

        CHECK(c.undo().ok);   // undoes SetLayerLocked (LIFO) - back to unlocked
        CHECK(!c.document().isLayerLocked("s", "layer-1"));
        CHECK(c.undo().ok);   // undoes the position change from before the lock
        CHECK(c.document().findInstanceInScene("s", 1)->transform.position.x == 0.f);
        CHECK(c.redo().ok);   // redo the position change
        CHECK(c.document().findInstanceInScene("s", 1)->transform.position.x == 5.f);
        CHECK(c.redo().ok);   // redo the lock itself
        CHECK(c.document().isLayerLocked("s", "layer-1"));
    }

    // -- Delete-then-undo across a lock: locking after a delete must not corrupt
    // the undo path (undo/redo apply to ProjectDocument directly, never gated) -
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Hero", "Hero", {}, "fg"}).ok);
        CHECK(c.execute(DeleteEntityCommand{"s", 1}).ok);
        CHECK(c.execute(SetLayerLockedCommand{"s", "fg", true}).ok);
        CHECK(c.undo().ok);                                  // undoes SetLayerLocked
        CHECK(c.undo().ok);                                  // undoes DeleteEntity -> entity restored
        CHECK(c.document().findInstanceInScene("s", 1) != nullptr);
    }

    // == Active layer scoping ====================================================
    // activeLayerId(): normalizes to the scene's default layer -----------------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.activeLayerId("no-such-scene").empty());
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        CHECK(c.activeLayerId("s") == "layer-1");   // nothing selected yet -> scene default
    }

    // -- SelectEntityIntent syncs activeLayerId to the entity's own layer ------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        c.apply(SelectSceneIntent{"s"});
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Hero", "Hero", {}, "fg"}).ok);
        CHECK(c.activeLayerId("s") == "layer-1");   // still the default until selected
        c.apply(SelectEntityIntent{1});
        CHECK(c.activeLayerId("s") == "fg");        // synced to the entity's own layer

        // Deselecting leaves activeLayerId exactly where it was.
        c.apply(SelectEntityIntent{INVALID_ENTITY});
        CHECK(c.activeLayerId("s") == "fg");
    }

    // -- SetActiveLayerIntent clears a selection that belongs to the layer just
    // left, and cancels any pending stroke/rectangle regardless of which entity
    // it belongs to; a selection already on the new active layer survives -----
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        c.apply(SelectSceneIntent{"s"});
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        CHECK(c.execute(AddImageAssetCommand{"img-1", "some/path.png"}).ok);
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-1", "Tiles", "img-1", TilesetSlicing{32, 32}}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Ground", "Ground", {}, "layer-1"}).ok);
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        CHECK(c.execute(AddTilemapComponentCommand{"s", 1, tm}).ok);
        c.apply(SelectEntityIntent{1});   // activeLayerId -> layer-1
        CHECK(c.apply(BeginTilePaintStrokeIntent{"s", 1, EditorTool::Eraser, {0, 0}}).ok);
        CHECK(c.state().tilemapEditor.pendingStroke.has_value());

        c.apply(SetActiveLayerIntent{"s", "fg"});   // switch away from entity 1's layer
        CHECK(c.activeLayerId("s") == "fg");
        CHECK(!c.selection().hasEntity());                          // selection cleared
        CHECK(!c.state().tilemapEditor.pendingStroke.has_value());  // pending stroke cancelled
    }
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        c.apply(SelectSceneIntent{"s"});
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Hero", "Hero", {}, "layer-1"}).ok);
        c.apply(SelectEntityIntent{1});
        c.apply(SetActiveLayerIntent{"s", "layer-1"});   // the layer entity 1 is already on
        CHECK(c.selection().primaryEntity == 1);         // survives: still the active layer
    }

    // -- BeginTilePaintStrokeIntent/BeginTileRectangleIntent/FillTilemapIntent
    // all reject an entity outside the active layer, even when unlocked, and
    // all three succeed again once the active layer is switched back ----------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        c.apply(SelectSceneIntent{"s"});
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        CHECK(c.execute(AddImageAssetCommand{"img-1", "some/path.png"}).ok);
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-1", "Tiles", "img-1", TilesetSlicing{}}).ok);
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Ground", "Ground", {}, "layer-1"}).ok);
        CHECK(c.execute(AddTilemapComponentCommand{"s", 1, tm}).ok);

        c.apply(SetActiveLayerIntent{"s", "fg"});   // active is "fg"; entity 1 is on "layer-1"
        const auto begin = c.apply(BeginTilePaintStrokeIntent{"s", 1, EditorTool::Eraser, {0, 0}});
        CHECK(!begin.ok);
        CHECK(begin.error.find("active layer") != std::string::npos);
        CHECK(!c.apply(BeginTileRectangleIntent{"s", 1, {0, 0}}).ok);
        CHECK(!c.apply(FillTilemapIntent{"s", 1, {0, 0}}).ok);

        // Switch back: the active-layer gate now passes, and everything else
        // about entity 1's tilemap is genuinely valid, so it fully succeeds.
        c.apply(SetActiveLayerIntent{"s", "layer-1"});
        CHECK(c.apply(BeginTilePaintStrokeIntent{"s", 1, EditorTool::Eraser, {0, 0}}).ok);
    }

    // -- Regression: the pre-existing lock check used inst->layerId directly,
    // which silently never matched a locked *default* layer for an instance
    // whose own layerId is "" (meaning "use the default"). isInstanceLayerLocked
    // resolves effectiveLayerId first, so this must now correctly reject. -----
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        c.apply(SelectSceneIntent{"s"});
        CHECK(c.execute(AddImageAssetCommand{"img-1", "some/path.png"}).ok);
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-1", "Tiles", "img-1", TilesetSlicing{}}).ok);
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Ground", "Ground", {}, ""}).ok);   // "" -> default layer
        CHECK(c.execute(AddTilemapComponentCommand{"s", 1, tm}).ok);
        CHECK(c.execute(SetLayerLockedCommand{"s", "layer-1", true}).ok);   // locks the default

        const auto begin = c.apply(BeginTilePaintStrokeIntent{"s", 1, EditorTool::Eraser, {0, 0}});
        CHECK(!begin.ok);
        CHECK(begin.error.find("locked") != std::string::npos);
    }

    // == Entity layer move reconciliation ========================================
    // reconcileWorkspace() keeps activeLayerId == effectiveLayerId(selection)
    // across a SetEntityLayerCommand's apply/undo/redo, not just at select time.

    // -- Move follows the selected entity: selection survives, activeLayerId
    // switches to the destination layer -----------------------------------------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        c.apply(SelectSceneIntent{"s"});
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Hero", "Hero", {}, "layer-1"}).ok);
        c.apply(SelectEntityIntent{1});
        CHECK(c.activeLayerId("s") == "layer-1");

        CHECK(c.execute(SetEntityLayerCommand{"s", 1, "fg"}).ok);
        CHECK(c.selection().primaryEntity == 1);   // still selected
        CHECK(c.activeLayerId("s") == "fg");       // active layer followed the move

        // -- Undo: both the entity and the active layer go back together -------
        CHECK(c.undo().ok);
        CHECK(c.document().findInstanceInScene("s", 1)->layerId == "layer-1");
        CHECK(c.selection().primaryEntity == 1);
        CHECK(c.activeLayerId("s") == "layer-1");

        // -- Redo: both move forward together again -----------------------------
        CHECK(c.redo().ok);
        CHECK(c.document().findInstanceInScene("s", 1)->layerId == "fg");
        CHECK(c.selection().primaryEntity == 1);
        CHECK(c.activeLayerId("s") == "fg");
    }

    // -- Same-layer no-op: no history entry, no console message, no workspace
    // change --------------------------------------------------------------------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        c.apply(SelectSceneIntent{"s"});
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Hero", "Hero", {}, "layer-1"}).ok);
        c.apply(SelectEntityIntent{1});
        const std::size_t undoBefore = c.undoSize();
        const std::size_t consoleBefore = c.consoleLog().size();
        CHECK(c.execute(SetEntityLayerCommand{"s", 1, "layer-1"}).ok);   // already there
        CHECK(c.undoSize() == undoBefore);         // no-op: not recorded
        CHECK(c.consoleLog().size() == consoleBefore);
        CHECK(c.activeLayerId("s") == "layer-1");
    }

    // -- Move to a hidden target layer: succeeds, activates the hidden layer,
    // keeps the selection, and posts exactly one Info message. A later unrelated
    // reconciliation (a second no-op-free command) does not repeat it ----------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        c.apply(SelectSceneIntent{"s"});
        CHECK(c.execute(AddSceneLayerCommand{"s", "bg", "Background", 1}).ok);
        c.apply(ToggleLayerEditorVisibilityIntent{"s", "bg"});   // hide it
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Hero", "Hero", {}, "layer-1"}).ok);
        c.apply(SelectEntityIntent{1});
        const std::size_t consoleBefore = c.consoleLog().size();

        CHECK(c.execute(SetEntityLayerCommand{"s", 1, "bg"}).ok);
        CHECK(c.selection().primaryEntity == 1);
        CHECK(c.activeLayerId("s") == "bg");
        CHECK(c.consoleLog().size() == consoleBefore + 1);
        CHECK(c.consoleLog().back().level == ConsoleMessage::Level::Info);
        CHECK(c.consoleLog().back().text.find("hidden layer") != std::string::npos);
        CHECK(c.consoleLog().back().text.find("Background") != std::string::npos);
        // The layer itself was not made visible as a side effect of the move.
        CHECK(c.sceneView("s").hiddenLayerIds.count("bg") == 1);

        const std::size_t consoleAfterMove = c.consoleLog().size();
        CHECK(c.execute(RenameEntityCommand{"s", 1, "Hero Renamed"}).ok);   // unrelated command
        CHECK(c.consoleLog().size() == consoleAfterMove);   // no repeated announcement
    }

    // -- Tilemap entity: moving layers preserves the component and its cells
    // exactly; only layerId changes ----------------------------------------------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        c.apply(SelectSceneIntent{"s"});
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        CHECK(c.execute(AddImageAssetCommand{"img-1", "some/path.png"}).ok);
        TilesetSlicing slicing{32, 32};
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-1", "Tiles", "img-1", slicing}).ok);
        CHECK(c.execute(ChangeTilesetSlicingCommand{
                  "tiles-1", slicing, tilesForSlicing(64, 64, slicing)}).ok);   // "tile-1".."tile-4"
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Ground", "Ground", {}, "layer-1"}).ok);
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        CHECK(c.execute(AddTilemapComponentCommand{"s", 1, tm}).ok);
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{0, 0}, std::nullopt,
                                            TilemapCellValue{"tile-1", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{"s", 1, changes}).ok);

        CHECK(c.execute(SetEntityLayerCommand{"s", 1, "fg"}).ok);
        const SceneInstanceDef* moved = c.document().findInstanceInScene("s", 1);
        CHECK(moved->layerId == "fg");
        CHECK(moved->tilemap.has_value());
        CHECK(moved->tilemap->tilesetAssetId == "tiles-1");
        CHECK(readTilemapCell(*moved->tilemap, {0, 0})->tileId == "tile-1");
    }

    // == Scene View Selection & Tool Context =====================================
    // reconcileTilemapEditingContext() keeps EditorState::activeTool /
    // pendingStroke|Rectangle / temporaryToolOverride / selectedTileId in line
    // with whether the current selection actually supports a tilemap tool -
    // called after every Command (via reconcileWorkspace) and directly from
    // the selection/tool-changing Intents.

    // -- End-to-end: Eraser stuck on a tilemap selection no longer blocks
    // dragging a different entity selected from the Hierarchy afterwards -----
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        c.apply(SelectSceneIntent{"s"});
        CHECK(c.execute(AddImageAssetCommand{"img-1", "some/path.png"}).ok);
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-1", "Tiles", "img-1", TilesetSlicing{32, 32}}).ok);
        CHECK(c.execute(ChangeTilesetSlicingCommand{
                  "tiles-1", TilesetSlicing{32, 32},
                  tilesForSlicing(64, 64, TilesetSlicing{32, 32})}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Ground", "Ground", {}, "layer-1"}).ok);
        TilemapComponent tm; tm.tilesetAssetId = "tiles-1";
        CHECK(c.execute(AddTilemapComponentCommand{"s", 1, tm}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 2, "obj-2", "Hero", "Hero", {5.f, 5.f}, "layer-1"}).ok);

        // 1. Eraser active on the tilemap entity.
        c.apply(SelectEntityIntent{1});
        c.apply(SetActiveToolIntent{EditorTool::Eraser});
        CHECK(c.state().activeTool == EditorTool::Eraser);
        const uint64_t revBefore = c.document().revision();
        const std::size_t undoBefore = c.undoSize();

        // 2. Select Entity 2 (no tilemap) from the Hierarchy.
        CHECK(c.apply(SelectEntityIntent{2}).ok);
        // 3. Tool falls back to Select.
        CHECK(c.state().activeTool == EditorTool::Select);
        CHECK(c.effectiveTilemapTool() == EditorTool::Select);
        // Pure workspace reconciliation: no authoring mutation happened.
        CHECK(c.document().revision() == revBefore);
        CHECK(c.undoSize() == undoBefore);

        // 4-6. Pointer down/drag/up equivalent - exactly what
        // routeViewportPickDrag issues on release, now reachable because its
        // own "activeTool == Select" guard passes again.
        CHECK(c.execute(SetEntityTransformCommand{"s", 2, {50.f, 50.f}}).ok);
        CHECK(c.document().findInstanceInScene("s", 2)->transform.position.x == 50.f);

        // 7. Undo restores the position.
        CHECK(c.undo().ok);
        CHECK(c.document().findInstanceInScene("s", 2)->transform.position.x == 5.f);
    }

    // -- Selecting the scene (Hierarchy "Scene 1" row) cancels any pending
    // gesture, falls the tool back to Select, and clears the selection -------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        c.apply(SelectSceneIntent{"s"});
        CHECK(c.execute(AddImageAssetCommand{"img-1", "some/path.png"}).ok);
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-1", "Tiles", "img-1", TilesetSlicing{32, 32}}).ok);
        CHECK(c.execute(ChangeTilesetSlicingCommand{
                  "tiles-1", TilesetSlicing{32, 32},
                  tilesForSlicing(64, 64, TilesetSlicing{32, 32})}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Ground", "Ground", {}, "layer-1"}).ok);
        TilemapComponent tm; tm.tilesetAssetId = "tiles-1";
        CHECK(c.execute(AddTilemapComponentCommand{"s", 1, tm}).ok);

        c.apply(SelectEntityIntent{1});
        c.apply(SetActiveToolIntent{EditorTool::Brush});
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.apply(BeginTilePaintStrokeIntent{"s", 1, EditorTool::Brush, {0, 0}}).ok);
        CHECK(c.state().tilemapEditor.pendingStroke.has_value());

        CHECK(c.apply(SelectSceneIntent{"s"}).ok);   // re-selecting the scene itself
        CHECK(!c.state().tilemapEditor.pendingStroke.has_value());
        CHECK(c.state().activeTool == EditorTool::Select);
        CHECK(!c.selection().hasEntity());
    }

    // -- Switching between two tilemaps keeps the tool, retargets, keeps a
    // still-valid tile pick but clears one that doesn't exist in the new
    // tileset (never substitutes one), and never touches the old tilemap -----
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        c.apply(SelectSceneIntent{"s"});
        CHECK(c.execute(AddImageAssetCommand{"img-1", "a.png"}).ok);
        CHECK(c.execute(AddImageAssetCommand{"img-2", "b.png"}).ok);
        // tiles-1: 64x64 sliced at 32x32 -> "tile-1".."tile-4".
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-1", "Tiles A", "img-1", TilesetSlicing{32, 32}}).ok);
        CHECK(c.execute(ChangeTilesetSlicingCommand{
                  "tiles-1", TilesetSlicing{32, 32},
                  tilesForSlicing(64, 64, TilesetSlicing{32, 32})}).ok);
        // tiles-2: 32x32 sliced at 32x32 -> "tile-1" only - deliberately does
        // not include "tile-4".
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-2", "Tiles B", "img-2", TilesetSlicing{32, 32}}).ok);
        CHECK(c.execute(ChangeTilesetSlicingCommand{
                  "tiles-2", TilesetSlicing{32, 32},
                  tilesForSlicing(32, 32, TilesetSlicing{32, 32})}).ok);

        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Ground", "Ground", {}, "layer-1"}).ok);
        TilemapComponent tmA; tmA.tilesetAssetId = "tiles-1";
        CHECK(c.execute(AddTilemapComponentCommand{"s", 1, tmA}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 2, "obj-2", "Decoration", "Decoration", {}, "layer-1"}).ok);
        TilemapComponent tmB; tmB.tilesetAssetId = "tiles-2";
        CHECK(c.execute(AddTilemapComponentCommand{"s", 2, tmB}).ok);

        c.apply(SelectEntityIntent{1});
        c.apply(SetActiveToolIntent{EditorTool::Brush});
        c.apply(SelectPaintTileIntent{"tile-4"});
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{0, 0}, std::nullopt,
                                            TilemapCellValue{"tile-4", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{"s", 1, changes}).ok);

        CHECK(c.apply(SelectEntityIntent{2}).ok);   // switch to the other tilemap
        CHECK(c.state().activeTool == EditorTool::Brush);         // tool stays
        CHECK(!c.state().tilemapEditor.stamp.has_value());   // stamp came from tiles-1, target is tiles-2

        const TilemapComponent& tmAfter = *c.document().findInstanceInScene("s", 1)->tilemap;
        CHECK(readTilemapCell(tmAfter, {0, 0})->tileId == "tile-4");   // old tilemap untouched

        // Provenance, not id matching: "tile-1" exists in BOTH tilesets, but a
        // stamp selected while tiles-2 was the target is reset on switching to
        // the tiles-1 tilemap - identically-named ids must never keep a stamp
        // alive across tilesets.
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.state().tilemapEditor.stamp->sourceTilesetAssetId == "tiles-2");
        CHECK(c.apply(SelectEntityIntent{1}).ok);   // back to tiles-1, which also has "tile-1"
        CHECK(!c.state().tilemapEditor.stamp.has_value());
    }

    // -- Locking the active layer while its tilemap is selected: selection
    // survives (Hierarchy/Inspector may still show it), but the tool falls
    // back to Select and painting is rejected with no pending residue --------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        c.apply(SelectSceneIntent{"s"});
        CHECK(c.execute(AddImageAssetCommand{"img-1", "some/path.png"}).ok);
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-1", "Tiles", "img-1", TilesetSlicing{32, 32}}).ok);
        CHECK(c.execute(ChangeTilesetSlicingCommand{
                  "tiles-1", TilesetSlicing{32, 32},
                  tilesForSlicing(64, 64, TilesetSlicing{32, 32})}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Ground", "Ground", {}, "layer-1"}).ok);
        TilemapComponent tm; tm.tilesetAssetId = "tiles-1";
        CHECK(c.execute(AddTilemapComponentCommand{"s", 1, tm}).ok);

        c.apply(SelectEntityIntent{1});
        c.apply(SetActiveToolIntent{EditorTool::Brush});
        CHECK(c.state().activeTool == EditorTool::Brush);

        CHECK(c.execute(SetLayerLockedCommand{"s", "layer-1", true}).ok);
        CHECK(c.selection().primaryEntity == 1);           // selection itself survives
        CHECK(c.state().activeTool == EditorTool::Select);  // but the tool falls back
        CHECK(!c.state().tilemapEditor.pendingStroke.has_value());

        const auto begin = c.apply(BeginTilePaintStrokeIntent{"s", 1, EditorTool::Eraser, {0, 0}});
        CHECK(!begin.ok);
        CHECK(!c.state().tilemapEditor.pendingStroke.has_value());
    }

    // -- Pan is not a tilemap tool: a selection change never resets it --------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        c.apply(SelectSceneIntent{"s"});
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Hero", "Hero", {}, "layer-1"}).ok);
        c.apply(SetActiveToolIntent{EditorTool::Pan});
        CHECK(c.apply(SelectEntityIntent{1}).ok);
        CHECK(c.state().activeTool == EditorTool::Pan);
        CHECK(c.apply(SelectSceneIntent{"s"}).ok);
        CHECK(c.state().activeTool == EditorTool::Pan);
    }

    // -- A momentary Eraser override is always cleared by a selection change,
    // even when the newly selected entity also supports tilemap tools --------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        c.apply(SelectSceneIntent{"s"});
        CHECK(c.execute(AddImageAssetCommand{"img-1", "some/path.png"}).ok);
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-1", "Tiles", "img-1", TilesetSlicing{32, 32}}).ok);
        CHECK(c.execute(ChangeTilesetSlicingCommand{
                  "tiles-1", TilesetSlicing{32, 32},
                  tilesForSlicing(64, 64, TilesetSlicing{32, 32})}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Ground", "Ground", {}, "layer-1"}).ok);
        TilemapComponent tm1; tm1.tilesetAssetId = "tiles-1";
        CHECK(c.execute(AddTilemapComponentCommand{"s", 1, tm1}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 2, "obj-2", "Decoration", "Decoration", {}, "layer-1"}).ok);
        TilemapComponent tm2; tm2.tilesetAssetId = "tiles-1";
        CHECK(c.execute(AddTilemapComponentCommand{"s", 2, tm2}).ok);

        c.apply(SelectEntityIntent{1});
        c.apply(SetActiveToolIntent{EditorTool::Brush});
        c.apply(BeginTemporaryToolOverrideIntent{EditorTool::Eraser});
        CHECK(c.state().tilemapEditor.temporaryToolOverride.has_value());

        CHECK(c.apply(SelectEntityIntent{2}).ok);   // entity 2 also has a tilemap
        CHECK(!c.state().tilemapEditor.temporaryToolOverride.has_value());
        CHECK(c.state().activeTool == EditorTool::Brush);   // persistent tool untouched
    }

    // -- Front-to-back picking respects real layer order: a sprite on the
    // foreground layer wins over an overlapping tilemap on the background
    // layer, and inverting which entity is on which layer flips the result --
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        c.apply(SelectSceneIntent{"s"});
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        CHECK(c.execute(AddImageAssetCommand{"img-1", "some/path.png"}).ok);
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-1", "Tiles", "img-1", TilesetSlicing{32, 32}}).ok);
        CHECK(c.execute(ChangeTilesetSlicingCommand{
                  "tiles-1", TilesetSlicing{32, 32},
                  tilesForSlicing(64, 64, TilesetSlicing{32, 32})}).ok);

        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Ground", "Ground", {16.f, 16.f}, "layer-1"}).ok);
        TilemapComponent tm; tm.tilesetAssetId = "tiles-1"; tm.cellSize = {32.f, 32.f};
        CHECK(c.execute(AddTilemapComponentCommand{"s", 1, tm}).ok);
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{0, 0}, std::nullopt,
                                            TilemapCellValue{"tile-1", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{"s", 1, changes}).ok);

        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 2, "obj-2", "Hero", "Hero", {16.f, 16.f}, "fg"}).ok);
        CHECK(c.execute(AddSpriteRendererToObjectTypeCommand{"obj-2"}).ok);
        CHECK(c.execute(SetObjectTypeSpriteSourceCommand{
            "obj-2", ObjectTypeSpriteSourceKind::Image, "img-1"}).ok);

        const SceneFrameSnapshot before =
            collectSceneFrameSnapshot(c.document(), "s", INVALID_ENTITY);
        CHECK(pickEntityAt(before, Vec2{16.f, 16.f}) == 2);   // sprite: foreground layer wins

        // Invert which entity is on which layer - the pick must flip too.
        CHECK(c.execute(SetEntityLayerCommand{"s", 2, "layer-1"}).ok);
        CHECK(c.execute(SetEntityLayerCommand{"s", 1, "fg"}).ok);
        const SceneFrameSnapshot after =
            collectSceneFrameSnapshot(c.document(), "s", INVALID_ENTITY);
        CHECK(pickEntityAt(after, Vec2{16.f, 16.f}) == 1);   // tilemap: now foreground
    }

    // == Start-scene invariant: scenes exist => startSceneId is valid ==========

    // -- (1)(2)(3) First scene becomes the start scene; workspace untouched ----
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.document().startSceneId().empty());
        const SceneId activeBefore = c.state().activeSceneId;
        const uint64_t revisionBefore = c.document().revision();
        CHECK(c.execute(CreateSceneCommand{"scene-1", "Scene 1"}).ok);
        CHECK(c.document().revision() == revisionBefore + 1);
        CHECK(c.document().startSceneId() == "scene-1");   // (1) invariant maintained
        CHECK(c.state().activeSceneId == activeBefore);    // (2) no auto-select
        CHECK(!c.state().selection.hasEntity());           // (3) selection untouched
    }

    // -- (4) Undo of the first scene restores an empty start scene -------------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"scene-1", "Scene 1"}).ok);
        c.undo();
        CHECK(c.document().data().scenes.empty());
        CHECK(c.document().startSceneId().empty());
    }

    // -- (5) Creating a second scene does not change the start scene -----------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"scene-1", "Scene 1"}).ok);
        CHECK(c.execute(CreateSceneCommand{"scene-2", "Scene 2"}).ok);
        CHECK(c.document().startSceneId() == "scene-1");   // unchanged
    }

    // -- (6)(7)(8)(9) After the first scene: valid, saveable, playable ---------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"scene-1", "Scene 1"}).ok);
        // (6) the document now satisfies the validator (start scene is valid).
        CHECK(ProjectValidator::validate(ProjectDocument{c.document().data()}).ok);
        // (7) and saves successfully.
        const std::filesystem::path dir = testTempDir();
        const auto saved = saveProjectToFile(c, dir / "first.artcade-project");
        CHECK(saved.ok);
        // (8) Play Project is available, (9) Play Current Scene is not (no active scene).
        CHECK(c.canPlayProject());
        CHECK(!c.canPlayCurrentScene());
    }

    // -- (10) SetStartScene (valid) changes only the document ------------------
    {
        EditorCoordinator c{makeDoc()};                    // start scene = kSceneA
        const SceneId activeBefore = c.state().activeSceneId;
        c.apply(SelectEntityIntent{kHero});
        const auto r = c.execute(SetStartSceneCommand{kSceneB});
        CHECK(r.ok);
        CHECK(c.document().startSceneId() == kSceneB);
        CHECK(c.state().activeSceneId == activeBefore);    // workspace unchanged
        CHECK(c.state().selection.primaryEntity == kHero); // selection unchanged
    }

    // -- (11) SetStartScene to the current start is a no-op (no undo, no rev) --
    {
        EditorCoordinator c{makeDoc()};
        const uint64_t revBefore = c.document().revision();
        c.consumeInvalidations();
        const auto r = c.execute(SetStartSceneCommand{kSceneA}); // already the start
        CHECK(r.ok);
        CHECK(c.document().revision() == revBefore);       // no mutation
        CHECK(!c.canUndo());                               // not recorded
        CHECK(c.consumeInvalidations() == EditorInvalidation::None);
    }

    // -- (12) SetStartScene to a missing scene fails without structural change -
    {
        EditorCoordinator c{makeDoc()};
        const uint64_t revBefore = c.document().revision();
        c.consumeInvalidations();
        const auto r = c.execute(SetStartSceneCommand{"missing"});
        CHECK(!r.ok);
        CHECK(c.document().startSceneId() == kSceneA);     // unchanged
        CHECK(c.document().revision() == revBefore);
        CHECK(!c.canUndo());
        const EditorInvalidation inv = c.consumeInvalidations();
        CHECK(!has(inv, EditorInvalidation::Hierarchy));
        CHECK(!has(inv, EditorInvalidation::Toolbar));
        CHECK(!has(inv, EditorInvalidation::Project));
    }

    // -- (13)(14) Undo restores the previous start; Hierarchy+Toolbar refresh --
    {
        EditorCoordinator c{makeDoc()};
        c.consumeInvalidations();
        CHECK(c.execute(SetStartSceneCommand{kSceneB}).ok);
        const EditorInvalidation inv = c.consumeInvalidations();
        CHECK(has(inv, EditorInvalidation::Hierarchy));    // (14)
        CHECK(has(inv, EditorInvalidation::Toolbar));      // (14)
        c.undo();                                          // (13)
        CHECK(c.document().startSceneId() == kSceneA);
    }

    // -- (15) SetStartScene is a Patch, never a Replace ------------------------
    {
        EditorCoordinator c{makeDoc()};
        const uint32_t replacesBefore = c.document().replaceCount();
        CHECK(c.execute(SetStartSceneCommand{kSceneB}).ok);
        CHECK(c.document().replaceCount() == replacesBefore);
    }

    // -- (16) Save/reload preserves the chosen start scene --------------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(SetStartSceneCommand{kSceneB}).ok);
        const std::filesystem::path dir = testTempDir();
        const std::filesystem::path path = dir / "start.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);

        EditorCoordinator reloaded{makeReplacementDoc()};
        CHECK(loadProjectFromFile(reloaded, path).ok);
        CHECK(reloaded.document().startSceneId() == kSceneB);
    }

    // == BoxCollider2D component: object type only ============================

    // -- (1) Add targets objectTypeId, not scene/entity -----------------------
    {
        EditorCoordinator c{makeInheritedDoc()};
        const uint32_t replacesBefore = c.document().replaceCount();
        const auto r = c.execute(AddBoxColliderCommand{"Hero"});
        CHECK(r.ok);
        CHECK(r.change.kind == DomainChangeKind::ComponentAdded);
        CHECK(r.change.componentKind == ComponentKind::BoxCollider2D);
        CHECK(r.change.objectTypeId == "Hero");
        CHECK(r.change.entityId == INVALID_ENTITY);
        CHECK(c.document().data().objectTypes.at("Hero").boxCollider2D.has_value());
        CHECK(!c.document().findInstanceInScene(kSceneA, kHero)->spriteRendererOverride);
        CHECK(c.document().replaceCount() == replacesBefore);
        CHECK(c.undoSize() == 1);

        const uint64_t revBeforeDuplicate = c.document().revision();
        const std::size_t undoBeforeDuplicate = c.undoSize();
        c.consumeInvalidations();
        CHECK(c.execute(AddBoxColliderCommand{"Hero"}).ok);
        CHECK(c.document().revision() == revBeforeDuplicate);
        CHECK(c.undoSize() == undoBeforeDuplicate);
        CHECK(c.consumeInvalidations() == EditorInvalidation::None);
    }

    // -- (2) Setters validate finite/positive values and invalidate narrowly --
    {
        EditorCoordinator c{makeInheritedDoc()};
        CHECK(c.execute(AddBoxColliderCommand{"Hero"}).ok);
        c.consumeInvalidations();

        CHECK(c.execute(SetBoxColliderOffsetCommand{"Hero", Vec2{4.f, -6.f}}).ok);
        CHECK(c.consumeInvalidations()
              == (EditorInvalidation::Inspector | EditorInvalidation::Viewport
                  | EditorInvalidation::Toolbar));
        CHECK(c.document().data().objectTypes.at("Hero").boxCollider2D->offset.x == 4.f);
        CHECK(c.document().data().objectTypes.at("Hero").boxCollider2D->offset.y == -6.f);

        const uint64_t revBeforeBadSize = c.document().revision();
        CHECK(!c.execute(SetBoxColliderSizeCommand{"Hero", Vec2{0.f, 10.f}}).ok);
        CHECK(c.document().revision() == revBeforeBadSize);
        CHECK(!c.execute(SetBoxColliderSizeCommand{"Hero", Vec2{-1.f, 10.f}}).ok);
        CHECK(!c.execute(SetBoxColliderSizeCommand{
            "Hero", Vec2{std::numeric_limits<float>::quiet_NaN(), 10.f}}).ok);
        CHECK(!c.execute(SetBoxColliderSizeCommand{
            "Hero", Vec2{std::numeric_limits<float>::infinity(), 10.f}}).ok);
        CHECK(!c.execute(SetBoxColliderOffsetCommand{
            "Hero", Vec2{std::numeric_limits<float>::quiet_NaN(), 0.f}}).ok);
        CHECK(c.document().revision() == revBeforeBadSize);
        CHECK(c.execute(SetBoxColliderSizeCommand{"Hero", Vec2{64.f, 24.f}}).ok);
        CHECK(c.document().data().objectTypes.at("Hero").boxCollider2D->size.x == 64.f);
        CHECK(c.document().data().objectTypes.at("Hero").boxCollider2D->size.y == 24.f);
    }

    // -- (3) No-op setters do not mutate or enter undo ------------------------
    {
        EditorCoordinator c{makeInheritedDoc()};
        CHECK(c.execute(AddBoxColliderCommand{"Hero"}).ok);
        const uint64_t rev = c.document().revision();
        const std::size_t undo = c.undoSize();
        CHECK(c.execute(SetBoxColliderEnabledCommand{"Hero", true}).ok);
        CHECK(c.document().revision() == rev);
        CHECK(c.undoSize() == undo);
        CHECK(c.execute(SetBoxColliderOffsetCommand{"Hero", Vec2{0.f, 0.f}}).ok);
        CHECK(c.document().revision() == rev);
        CHECK(c.undoSize() == undo);
        CHECK(c.execute(SetBoxColliderSizeCommand{"Hero", Vec2{32.f, 32.f}}).ok);
        CHECK(c.document().revision() == rev);
        CHECK(c.undoSize() == undo);
        CHECK(c.execute(SetBoxColliderModeCommand{"Hero", BoxColliderMode::Solid}).ok);
        CHECK(c.document().revision() == rev);
        CHECK(c.undoSize() == undo);
    }

    // -- (4) Undo restores exact object-type component state ------------------
    {
        EditorCoordinator c{makeInheritedDoc()};
        CHECK(c.execute(AddBoxColliderCommand{"Hero"}).ok);
        CHECK(c.execute(SetBoxColliderSizeCommand{"Hero", Vec2{80.f, 30.f}}).ok);
        CHECK(c.execute(SetBoxColliderModeCommand{"Hero", BoxColliderMode::Trigger}).ok);
        CHECK(c.execute(RemoveBoxColliderCommand{"Hero"}).ok);
        CHECK(!c.document().data().objectTypes.at("Hero").boxCollider2D.has_value());
        c.undo();
        const BoxCollider2DComponent& collider =
            *c.document().data().objectTypes.at("Hero").boxCollider2D;
        CHECK(collider.size.x == 80.f);
        CHECK(collider.size.y == 30.f);
        CHECK(collider.mode == BoxColliderMode::Trigger);
    }

    // -- (5) Inspector action resolves selection -> objectTypeId --------------
    {
        EditorCoordinator c{makeInheritedDoc()};
        c.apply(SelectEntityIntent{kHero});
        const std::size_t undoBefore = c.undoSize();
        CHECK(addBoxCollider(c).ok);
        CHECK(c.undoSize() == undoBefore + 1);
        CHECK(c.document().data().objectTypes.at("Hero").boxCollider2D.has_value());

        EditorCoordinator empty{makeInheritedDoc()};
        // A rejected inspector_actions helper is not silent either: every one
        // of these select-target-then-execute wrappers logs through the same
        // fail() helper before returning, whether or not the caller (a UI
        // action handler) happens to check the result.
        const std::size_t logBefore = empty.consoleLog().size();
        CHECK(!addBoxCollider(empty).ok);
        CHECK(empty.undoSize() == 0);
        CHECK(empty.consoleLog().size() == logBefore + 1);
        CHECK(empty.consoleLog().back().level == ConsoleMessage::Level::Error);
    }

    // -- (6) Bounds projection updates every instance of the same object type -
    {
        ProjectDoc doc = makeInheritedDoc();
        SceneInstanceDef second;
        second.id = 314;
        second.objectTypeId = "Hero";
        second.instanceName = "Hero 2";
        second.transform.position = {100.f, 200.f};
        doc.scenes.at(kSceneA).instances.push_back(second);

        EditorCoordinator c{doc};
        CHECK(c.execute(AddBoxColliderCommand{"Hero"}).ok);
        CHECK(c.execute(SetBoxColliderOffsetCommand{"Hero", Vec2{2.f, 3.f}}).ok);
        CHECK(c.execute(SetBoxColliderSizeCommand{"Hero", Vec2{10.f, 20.f}}).ok);

        c.execute(CreateEntityCommand{kSceneA, 777, "Enemy", "Enemy", {300.f, 400.f}});

        const std::vector<SceneFrameCollider> bounds =
            collectBoxColliderBounds(c.document(), kSceneA, kHero);
        CHECK(bounds.size() == 2);
        CHECK(bounds[0].worldBounds.width == 10.f);
        CHECK(bounds[0].worldBounds.height == 20.f);
        CHECK(bounds[0].worldBounds.x == 7.f);
        CHECK(bounds[0].worldBounds.y == 13.f);
        CHECK(bounds[0].selected);
        CHECK(bounds[0].enabled);
        CHECK(bounds[1].worldBounds.x == 97.f);
        CHECK(bounds[1].worldBounds.y == 193.f);
        CHECK(!bounds[1].selected);

        // Instance scale must enlarge/shrink the overlay the same way Play
        // scales CollisionWorld::shapeInstance (ADR-0014 local size × |scale|).
        {
            CHECK(c.execute(SetEntityTransformCommand{
                kSceneA, kHero,
                AuthoredTransformPatch{std::nullopt, std::nullopt, Vec2{2.f, 0.5f}}}).ok);
            const std::vector<SceneFrameCollider> scaled =
                collectBoxColliderBounds(c.document(), kSceneA, kHero);
            CHECK(scaled.size() == 2);
            const SceneFrameCollider* heroBounds = nullptr;
            for (const SceneFrameCollider& b : scaled) {
                if (b.selected) { heroBounds = &b; break; }
            }
            CHECK(heroBounds != nullptr);
            if (heroBounds) {
                CHECK(heroBounds->worldBounds.width == 20.f);
                CHECK(heroBounds->worldBounds.height == 10.f);
            }
        }

        CHECK(c.execute(SetBoxColliderModeCommand{"Hero", BoxColliderMode::Trigger}).ok);
        const std::vector<SceneFrameCollider> triggerBounds =
            collectBoxColliderBounds(c.document(), kSceneA, INVALID_ENTITY);
        CHECK(triggerBounds.size() == 2);
        CHECK(triggerBounds[0].mode == BoxColliderMode::Trigger);

        CHECK(c.execute(SetBoxColliderEnabledCommand{"Hero", false}).ok);
        CHECK(collectBoxColliderBounds(c.document(), kSceneA, INVALID_ENTITY).empty());
        CHECK(c.document().data().objectTypes.at("Hero").boxCollider2D.has_value());
        c.undo();
        CHECK(collectBoxColliderBounds(c.document(), kSceneA, INVALID_ENTITY).size() == 2);
    }

    // RU-03 (D-01): (6b) "Editor overlay and runtime share the same world-bounds
    // formula" removed - it parity-checked the editor's boxColliderWorldBounds()
    // against PlaySession's own hand-written runtimeColliderBounds(), which no
    // longer exists (GameplaySession's real Physics owns collider bounds now,
    // not exposed through PlaySession's facade).

    // -- (7) Save/reload persists object-type collider, never per instance ----
    {
        EditorCoordinator c{makeInheritedDoc()};
        CHECK(c.execute(AddBoxColliderCommand{"Hero"}).ok);
        CHECK(c.execute(SetBoxColliderOffsetCommand{"Hero", Vec2{5.f, 6.f}}).ok);
        CHECK(c.execute(SetBoxColliderSizeCommand{"Hero", Vec2{70.f, 32.f}}).ok);
        CHECK(c.execute(SetBoxColliderEnabledCommand{"Hero", false}).ok);
        CHECK(c.execute(SetBoxColliderModeCommand{"Hero", BoxColliderMode::OneWayPlatform}).ok);

        const auto ser = ProjectSerializer::serialize(c.document());
        CHECK(ser.ok);
        CHECK(ser.value.find("boxCollider2D") != std::string::npos);
        CHECK(ser.value.find("\"mode\"") != std::string::npos);
        CHECK(ser.value.find("oneWayPlatform") != std::string::npos);
        CHECK(ser.value.find("isTrigger") == std::string::npos);
        CHECK(ser.value.find("spriteRendererOverride") == std::string::npos);
        const std::size_t instancePos = ser.value.find("\"instances\"");
        const std::size_t colliderPos = ser.value.find("boxCollider2D");
        CHECK(colliderPos < instancePos);

        const std::filesystem::path dir = testTempDir();
        const std::filesystem::path path = dir / "box-collider.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);

        EditorCoordinator reloaded{makeReplacementDoc()};
        CHECK(loadProjectFromFile(reloaded, path).ok);
        const auto& type = reloaded.document().data().objectTypes.at("Hero");
        CHECK(type.boxCollider2D.has_value());
        CHECK(type.boxCollider2D->offset.x == 5.f);
        CHECK(type.boxCollider2D->offset.y == 6.f);
        CHECK(type.boxCollider2D->size.x == 70.f);
        CHECK(type.boxCollider2D->size.y == 32.f);
        CHECK(!type.boxCollider2D->enabled);
        CHECK(type.boxCollider2D->mode == BoxColliderMode::OneWayPlatform);
    }

    // -- (8) Legacy isTrigger migrates to mode; unknown mode is rejected ------
    {
        const std::string legacySolid =
            R"({"activeSceneId":"s","scenes":[{"id":"s","instances":[{"id":1,"objectTypeId":"Hero","instanceName":"Hero"}]}],"objectTypes":[{"id":"Hero","boxCollider2D":{"enabled":true,"isTrigger":false}}]})";
        const auto loadedSolid = ProjectSerializer::deserialize(legacySolid);
        CHECK(loadedSolid.ok);
        CHECK(loadedSolid.value.data().objectTypes.at("Hero").boxCollider2D->mode
              == BoxColliderMode::Solid);

        const std::string legacyTrigger =
            R"({"activeSceneId":"s","scenes":[{"id":"s","instances":[{"id":1,"objectTypeId":"Hero","instanceName":"Hero"}]}],"objectTypes":[{"id":"Hero","boxCollider2D":{"enabled":true,"isTrigger":true}}]})";
        const auto loaded = ProjectSerializer::deserialize(legacyTrigger);
        CHECK(loaded.ok);
        const auto& type = loaded.value.data().objectTypes.at("Hero");
        CHECK(type.boxCollider2D.has_value());
        CHECK(type.boxCollider2D->mode == BoxColliderMode::Trigger);

        const std::string badMode =
            R"({"activeSceneId":"s","scenes":[{"id":"s","instances":[{"id":1,"objectTypeId":"Hero","instanceName":"Hero"}]}],"objectTypes":[{"id":"Hero","boxCollider2D":{"mode":"ghost"}}]})";
        CHECK(!ProjectSerializer::deserialize(badMode).ok);

        const std::string conflictingLegacy =
            R"({"activeSceneId":"s","scenes":[{"id":"s","instances":[{"id":1,"objectTypeId":"Hero","instanceName":"Hero"}]}],"objectTypes":[{"id":"Hero","boxCollider2D":{"mode":"oneWayPlatform","isTrigger":true}}]})";
        CHECK(!ProjectSerializer::deserialize(conflictingLegacy).ok);
    }

    // -- (9) Invalid persisted collider is rejected during validation ---------
    {
        const std::string bad =
            R"({"activeSceneId":"s","scenes":[{"id":"s","instances":[{"id":1,"objectTypeId":"Hero","instanceName":"Hero"}]}],"objectTypes":[{"id":"Hero","boxCollider2D":{"size":{"x":-1,"y":10}}}]})";
        const auto loaded = ProjectSerializer::deserialize(bad);
        CHECK(loaded.ok);
        CHECK(!ProjectValidator::validate(std::move(loaded.value)).ok);
    }

    // -- (10) OneWayPlatform + movement driver is explicitly unsupported ------
    {
        ProjectDoc doc = makeInheritedDoc();
        EntityDef& hero = doc.objectTypes.at("Hero");
        hero.boxCollider2D = BoxCollider2DComponent{
            {0.f, 0.f}, {32.f, 32.f}, true, BoxColliderMode::OneWayPlatform};
        LinearMoverComponent mover;
        mover.speed = 10.f;
        hero.linearMover = mover;
        CHECK(!ProjectValidator::validate(ProjectDocument{std::move(doc)}).ok);
    }

    // -- (11) Authoring commands prevent OneWayPlatform + movement driver -----
    {
        EditorCoordinator withMover{makeInheritedDoc()};
        CHECK(withMover.execute(AddBoxColliderCommand{"Hero"}).ok);
        CHECK(withMover.execute(AddLinearMoverCommand{"Hero"}).ok);
        CHECK(!withMover.execute(
            SetBoxColliderModeCommand{"Hero", BoxColliderMode::OneWayPlatform}).ok);

        EditorCoordinator oneWay{makeInheritedDoc()};
        CHECK(oneWay.execute(AddBoxColliderCommand{"Hero"}).ok);
        CHECK(oneWay.execute(
            SetBoxColliderModeCommand{"Hero", BoxColliderMode::OneWayPlatform}).ok);
        CHECK(!oneWay.execute(AddLinearMoverCommand{"Hero"}).ok);
        CHECK(!oneWay.execute(AddTopDownControllerCommand{"Hero"}).ok);
        CHECK(!oneWay.execute(AddPlatformerControllerCommand{"Hero"}).ok);
    }

    return reportAndExit("editor-core-test");
}
