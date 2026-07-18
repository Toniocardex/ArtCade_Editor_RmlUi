// script-text-ops-test.cpp — pure IDE-1 Lua text operations.

#include "editor_core_test_harness.h"
#include "editor-native/model/script_text_ops.h"

#include <iostream>
#include <string>
#include <utility>

using namespace ArtCade::EditorNative;
using namespace ArtCade::EditorNative::CoreTest;

int main() {
    {
        const std::string src = "local x = 1\nreturn x\n";
        const auto commented = toggleLineComment(src, 0, src.size());
        CHECK(commented.text.find("-- local x = 1") != std::string::npos);
        CHECK(commented.text.find("-- return x") != std::string::npos);
        const auto uncommented = toggleLineComment(commented.text, 0, commented.text.size());
        CHECK(uncommented.text.find("-- local") == std::string::npos);

        const auto indented = indentOrInsertTab("a\nb\n", 0, 4);
        CHECK(indented.text == "    a\n    b\n");
        const auto outdented = outdentSelection(indented.text, 0, indented.text.size());
        CHECK(outdented.text == "a\nb\n");

        const auto nl = insertNewlineWithAutoIndent("if true then", 12);
        CHECK(nl.text == "if true then\n");
        const auto nlBlock = insertNewlineWithAutoIndent("foo(", 4);
        CHECK(nlBlock.text == "foo(\n    ");

        const auto match = findMatchingBracket("a(b(c))", 6);
        CHECK(match.has_value());
        CHECK(match && match->first == 1 && match->second == 6);

        const auto tokens = tokenizeLua("local n = 2 -- hi\n");
        bool sawKeyword = false, sawComment = false, sawNumber = false;
        for (const LuaTokenSpan& span : tokens) {
            if (span.kind == LuaTokenKind::Keyword) sawKeyword = true;
            if (span.kind == LuaTokenKind::Comment) sawComment = true;
            if (span.kind == LuaTokenKind::Number) sawNumber = true;
        }
        CHECK(sawKeyword);
        CHECK(sawComment);
        CHECK(sawNumber);
        const std::string highlighted = highlightLuaToRml("return 1");
        CHECK(highlighted.find("lua-kw") != std::string::npos);
        CHECK(highlighted.find("lua-num") != std::string::npos);

        ScriptHighlightDecorations decorations;
        decorations.errorLines = {1};
        decorations.bracketMatch = std::make_pair(std::size_t{0}, std::size_t{0});
        const std::string decorated = highlightLuaToRml("(x)", decorations);
        CHECK(decorated.find("lua-line-error") != std::string::npos);
        CHECK(decorated.find("lua-bracket") != std::string::npos);

        const auto dup = duplicateSelectionLines("a\nb\n", 0, 1);
        CHECK(dup.text == "a\na\nb\n");
        const auto movedDown = moveSelectionLines("a\nb\n", 0, 1, 1);
        CHECK(movedDown.text == "b\na\n");
        const auto movedUp = moveSelectionLines("a\nb\n", 2, 3, -1);
        CHECK(movedUp.text == "b\na\n");
    }
    return reportAndExit("script-text-ops-test");
}
