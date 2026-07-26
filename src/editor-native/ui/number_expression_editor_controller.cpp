#include "editor-native/ui/number_expression_editor_controller.h"

#include "editor-native/ui/ui_markup.h"

#include <cmath>
#include <sstream>

namespace ArtCade::EditorNative {
namespace {

GameVariableId firstNumberKey(const std::vector<GameVariableDefinition>& variables) {
    GameVariableId best;
    for (const GameVariableDefinition& variable : variables) {
        if (variable.type != GameVariableDefinition::Type::Number || variable.key.empty())
            continue;
        if (best.empty() || variable.key < best) best = variable.key;
    }
    return best;
}

NumberExpression makeChoice(const std::string& choiceId,
                            const NumberExpressionEditorDraft& draft) {
    if (choiceId == "literal") return NumberExpression::literal(0.0);
    if (choiceId == "var.local") {
        NumberVariableExpression node;
        node.scope = NumberVariableScope::Local;
        node.variableId = firstNumberKey(draft.localNumberVariables);
        return NumberExpression{std::move(node)};
    }
    if (choiceId == "var.global") {
        NumberVariableExpression node;
        node.scope = NumberVariableScope::Global;
        node.variableId = firstNumberKey(draft.globalNumberVariables);
        return NumberExpression{std::move(node)};
    }
    if (choiceId == "self.x")
        return NumberExpression{NumberPropertyExpression{NumberProperty::SelfPositionX}};
    if (choiceId == "self.y")
        return NumberExpression{NumberPropertyExpression{NumberProperty::SelfPositionY}};
    if (choiceId == "scene.w")
        return NumberExpression{NumberPropertyExpression{NumberProperty::SceneWorldWidth}};
    if (choiceId == "scene.h")
        return NumberExpression{NumberPropertyExpression{NumberProperty::SceneWorldHeight}};
    if (choiceId == "delta")
        return NumberExpression{NumberPropertyExpression{NumberProperty::DeltaSeconds}};
    if (choiceId == "random") {
        NumberRandomRangeExpression node;
        node.minimum = boxNumberExpression(NumberExpression::literal(0.0));
        node.maximum = boxNumberExpression(NumberExpression::literal(1.0));
        return NumberExpression{std::move(node)};
    }
    if (choiceId == "clamp") {
        NumberClampExpression node;
        node.value = boxNumberExpression(NumberExpression::literal(0.0));
        node.minimum = boxNumberExpression(NumberExpression::literal(0.0));
        node.maximum = boxNumberExpression(NumberExpression::literal(1.0));
        return NumberExpression{std::move(node)};
    }
    if (choiceId == "add") {
        NumberBinaryExpression node;
        node.operation = NumberBinaryOperator::Add;
        node.left = boxNumberExpression(NumberExpression::literal(0.0));
        node.right = boxNumberExpression(NumberExpression::literal(0.0));
        return NumberExpression{std::move(node)};
    }
    if (choiceId == "sub") {
        NumberBinaryExpression node;
        node.operation = NumberBinaryOperator::Subtract;
        node.left = boxNumberExpression(NumberExpression::literal(0.0));
        node.right = boxNumberExpression(NumberExpression::literal(0.0));
        return NumberExpression{std::move(node)};
    }
    if (choiceId == "mul") {
        NumberBinaryExpression node;
        node.operation = NumberBinaryOperator::Multiply;
        node.left = boxNumberExpression(NumberExpression::literal(1.0));
        node.right = boxNumberExpression(NumberExpression::literal(1.0));
        return NumberExpression{std::move(node)};
    }
    if (choiceId == "div") {
        NumberBinaryExpression node;
        node.operation = NumberBinaryOperator::Divide;
        node.left = boxNumberExpression(NumberExpression::literal(0.0));
        node.right = boxNumberExpression(NumberExpression::literal(1.0));
        return NumberExpression{std::move(node)};
    }
    if (choiceId == "min") {
        NumberBinaryExpression node;
        node.operation = NumberBinaryOperator::Minimum;
        node.left = boxNumberExpression(NumberExpression::literal(0.0));
        node.right = boxNumberExpression(NumberExpression::literal(0.0));
        return NumberExpression{std::move(node)};
    }
    if (choiceId == "max") {
        NumberBinaryExpression node;
        node.operation = NumberBinaryOperator::Maximum;
        node.left = boxNumberExpression(NumberExpression::literal(0.0));
        node.right = boxNumberExpression(NumberExpression::literal(0.0));
        return NumberExpression{std::move(node)};
    }
    if (choiceId == "neg") {
        NumberUnaryExpression node;
        node.operation = NumberUnaryOperator::Negate;
        node.operand = boxNumberExpression(NumberExpression::literal(0.0));
        return NumberExpression{std::move(node)};
    }
    if (choiceId == "abs") {
        NumberUnaryExpression node;
        node.operation = NumberUnaryOperator::Absolute;
        node.operand = boxNumberExpression(NumberExpression::literal(0.0));
        return NumberExpression{std::move(node)};
    }
    if (choiceId == "floor") {
        NumberUnaryExpression node;
        node.operation = NumberUnaryOperator::Floor;
        node.operand = boxNumberExpression(NumberExpression::literal(0.0));
        return NumberExpression{std::move(node)};
    }
    if (choiceId == "ceil") {
        NumberUnaryExpression node;
        node.operation = NumberUnaryOperator::Ceil;
        node.operand = boxNumberExpression(NumberExpression::literal(0.0));
        return NumberExpression{std::move(node)};
    }
    if (choiceId == "round") {
        NumberUnaryExpression node;
        node.operation = NumberUnaryOperator::Round;
        node.operand = boxNumberExpression(NumberExpression::literal(0.0));
        return NumberExpression{std::move(node)};
    }
    if (choiceId == "lerp") {
        NumberLerpExpression node;
        node.from = boxNumberExpression(NumberExpression::literal(0.0));
        node.to = boxNumberExpression(NumberExpression::literal(1.0));
        node.amount = boxNumberExpression(NumberExpression::literal(0.5));
        return NumberExpression{std::move(node)};
    }
    return NumberExpression::literal(0.0);
}

std::string childPath(const std::string& parent, const char* segment) {
    return parent.empty() ? std::string(segment) : parent + "/" + segment;
}

NumberExpression* resolveMutable(NumberExpression& root, const std::string& path) {
    if (path.empty()) return &root;
    NumberExpression* current = &root;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t pos = path.find('/', start);
        const std::string segment = pos == std::string::npos
            ? path.substr(start) : path.substr(start, pos - start);
        if (segment.empty()) return nullptr;
        auto& variant = current->value();
        if (auto* unary = std::get_if<NumberUnaryExpression>(&variant)) {
            if (segment != "operand" || !unary->operand) return nullptr;
            current = unary->operand.get();
        } else if (auto* binary = std::get_if<NumberBinaryExpression>(&variant)) {
            if (segment == "left" && binary->left) current = binary->left.get();
            else if (segment == "right" && binary->right) current = binary->right.get();
            else return nullptr;
        } else if (auto* clamp = std::get_if<NumberClampExpression>(&variant)) {
            if (segment == "value" && clamp->value) current = clamp->value.get();
            else if (segment == "min" && clamp->minimum) current = clamp->minimum.get();
            else if (segment == "max" && clamp->maximum) current = clamp->maximum.get();
            else return nullptr;
        } else if (auto* lerp = std::get_if<NumberLerpExpression>(&variant)) {
            if (segment == "from" && lerp->from) current = lerp->from.get();
            else if (segment == "to" && lerp->to) current = lerp->to.get();
            else if (segment == "amount" && lerp->amount) current = lerp->amount.get();
            else return nullptr;
        } else if (auto* random = std::get_if<NumberRandomRangeExpression>(&variant)) {
            if (segment == "min" && random->minimum) current = random->minimum.get();
            else if (segment == "max" && random->maximum) current = random->maximum.get();
            else return nullptr;
        } else {
            return nullptr;
        }
        if (pos == std::string::npos) break;
        start = pos + 1;
    }
    return current;
}

const char* kindLabel(NumberExpressionKind kind) {
    switch (kind) {
        case NumberExpressionKind::Literal: return "Number";
        case NumberExpressionKind::Property: return "Property";
        case NumberExpressionKind::Variable: return "Variable";
        case NumberExpressionKind::Unary: return "Unary";
        case NumberExpressionKind::Binary: return "Binary";
        case NumberExpressionKind::Clamp: return "Clamp";
        case NumberExpressionKind::Lerp: return "Lerp";
        case NumberExpressionKind::RandomRange: return "Random Range";
    }
    return "Expression";
}

std::string formatLiteral(double value) {
    if (!std::isfinite(value)) return "0";
    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(4);
    stream << value;
    std::string text = stream.str();
    while (text.size() > 1 && text.back() == '0') text.pop_back();
    if (!text.empty() && text.back() == '.') text.pop_back();
    return text;
}

void appendPickButton(std::string& html, const std::string& path, const char* id,
                      const char* label, bool playing) {
    html += "<button class=\"panel-btn number-expression-pick-btn\" "
            "data-action=\"number-expression-pick\" data-arg=\""
          + escapeRml(path.empty() ? std::string(id) : path + "|" + id) + "\"";
    if (playing) html += " disabled=\"disabled\"";
    html += "><span class=\"number-expression-pick-label\">"
          + escapeRml(label) + "</span></button>";
}

void appendPickerCategory(std::string& html, const char* title) {
    html += "<div class=\"number-expression-picker-category\">"
            "<span class=\"number-expression-picker-category-label\">"
          + escapeRml(title) + "</span></div>";
}

void appendPicker(std::string& html, const std::string& path, bool playing,
                  const NumberExpressionEditorDraft& draft) {
    html += "<div class=\"number-expression-picker\">"
            "<span class=\"number-expression-picker-label\">Replace with</span>";

    appendPickerCategory(html, "VALUE");
    html += "<div class=\"number-expression-picker-row\">";
    appendPickButton(html, path, "literal", "Number", playing);
    if (!firstNumberKey(draft.localNumberVariables).empty())
        appendPickButton(html, path, "var.local", "Local Variable", playing);
    if (!firstNumberKey(draft.globalNumberVariables).empty())
        appendPickButton(html, path, "var.global", "Global Variable", playing);
    html += "</div>";

    appendPickerCategory(html, "CONTEXT");
    html += "<div class=\"number-expression-picker-row\">";
    if (draft.selfAvailable) {
        appendPickButton(html, path, "self.x", "Self Position X", playing);
        appendPickButton(html, path, "self.y", "Self Position Y", playing);
    }
    if (draft.sceneAvailable) {
        appendPickButton(html, path, "scene.w", "Scene World Width", playing);
        appendPickButton(html, path, "scene.h", "Scene World Height", playing);
    }
    if (draft.deltaSecondsAvailable)
        appendPickButton(html, path, "delta", "Delta Seconds", playing);
    html += "</div>";

    appendPickerCategory(html, "MATH");
    html += "<div class=\"number-expression-picker-row\">";
    appendPickButton(html, path, "add", "Add", playing);
    appendPickButton(html, path, "sub", "Subtract", playing);
    appendPickButton(html, path, "mul", "Multiply", playing);
    appendPickButton(html, path, "div", "Divide", playing);
    appendPickButton(html, path, "min", "Minimum", playing);
    appendPickButton(html, path, "max", "Maximum", playing);
    appendPickButton(html, path, "neg", "Negate", playing);
    appendPickButton(html, path, "abs", "Absolute", playing);
    appendPickButton(html, path, "floor", "Floor", playing);
    appendPickButton(html, path, "ceil", "Ceil", playing);
    appendPickButton(html, path, "round", "Round", playing);
    appendPickButton(html, path, "clamp", "Clamp", playing);
    appendPickButton(html, path, "lerp", "Lerp", playing);
    html += "</div>";

    appendPickerCategory(html, "RANDOM");
    html += "<div class=\"number-expression-picker-row\">";
    appendPickButton(html, path, "random", "Random Range", playing);
    html += "</div></div>";
}

void appendVariablePicker(std::string& html, const std::string& path,
                          const NumberVariableExpression& node,
                          const NumberExpressionEditorDraft& draft, bool playing) {
    const std::vector<GameVariableDefinition>& catalog =
        node.scope == NumberVariableScope::Global ? draft.globalNumberVariables
                                                  : draft.localNumberVariables;
    html += "<div class=\"number-expression-variable-row\">"
            "<span class=\"number-expression-variable-scope\">"
          + escapeRml(node.scope == NumberVariableScope::Global ? "Global variable"
                                                                : "Local variable")
          + "</span>"
            "<div class=\"number-expression-variable-choices\">";
    if (catalog.empty()) {
        html += "<span class=\"number-expression-variable-empty\">"
                "No Number variables available</span>";
    } else {
        for (const GameVariableDefinition& variable : catalog) {
            if (variable.type != GameVariableDefinition::Type::Number || variable.key.empty())
                continue;
            const bool selected = variable.key == node.variableId;
            html += "<button class=\"panel-btn";
            if (selected) html += " primary";
            html += "\" data-action=\"number-expression-set-variable\" data-arg=\""
                  + escapeRml(path + "|"
                              + (node.scope == NumberVariableScope::Global ? "global"
                                                                          : "local")
                              + "|" + variable.key)
                  + "\"";
            if (playing) html += " disabled=\"disabled\"";
            html += "><span class=\"number-expression-variable-label\">"
                  + escapeRml(variable.key) + "</span></button>";
        }
    }
    html += "</div></div>";
}

void appendChild(std::string& html, const NumberExpression& expr, const std::string& path,
                 const char* role, bool playing, int depth,
                 const NumberExpressionEditorDraft& draft,
                 const std::optional<std::string>& pickerPath);

void appendNode(std::string& html, const NumberExpression& expr, const std::string& path,
                bool playing, int depth, const NumberExpressionEditorDraft& draft,
                const std::optional<std::string>& pickerPath) {
    if (depth > static_cast<int>(kMaximumNumberExpressionDepth)) return;
    const bool pickerOpen = pickerPath && *pickerPath == path;

    html += "<div class=\"number-expression-node\">"
            "<div class=\"number-expression-node-header\">"
            "<span class=\"number-expression-node-kind\">"
          + escapeRml(kindLabel(expr.kind()))
          + "</span>"
            "<span class=\"number-expression-node-summary\">"
          + escapeRml(Logic::formatNumberExpression(
                expr, Logic::NumberExpressionFormatStyle::Compact))
          + "</span>"
            "<button class=\"panel-btn number-expression-change\" "
            "data-action=\"number-expression-toggle-picker\" data-arg=\""
          + escapeRml(path) + "\"";
    if (playing) html += " disabled=\"disabled\"";
    html += "><span>" + escapeRml(pickerOpen ? "Close" : "Change") + "</span></button>"
            "</div>";

    if (const auto* lit = std::get_if<NumberLiteralExpression>(&expr.value())) {
        html += "<div class=\"number-expression-literal-row\">"
                "<span class=\"number-expression-literal-label\">Value</span>"
                "<input type=\"text\" class=\"logic-value-input number-expression-literal-input\""
                " data-action=\"commit-number-expression-literal\" data-arg=\""
              + escapeRml(path) + "\" value=\"" + escapeRml(formatLiteral(lit->value)) + "\"";
        if (playing) html += " disabled=\"disabled\"";
        html += "/></div>";
    } else if (const auto* variable = std::get_if<NumberVariableExpression>(&expr.value())) {
        appendVariablePicker(html, path, *variable, draft, playing);
    }

    if (pickerOpen) appendPicker(html, path, playing, draft);

    if (const auto* unary = std::get_if<NumberUnaryExpression>(&expr.value())) {
        if (unary->operand)
            appendChild(html, *unary->operand, childPath(path, "operand"), "Operand",
                        playing, depth + 1, draft, pickerPath);
    } else if (const auto* binary = std::get_if<NumberBinaryExpression>(&expr.value())) {
        if (binary->left)
            appendChild(html, *binary->left, childPath(path, "left"), "Left", playing,
                        depth + 1, draft, pickerPath);
        if (binary->right)
            appendChild(html, *binary->right, childPath(path, "right"), "Right", playing,
                        depth + 1, draft, pickerPath);
    } else if (const auto* clamp = std::get_if<NumberClampExpression>(&expr.value())) {
        if (clamp->value)
            appendChild(html, *clamp->value, childPath(path, "value"), "Value", playing,
                        depth + 1, draft, pickerPath);
        if (clamp->minimum)
            appendChild(html, *clamp->minimum, childPath(path, "min"), "Minimum", playing,
                        depth + 1, draft, pickerPath);
        if (clamp->maximum)
            appendChild(html, *clamp->maximum, childPath(path, "max"), "Maximum", playing,
                        depth + 1, draft, pickerPath);
    } else if (const auto* lerp = std::get_if<NumberLerpExpression>(&expr.value())) {
        if (lerp->from)
            appendChild(html, *lerp->from, childPath(path, "from"), "From", playing,
                        depth + 1, draft, pickerPath);
        if (lerp->to)
            appendChild(html, *lerp->to, childPath(path, "to"), "To", playing, depth + 1,
                        draft, pickerPath);
        if (lerp->amount)
            appendChild(html, *lerp->amount, childPath(path, "amount"), "Amount", playing,
                        depth + 1, draft, pickerPath);
    } else if (const auto* random = std::get_if<NumberRandomRangeExpression>(&expr.value())) {
        if (random->minimum)
            appendChild(html, *random->minimum, childPath(path, "min"), "Minimum", playing,
                        depth + 1, draft, pickerPath);
        if (random->maximum)
            appendChild(html, *random->maximum, childPath(path, "max"), "Maximum", playing,
                        depth + 1, draft, pickerPath);
    }

    html += "</div>";
}

void appendChild(std::string& html, const NumberExpression& expr, const std::string& path,
                 const char* role, bool playing, int depth,
                 const NumberExpressionEditorDraft& draft,
                 const std::optional<std::string>& pickerPath) {
    html += "<div class=\"number-expression-child\">"
            "<span class=\"number-expression-child-label\">"
          + escapeRml(role) + "</span>";
    appendNode(html, expr, path, playing, depth, draft, pickerPath);
    html += "</div>";
}

std::vector<GameVariableDefinition> numberVariablesOnly(
    const std::vector<GameVariableDefinition>& source) {
    std::vector<GameVariableDefinition> out;
    for (const GameVariableDefinition& variable : source) {
        if (variable.type == GameVariableDefinition::Type::Number && !variable.key.empty())
            out.push_back(variable);
    }
    return out;
}

} // namespace

