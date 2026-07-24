#include "../include/dialog-manager.h"
#include "../include/dialog-parser.h"

#include "../../../core/engine-context.h"
#include "../../event-bus/include/event-bus.h"
#include "../../variable-manager/include/variable-manager.h"
#include "../../time/include/time-manager.h"
#include "../../input/include/input.h"
#include "../../renderer/include/renderer.h"
#include "../../runtime-entity-gateway/include/runtime-entity-gateway.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace ArtCade::Modules {

bool DialogManager::init() { return true; }

void DialogManager::shutdown() {
    endDialog();
    clearTriggerSubscriptions();
    graphs_.clear();
    locale_.clear();
}

void DialogManager::setContext(const EngineContext* ctx) {
    ctx_ = ctx;
}

void DialogManager::setLocaleStrings(
    std::unordered_map<std::string, std::string> strings)
{
    locale_ = std::move(strings);
}

bool DialogManager::loadDialogsFromDirectory(const std::string& projectRoot) {
    clearTriggerSubscriptions();
    graphs_.clear();
    locale_.clear();

    try {
        const fs::path locEn = fs::path(projectRoot) / "locales" / "en.json";
        if (fs::exists(locEn)) {
            std::ifstream in(locEn.string());
            const std::string body(std::istreambuf_iterator<char>(in), {});
            const auto j = nlohmann::json::parse(body, nullptr, false);
            if (!j.is_discarded() && j.is_object()) {
                std::unordered_map<std::string, std::string> table;
                for (auto it = j.begin(); it != j.end(); ++it)
                    if (it.value().is_string())
                        table[it.key()] = it.value().get<std::string>();
                setLocaleStrings(std::move(table));
            }
        }
    } catch (...) { /* optional i18n */ }

    const fs::path dir = fs::path(projectRoot) / "dialogs";
    if (!fs::exists(dir) || !fs::is_directory(dir))
        return true;

    bool anyOk = true;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;
        auto result = DialogParser::parseFile(entry.path().string());
        if (!result.ok()) {
            std::cerr << "[Dialog] " << result.error << " (" << entry.path() << ")\n";
            anyOk = false;
            continue;
        }
        registerGraph(std::move(result.graph));
    }
    return anyOk;
}

bool DialogManager::loadDialogGraphsJson(const std::string& jsonUtf8) {
    endDialog();
    clearTriggerSubscriptions();
    graphs_.clear();
    locale_.clear();

    if (jsonUtf8.empty()) return true;

    try {
        const auto j = nlohmann::json::parse(jsonUtf8);
        if (!j.is_array()) {
            std::cerr << "[Dialog] editor_load_dialogs: expected JSON array\n";
            return false;
        }

        bool anyOk = true;
        for (const auto& item : j) {
            if (!item.is_object()) continue;
            auto result = DialogParser::parseJsonString(item.dump());
            if (!result.ok()) {
                std::cerr << "[Dialog] " << result.error << "\n";
                anyOk = false;
                continue;
            }
            registerGraph(std::move(result.graph));
        }
        return anyOk;
    } catch (const std::exception& ex) {
        std::cerr << "[Dialog] editor_load_dialogs parse error: " << ex.what() << "\n";
        return false;
    }
}

void DialogManager::registerGraph(DialogGraph graph) {
    if (graph.dialogId.empty()) return;
    graphs_[graph.dialogId] = std::move(graph);
}

const DialogGraph* DialogManager::getGraph(const std::string& dialogId) const {
    auto it = graphs_.find(dialogId);
    return it != graphs_.end() ? &it->second : nullptr;
}

EntityId DialogManager::hostEntity() const {
    return session_ ? session_->hostId : 0;
}

bool DialogManager::startDialog(EntityId hostEntityId, const std::string& dialogId) {
    if (session_) return false;
    const DialogGraph* g = getGraph(dialogId);
    if (!g) {
        std::cerr << "[Dialog] unknown dialogId: " << dialogId << "\n";
        return false;
    }

    Session s;
    s.hostId     = hostEntityId;
    s.dialogId   = dialogId;
    s.textSpeed  = 40.f;
    session_     = std::move(s);

    if (ctx_ && ctx_->timeManager)
        session_->pauseToken = ctx_->timeManager->pause("dialog");

    advanceToNode(g->startNode);
    return true;
}

bool DialogManager::startDialog(EntityId hostEntityId, const DialogComponent& component) {
    if (session_) return false;
    if (component.dialogId.empty()) return false;
    const DialogGraph* g = getGraph(component.dialogId);
    if (!g) {
        std::cerr << "[Dialog] unknown dialogId: " << component.dialogId << "\n";
        return false;
    }

    const std::string startNode =
        component.startNode.empty() ? g->startNode : component.startNode;
    if (!g->nodes.count(startNode)) {
        std::cerr << "[Dialog] startNode not found: " << startNode << "\n";
        return false;
    }

    Session s;
    s.hostId     = hostEntityId;
    s.dialogId   = component.dialogId;
    s.textSpeed  = component.textSpeed > 0.f ? component.textSpeed : 40.f;
    session_     = std::move(s);

    if (ctx_ && ctx_->timeManager)
        session_->pauseToken = ctx_->timeManager->pause("dialog");

    advanceToNode(startNode);
    return true;
}

