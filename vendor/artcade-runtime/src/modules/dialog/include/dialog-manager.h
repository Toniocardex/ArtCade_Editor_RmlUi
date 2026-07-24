#pragma once

#include "../../../core/module.h"
#include "../../../core/types.h"
#include "dialog-types.h"
#include "dialog-renderer.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>

namespace ArtCade {

struct EngineContext;

namespace Modules {

class EventBus;
class VariableManager;
class TimeManager;
class Input;
class Renderer;

class DialogManager final : public IModule {
public:
    DialogManager() = default;

    bool init() override;
    void shutdown() override;

    void setContext(const EngineContext* ctx);

    bool loadDialogsFromDirectory(const std::string& projectRoot);
    /** Replace all graphs from a JSON array of dialog graph objects (editor preview). */
    bool loadDialogGraphsJson(const std::string& jsonUtf8);
    void registerGraph(DialogGraph graph);
    const DialogGraph* getGraph(const std::string& dialogId) const;

    bool startDialog(EntityId hostEntityId, const std::string& dialogId);
    bool startDialog(EntityId hostEntityId, const DialogComponent& component);
    void endDialog();

    void tick(float dt);
    void render();

    bool isActive() const { return session_.has_value(); }
    bool isBlocking() const { return isActive(); }

    EntityId hostEntity() const;

    /** Optional locale table: textKey → localized string. */
    void setLocaleStrings(std::unordered_map<std::string, std::string> strings);

private:
    const EngineContext* ctx_ = nullptr;
    DialogRenderer       renderer_;

    std::unordered_map<std::string, DialogGraph> graphs_;
    std::unordered_map<std::string, std::string> locale_;
    std::unordered_map<std::string, uint32_t> triggerSubscriptions_;

    struct Session {
        EntityId              hostId = 0;
        std::string           dialogId;
        std::string           currentNodeId;
        DialogWaitPhase       phase = DialogWaitPhase::None;
        TypewriterState       typewriter;
        int                   selectedChoice = 0;
        float                 textSpeed    = 40.f;
        std::unordered_set<std::string> visitedNodes;
        uint32_t              pauseToken = 0;
    };
    std::optional<Session> session_;

    const DialogNode* currentNode() const;
    std::string resolveLineText(const DialogNode& node) const;
    void advanceToNode(const std::string& nodeId);
    void processInstantNodes();
    bool handleInput();
    void markVisited(const std::string& nodeId);
    void clearTriggerSubscriptions();
    void syncTriggerSubscriptions();
    void handleTriggerMessage(const std::string& eventName);
    bool evaluateCondition(const DialogNode& node) const;
    /**
     * Mutates a catalog Number global for a SetVariable node.
     * @return false when VariableManager is missing or the mutation is rejected
     */
    [[nodiscard]] bool applySetVariable(const DialogNode& node);
};

} // namespace Modules
} // namespace ArtCade