Logic::NumberExpressionContext NumberExpressionEditorController::validationContext() const {
    Logic::NumberExpressionContext context;
    if (!draft_) return context;
    context.selfAvailable = draft_->selfAvailable;
    context.sceneAvailable = draft_->sceneAvailable;
    context.deltaSecondsAvailable = draft_->deltaSecondsAvailable;
    context.localVariables = &draft_->localNumberVariables;
    context.globalVariables = &draft_->globalNumberVariables;
    return context;
}

void NumberExpressionEditorController::open(NumberExpressionEditorDraft draft) {
    draft.localNumberVariables = numberVariablesOnly(draft.localNumberVariables);
    draft.globalNumberVariables = numberVariablesOnly(draft.globalNumberVariables);
    draft.edited = draft.original;
    draft_ = std::move(draft);
    pickerPath_.reset();
}

void NumberExpressionEditorController::close() {
    draft_.reset();
    pickerPath_.reset();
}

void NumberExpressionEditorController::setEdited(NumberExpression expression) {
    if (!draft_) return;
    draft_->edited = std::move(expression);
}

void NumberExpressionEditorController::replaceAtPath(const std::string& path,
                                                     const std::string& choiceId) {
    if (!draft_) return;
    NumberExpression* target = resolveMutable(draft_->edited, path);
    if (!target) return;
    *target = makeChoice(choiceId, *draft_);
    pickerPath_.reset();
}