void DialogManager::endDialog() {
    if (!session_) return;
    if (ctx_ && ctx_->timeManager && session_->pauseToken != 0)
        ctx_->timeManager->resume(session_->pauseToken);
    session_.reset();
}

void DialogManager::clearTriggerSubscriptions() {
    if (ctx_ && ctx_->eventBus) {
        for (const auto& [_, token] : triggerSubscriptions_)
            ctx_->eventBus->unsubscribe(token);
    }
    triggerSubscriptions_.clear();
}

void DialogManager::syncTriggerSubscriptions() {
    if (!ctx_ || !ctx_->eventBus || !ctx_->entityGateway) {
        clearTriggerSubscriptions();
        return;
    }

    std::unordered_set<std::string> needed;
    for (const EntityId id : ctx_->entityGateway->activeSceneIds()) {
        DialogComponent component{};
        if (!ctx_->entityGateway->getDialog(id, component)) continue;
        if (!component.triggerMessage.empty())
            needed.insert(component.triggerMessage);
    }

    for (auto it = triggerSubscriptions_.begin(); it != triggerSubscriptions_.end(); ) {
        if (needed.count(it->first)) {
            ++it;
            continue;
        }
        ctx_->eventBus->unsubscribe(it->second);
        it = triggerSubscriptions_.erase(it);
    }

    for (const auto& eventName : needed) {
        if (triggerSubscriptions_.count(eventName)) continue;
        triggerSubscriptions_[eventName] = ctx_->eventBus->subscribe(
            eventName,
            [this, eventName](const std::any&) {
                handleTriggerMessage(eventName);
            });
    }
}

void DialogManager::handleTriggerMessage(const std::string& eventName) {
    if (session_ || !ctx_ || !ctx_->entityGateway) return;
    for (const EntityId id : ctx_->entityGateway->activeSceneIds()) {
        DialogComponent component{};
        if (!ctx_->entityGateway->getDialog(id, component)) continue;
        if (component.triggerMessage != eventName) continue;
        if (startDialog(id, component)) return;
    }
}

const DialogNode* DialogManager::currentNode() const {
    if (!session_) return nullptr;
    const DialogGraph* g = getGraph(session_->dialogId);
    if (!g) return nullptr;
    auto it = g->nodes.find(session_->currentNodeId);
    return it != g->nodes.end() ? &it->second : nullptr;
}

std::string DialogManager::resolveLineText(const DialogNode& node) const {
    if (!node.textKey.empty()) {
        auto it = locale_.find(node.textKey);
        if (it != locale_.end()) return it->second;
    }
    return node.text;
}

void DialogManager::markVisited(const std::string& nodeId) {
    if (!session_) return;
    session_->visitedNodes.insert(nodeId);
}

void DialogManager::advanceToNode(const std::string& nodeId) {
    if (!session_) return;
    session_->currentNodeId = nodeId;
    session_->phase         = DialogWaitPhase::None;
    session_->typewriter    = {};
    session_->selectedChoice = 0;
    markVisited(nodeId);
    processInstantNodes();
}

bool DialogManager::evaluateCondition(const DialogNode& node) const {
    if (!ctx_ || !ctx_->variableManager) return false;
    auto* vm = ctx_->variableManager;
    const std::optional<double> lhs = vm->tryGetNumber(node.variable);
    if (!lhs) return false;
    const float rhs = node.value;
    if (node.op == ">=") return *lhs >= rhs;
    if (node.op == "<=") return *lhs <= rhs;
    if (node.op == "!=") return *lhs != rhs;
    return *lhs == rhs;
}

bool DialogManager::applySetVariable(const DialogNode& node) {
    if (!ctx_ || !ctx_->variableManager) {
        std::cerr << "[Dialog] SetVariable rejected: VariableManager unavailable ("
                  << node.variable << ")\n";
        return false;
    }
    if (node.variable.empty()) {
        std::cerr << "[Dialog] SetVariable rejected: empty variable key\n";
        return false;
    }
    auto* vm = ctx_->variableManager;
    VariableMutationResult result;
    if (node.operation == "+=") {
        result = vm->addNumber(node.variable, static_cast<double>(node.value));
    } else if (node.operation == "-=") {
        result = vm->addNumber(node.variable, static_cast<double>(-node.value));
    } else {
        result = vm->setGlobal(node.variable, static_cast<double>(node.value));
    }
    if (result.accepted()) return true;

    const char* reason = "unknown";
    switch (result.status) {
    case VariableMutationStatus::MissingVariable:
        reason = "missing catalog variable";
        break;
    case VariableMutationStatus::TypeMismatch:
        reason = "type mismatch (Number required)";
        break;
    case VariableMutationStatus::NonFiniteValue:
        reason = "non-finite value";
        break;
    case VariableMutationStatus::Changed:
    case VariableMutationStatus::Unchanged:
        break;
    }
    std::cerr << "[Dialog] SetVariable rejected for '" << node.variable << "': " << reason
              << "\n";
    return false;
}

