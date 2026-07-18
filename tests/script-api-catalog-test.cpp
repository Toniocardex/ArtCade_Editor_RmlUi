// script-api-catalog-test.cpp — IDE-2 catalog + language service + binding parity.

#include "editor_core_test_harness.h"
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
            if (std::string(entry.qualifiedName) == "artcade.require_api_version")
                sawVersion = true;
            if (std::string(entry.qualifiedName) == "on_update") sawOnUpdate = true;
            if (std::string(entry.qualifiedName) == "ctx.self.set_rotation") {
                sawSetRotation = true;
                CHECK(entry.paramCount >= 1);
                CHECK(entry.params && entry.params[0].unit == ScriptApiUnit::Radians);
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
        CHECK(noEatEnd.text.find("on_start = function(ctx)\n  end\n") != std::string::npos);
        CHECK(noEatEnd.text.find("on_update = function") != std::string::npos);

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
    }
    return reportAndExit("script-api-catalog-test");
}