void NumberExpressionEditorController::setLiteralAtPath(const std::string& path,
                                                        double value) {
    if (!draft_ || !std::isfinite(value)) return;
    NumberExpression* target = resolveMutable(draft_->edited, path);
    if (!target) return;
    *target = NumberExpression::literal(value);
}

void NumberExpressionEditorController::setVariableAtPath(
    const std::string& path, NumberVariableScope scope, const GameVariableId& variableId) {
    if (!draft_ || variableId.empty()) return;
    NumberExpression* target = resolveMutable(draft_->edited, path);
    if (!target) return;
    NumberVariableExpression node;
    node.scope = scope;
    node.variableId = variableId;
    *target = NumberExpression{std::move(node)};
}

void NumberExpressionEditorController::togglePicker(const std::string& path) {
    if (!draft_) return;
    if (pickerPath_ && *pickerPath_ == path) pickerPath_.reset();
    else pickerPath_ = path;
}

void NumberExpressionEditorController::closePicker() { pickerPath_.reset(); }

Logic::NumberExpressionValidationResult NumberExpressionEditorController::validation() const {
    if (!draft_) return {false, "NE_CLOSED", "Editor is closed"};
    return Logic::validateNumberExpression(draft_->edited, validationContext());
}

bool NumberExpressionEditorController::canApply() const {
    if (!draft_) return false;
    if (!validation().ok) return false;
    return !sameNumberExpression(draft_->original, draft_->edited);
}