void DialogManager::processInstantNodes() {
    while (session_) {
        const DialogNode* node = currentNode();
        if (!node) {
            endDialog();
            return;
        }

        switch (node->type) {
        case DialogNodeType::Condition: {
            const std::string next =
                evaluateCondition(*node) ? node->ifTrue : node->ifFalse;
            if (next.empty()) {
                endDialog();
                return;
            }
            session_->currentNodeId = next;
            markVisited(next);
            continue;
        }
        case DialogNodeType::SetVariable:
            // Catalog-only: rejected mutations are logged; dialog still advances so
            // a bad authoring key cannot freeze the conversation loop.
            (void)applySetVariable(*node);
            if (node->next.empty()) {
                endDialog();
                return;
            }
            session_->currentNodeId = node->next;
            markVisited(node->next);
            continue;
        case DialogNodeType::EmitEvent:
            if (ctx_ && ctx_->eventBus && !node->event.empty())
                ctx_->eventBus->emitDeferred(node->event);
            if (node->next.empty()) {
                endDialog();
                return;
            }
            session_->currentNodeId = node->next;
            markVisited(node->next);
            continue;
        case DialogNodeType::End:
            endDialog();
            return;
        case DialogNodeType::Say: {
            session_->phase = DialogWaitPhase::Text;
            session_->typewriter.fullText = resolveLineText(*node);
            session_->typewriter.visibleText.clear();
            session_->typewriter.timer    = 0.f;
            session_->typewriter.complete = session_->typewriter.fullText.empty();
            return;
        }
        case DialogNodeType::Choice:
            session_->phase = DialogWaitPhase::Choice;
            return;
        default:
            endDialog();
            return;
        }
    }
}

bool DialogManager::handleInput() {
    if (!session_ || !ctx_ || !ctx_->input) return false;
    Input* in = ctx_->input;

    const bool confirm =
        in->wasKeyPressed("Enter") || in->wasKeyPressed("Space");
    const bool up   = in->wasKeyPressed("ArrowUp");
    const bool down = in->wasKeyPressed("ArrowDown");

    const DialogNode* node = currentNode();
    if (!node) return false;

    if (session_->phase == DialogWaitPhase::Text) {
        if (!confirm) return false;
        if (!session_->typewriter.complete) {
            session_->typewriter.visibleText = session_->typewriter.fullText;
            session_->typewriter.complete    = true;
            return true;
        }
        if (node->next.empty()) {
            endDialog();
            return true;
        }
        advanceToNode(node->next);
        return true;
    }

    if (session_->phase == DialogWaitPhase::Choice) {
        if (!node->options.empty()) {
            if (up)
                session_->selectedChoice =
                    (session_->selectedChoice + static_cast<int>(node->options.size()) - 1)
                    % static_cast<int>(node->options.size());
            if (down)
                session_->selectedChoice =
                    (session_->selectedChoice + 1)
                    % static_cast<int>(node->options.size());
        }
        if (!confirm) return false;
        if (node->options.empty()) {
            endDialog();
            return true;
        }
        const auto& opt =
            node->options[static_cast<std::size_t>(session_->selectedChoice)];
        if (opt.next.empty()) {
            endDialog();
            return true;
        }
        advanceToNode(opt.next);
        return true;
    }

    return false;
}

void DialogManager::tick(float dt) {
    syncTriggerSubscriptions();
    if (!session_) return;

    handleInput();

    const DialogNode* node = currentNode();
    if (!node) return;

    if (session_->phase == DialogWaitPhase::Text) {
        updateTypewriter(session_->typewriter, dt, session_->textSpeed);
    }
}

void DialogManager::render() {
    if (!session_ || !ctx_ || !ctx_->renderer) return;
    const DialogNode* node = currentNode();
    if (!node) return;

    const std::vector<DialogChoiceOption>* choices = nullptr;
    if (session_->phase == DialogWaitPhase::Choice)
        choices = &node->options;

    renderer_.draw(*ctx_->renderer,
                   node->character,
                   session_->typewriter,
                   choices,
                   session_->selectedChoice,
                   node->portrait);
}

} // namespace ArtCade::Modules
