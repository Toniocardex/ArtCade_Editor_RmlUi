#pragma once

#include "editor-native/commands/logic_expression_commands.h"
#include "logic-core.h"
#include "logic-number-expression-format.h"
#include "logic-number-expression-validation.h"

#include <optional>
#include <string>
#include <vector>

namespace ArtCade::EditorNative {

struct NumberExpressionEditorDraft {
    LogicNumberExpressionAddress address;
    NumberExpression original = NumberExpression::literal(0.0);
    NumberExpression edited = NumberExpression::literal(0.0);
    std::string title;
    /** ADR-0028 Context — Number-typed catalogs owned by the draft. */
    std::vector<GameVariableDefinition> localNumberVariables;
    std::vector<GameVariableDefinition> globalNumberVariables;
    bool selfAvailable = true;
    bool sceneAvailable = true;
    bool deltaSecondsAvailable = false;
};

class NumberExpressionEditorController {
public:
    bool isOpen() const { return draft_.has_value(); }
    const NumberExpressionEditorDraft* draft() const {
        return draft_ ? &*draft_ : nullptr;
    }

    void open(NumberExpressionEditorDraft draft);
    void close();
    void setEdited(NumberExpression expression);

    /** Replace the node at @p path ("" = root) with a catalog choice. */
    void replaceAtPath(const std::string& path, const std::string& choiceId);
    /** Commit a finite literal at @p path when that node is (or becomes) a literal. */
    void setLiteralAtPath(const std::string& path, double value);
    /** Bind a Variable node at @p path to a Number catalog entry. */
    void setVariableAtPath(const std::string& path, NumberVariableScope scope,
                           const GameVariableId& variableId);

    /** ADR §22 — Change opens the categorized picker for one node path. */
    void togglePicker(const std::string& path);
    void closePicker();
    bool hasOpenPicker() const { return pickerPath_.has_value(); }

    Logic::NumberExpressionValidationResult validation() const;
    bool canApply() const;
    std::string renderMarkup(bool playing) const;

private:
    Logic::NumberExpressionContext validationContext() const;

    std::optional<NumberExpressionEditorDraft> draft_;
    /** Path of the node whose Change picker is open; nullopt when collapsed. */
    std::optional<std::string> pickerPath_;
};

} // namespace ArtCade::EditorNative