std::string NumberExpressionEditorController::renderMarkup(bool playing) const {
    if (!draft_) return {};
    const auto validationResult = validation();
    std::string html;
    html += "<div id=\"number-expression-modal\" class=\"editor-modal-backdrop\">"
            "<div class=\"editor-modal number-expression-modal\" data-stop-backdrop=\"1\">"
            "<div class=\"number-expression-header\">"
            "<div class=\"number-expression-header-text\">"
            "<span class=\"number-expression-title\">Edit Number Expression</span>"
            "<span class=\"number-expression-subtitle\">"
          + escapeRml(draft_->title)
          + "</span></div>"
            "<button class=\"help-dialog-close\" data-action=\"cancel-number-expression-editor\">"
            "<span class=\"help-dialog-close-label\">×</span></button></div>"
            "<div class=\"number-expression-result\">"
            "<span class=\"number-expression-result-label\">Result</span>"
            "<span class=\"number-expression-result-text\">"
          + escapeRml(Logic::formatNumberExpression(
                draft_->edited, Logic::NumberExpressionFormatStyle::Full))
          + "</span></div>";
    if (!validationResult.ok) {
        html += "<div class=\"number-expression-error\">"
                "<span class=\"number-expression-error-text\">"
              + escapeRml(validationResult.message.empty() ? validationResult.errorCode
                                                           : validationResult.message)
              + "</span></div>";
    }
    html += "<div class=\"number-expression-tree\">";
    appendNode(html, draft_->edited, "", playing, 0, *draft_, pickerPath_);
    html += "</div>"
            "<div class=\"number-expression-footer\">"
            "<button class=\"panel-btn\" data-action=\"cancel-number-expression-editor\">"
            "<span>Cancel</span></button>"
            "<button class=\"panel-btn primary\" data-action=\"apply-number-expression-editor\"";
    if (playing || !canApply()) html += " disabled=\"disabled\"";
    html += "><span>Apply</span></button></div></div></div>";
    return html;
}

} // namespace ArtCade::EditorNative
