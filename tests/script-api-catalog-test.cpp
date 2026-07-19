// script-api-catalog-test.cpp — IDE-2 catalog + language service + binding parity.

#include "editor_core_test_harness.h"
#include "editor-native/model/script_editor_state.h"
#include "editor-native/model/script_language_service.h"

#include "script-api-catalog.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <unordered_set>

using namespace ArtCade::Scripts;
using namespace ArtCade::EditorNative;
using namespace ArtCade::EditorNative::CoreTest;

int main() {
    {
        const auto& catalog = scriptApiCatalogV1();
        CHECK(!catalog.empty());
        std::unordered_set<std::string> names;
        bool sawVersion = false;
        bool sawOnUpdate = false;
        bool sawSetRotation = false;
        for (const ScriptApiEntry& entry : catalog) {
            CHECK(entry.apiVersion == kScriptApiVersion);
            CHECK(entry.qualifiedName && entry.qualifiedName[0] != '\0');
            CHECK(names.insert(entry.qualifiedName).second);
            if (std::string(entry.qualifiedName) == "artcade.require_api_version") {
                sawVersion = true;
                CHECK(entry.insertKind == ScriptApiInsertKind::ApiDeclaration);
            }
            if (std::string(entry.qualifiedName) == "on_update") {
                sawOnUpdate = true;
                CHECK(entry.insertKind == ScriptApiInsertKind::LifecycleCallback);
            }
            if (std::string(entry.qualifiedName) == "ctx.self.set_rotation") {
                sawSetRotation = true;
                CHECK(entry.paramCount >= 1);
                CHECK(entry.params && entry.params[0].unit == ScriptApiUnit::Radians);
                CHECK(entry.insertKind == ScriptApiInsertKind::FunctionCall);
            }
            if (std::string(entry.qualifiedName) == "ctx") {
                CHECK(entry.insertKind == ScriptApiInsertKind::None);
            }
            if (entry.kind == ScriptApiKind::Method || entry.kind == ScriptApiKind::Callback
                || entry.kind == ScriptApiKind::GlobalFn) {
                CHECK(entry.insertText && entry.insertText[0] != '\0');
            }
        }
        CHECK(sawVersion);
        CHECK(sawOnUpdate);
        CHECK(sawSetRotation);

        auto catalogPaths = scriptApiCatalogBindingPaths();
        auto runtimePaths = manualScriptRuntimeBindingInventory();
        CHECK(catalogPaths == runtimePaths);

        const std::string sample =
            "artcade.require_api_version(1)\n"
            "return {\n"
            "  on_update = function(ctx, dt)\n"
            "    ctx.self.set_rotation(1.5)\n"
            "  end\n"
            "}\n";
        const std::size_t rot = sample.find("set_rotation");
        CHECK(rot != std::string::npos);
        const ScriptLanguageHint hover = scriptHoverAt(sample, rot + 3);
        CHECK(hover.qualifiedName.find("set_rotation") != std::string::npos);
        CHECK(hover.title.find("radians") != std::string::npos
              || hover.detail.find("radians") != std::string::npos);

        const std::size_t call = sample.find("set_rotation(") + std::string("set_rotation").size();
        const ScriptLanguageHint signature = scriptSignatureAt(sample, call + 1);
        CHECK(!signature.title.empty());

        const auto completions = scriptCompletionsAt("ctx.self.set_", 13);
        bool foundRotation = false;
        for (const ScriptLanguageHint& hint : completions) {
            if (hint.qualifiedName == "ctx.self.set_rotation") {
                foundRotation = true;
                CHECK(hint.insertText == "set_rotation(");
            }
        }
        CHECK(foundRotation);

        // P1: keep dotted receiver; never eat "end" with a multi-line snippet.
        const std::string afterPartial = "ctx.self.set_";
        const auto keepReceiver = applyScriptCompletionInsert(
            afterPartial, afterPartial.size(), "set_rotation(");
        CHECK(keepReceiver.text == "ctx.self.set_rotation(");

        const std::string afterEnd =
            "return {\n  on_start = function(ctx)\n  end\n}";
        const std::size_t endCursor = afterEnd.find("end") + 3;
        const auto noEatEnd = applyScriptCompletionInsert(
            afterEnd, endCursor,
            "on_update = function(ctx, dt)\n    \nend");
        CHECK(noEatEnd.text.find("endon_update") == std::string::npos);
        CHECK(noEatEnd.text.find("end\non_update") != std::string::npos);
        CHECK(noEatEnd.text.find("on_start = function(ctx)\n  end\n") != std::string::npos);
        CHECK(noEatEnd.text.find("on_update = function") != std::string::npos);

        // Multi-line insert in front of existing text must also add a trailing newline.
        const std::string gluedBase = "on_start = function(ctx)\nendnext = 1";
        const std::size_t afterFirstEnd = gluedBase.find("end") + 3;
        const auto suffixNl = applyScriptCompletionInsert(
            gluedBase, afterFirstEnd,
            "on_update = function(ctx, dt)\n    \nend");
        CHECK(suffixNl.text.find("endon_update") == std::string::npos);
        CHECK(suffixNl.text.find("end\non_update") != std::string::npos);
        CHECK(suffixNl.text.find("end\nnext") != std::string::npos
              || suffixNl.text.find("endnext") == std::string::npos);

        // P2: ambiguous bare name "play" must not resolve.
        CHECK(resolveScriptApiAt("play", 4) == nullptr);

        // P2: rank-then-limit still surfaces prefix matches.
        const auto limited = scriptCompletionsAt("ctx.self.set_", 13, 2);
        CHECK(limited.size() <= 2);
        bool stillHasSet = false;
        for (const ScriptLanguageHint& hint : limited) {
            if (hint.qualifiedName.find("ctx.self.set_") == 0) stillHasSet = true;
        }
        CHECK(stillHasSet);

        CHECK(resolveScriptApiAt("unknown_token", 5) == nullptr);
        CHECK(!scriptApiReferenceEntries().empty());

        // Catalog insert kinds: lifecycle into return { }, no duplicate, API once.
        const ScriptApiEntry* onCollision =
            findScriptApiByQualifiedName("on_collision_enter");
        CHECK(onCollision != nullptr);
        const std::string baseScript =
            "artcade.require_api_version(1)\n"
            "\n"
            "return {\n"
            "    on_start = function(ctx)\n"
            "    end,\n"
            "}\n";
        const auto firstInsert =
            applyScriptApiCatalogInsert(*onCollision, baseScript, baseScript.size());
        CHECK(firstInsert.applied);
        CHECK(firstInsert.edit.text.find("on_collision_enter = function(ctx, other)")
              != std::string::npos);
        CHECK(firstInsert.edit.text.find("return {") != std::string::npos);
        CHECK(firstInsert.edit.text.find("endon_collision") == std::string::npos);

        const auto secondInsert =
            applyScriptApiCatalogInsert(*onCollision, firstInsert.edit.text,
                                        firstInsert.edit.selectionBegin);
        CHECK(!secondInsert.applied);
        CHECK(secondInsert.message.find("already exists") != std::string::npos);
        CHECK(secondInsert.edit.text.empty() || secondInsert.edit.text == firstInsert.edit.text
              || !secondInsert.applied);

        const ScriptApiEntry* requireApi =
            findScriptApiByQualifiedName("artcade.require_api_version");
        CHECK(requireApi != nullptr);
        const auto dupRequire =
            applyScriptApiCatalogInsert(*requireApi, firstInsert.edit.text, 0);
        CHECK(!dupRequire.applied);
        CHECK(dupRequire.message.find("already present") != std::string::npos);

        const ScriptApiEntry* ctxOnly = findScriptApiByQualifiedName("ctx");
        CHECK(ctxOnly != nullptr);
        const auto docsOnly = applyScriptApiCatalogInsert(*ctxOnly, baseScript, 0);
        CHECK(!docsOnly.applied);
        CHECK(docsOnly.message.find("Documentation only") != std::string::npos);

        // Open / activate must not mutate buffer text, dirty, revision, or undo.
        ScriptEditorState editor;
        const std::string scriptA =
            "artcade.require_api_version(1)\nreturn {\n    on_start = function(ctx)\n    end,\n}\n";
        const std::string scriptB =
            "artcade.require_api_version(1)\nreturn {\n}\n";
        CHECK(editor.open("NewScript_4", scriptA));
        CHECK(editor.open("Other", scriptB));
        const ScriptEditorBuffer* a = editor.find("NewScript_4");
        const ScriptEditorBuffer* b = editor.find("Other");
        CHECK(a && b);
        const std::string aText = a->text;
        const std::string bText = b->text;
        const std::uint64_t aRev = a->revision;
        const std::uint64_t bRev = b->revision;
        CHECK(!a->dirty());
        CHECK(!b->dirty());
        CHECK(a->undoHistory.empty());
        CHECK(b->undoHistory.empty());

        CHECK(!editor.open("NewScript_4", "MUST_NOT_REPLACE"));
        CHECK(editor.activeAssetId.has_value());
        CHECK(*editor.activeAssetId == "NewScript_4");
        CHECK(editor.find("NewScript_4")->text == aText);
        CHECK(editor.find("Other")->text == bText);
        CHECK(editor.find("NewScript_4")->revision == aRev);
        CHECK(editor.find("Other")->revision == bRev);
        CHECK(!editor.find("NewScript_4")->dirty());
        CHECK(!editor.find("Other")->dirty());
        CHECK(editor.find("NewScript_4")->undoHistory.empty());

        editor.activeAssetId = "Other";
        CHECK(editor.find("NewScript_4")->text == aText);
        CHECK(editor.find("Other")->text == bText);
        CHECK(!editor.anyDirty());
    }
    return reportAndExit("script-api-catalog-test");
}
