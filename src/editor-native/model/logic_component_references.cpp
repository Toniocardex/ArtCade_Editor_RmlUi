#include "editor-native/model/logic_component_references.h"

#include "editor-native/model/project_document.h"
#include "logic-core.h"

#include <optional>

namespace ArtCade::EditorNative {
namespace {

bool requiresComponent(const Logic::LogicBlockDescriptor& descriptor,
                       Logic::LogicRequiredComponent component) {
    for (const Logic::LogicRequiredComponent required : descriptor.requiredComponents) {
        if (required == component) return true;
    }
    return false;
}

void appendRef(LogicComponentReferenceReport& report,
               const ObjectTypeId& objectTypeId,
               const LogicRuleId& ruleId,
               LogicReferenceSlot slot,
               std::size_t blockIndex,
               const LogicBlockDef& block,
               const Logic::LogicBlockDescriptor& descriptor) {
    LogicComponentReference ref;
    ref.objectTypeId = objectTypeId;
    ref.ruleId = ruleId;
    ref.slot = slot;
    ref.blockIndex = blockIndex;
    ref.typeId = block.typeId;
    ref.displayName = descriptor.displayName.empty() ? block.typeId : descriptor.displayName;
    report.references.push_back(std::move(ref));
}

void collectRequiring(LogicComponentReferenceReport& report,
                      const ObjectTypeId& objectTypeId,
                      const LogicBoardDef& board,
                      Logic::LogicRequiredComponent component) {
    for (const LogicRuleDef& rule : board.rules) {
        if (const Logic::LogicBlockDescriptor* trigger =
                Logic::findDescriptor(rule.trigger.typeId)) {
            if (requiresComponent(*trigger, component)) {
                appendRef(report, objectTypeId, rule.id, LogicReferenceSlot::Trigger, 0,
                          rule.trigger, *trigger);
            }
        }
        for (std::size_t i = 0; i < rule.conditions.size(); ++i) {
            const LogicBlockDef& block = rule.conditions[i].block;
            if (const Logic::LogicBlockDescriptor* descriptor =
                    Logic::findDescriptor(block.typeId)) {
                if (requiresComponent(*descriptor, component)) {
                    appendRef(report, objectTypeId, rule.id, LogicReferenceSlot::Condition, i,
                              block, *descriptor);
                }
            }
        }
        for (std::size_t i = 0; i < rule.actions.size(); ++i) {
            const LogicBlockDef& block = rule.actions[i];
            if (const Logic::LogicBlockDescriptor* descriptor =
                    Logic::findDescriptor(block.typeId)) {
                if (requiresComponent(*descriptor, component)) {
                    appendRef(report, objectTypeId, rule.id, LogicReferenceSlot::Action, i,
                              block, *descriptor);
                }
            }
        }
    }
}

bool blockIncompatible(const EntityDef& owner,
                       const LogicBlockDef& block,
                       const Logic::LogicBlockDescriptor* trigger) {
    const Logic::LogicBlockDescriptor* descriptor = Logic::findDescriptor(block.typeId);
    if (!descriptor) return false;
    return !Logic::blockAvailability(owner, *descriptor, trigger).compatible;
}

} // namespace

std::size_t LogicComponentReferenceReport::triggerCount() const {
    std::size_t n = 0;
    for (const LogicComponentReference& ref : references)
        if (ref.slot == LogicReferenceSlot::Trigger) ++n;
    return n;
}

std::size_t LogicComponentReferenceReport::conditionCount() const {
    std::size_t n = 0;
    for (const LogicComponentReference& ref : references)
        if (ref.slot == LogicReferenceSlot::Condition) ++n;
    return n;
}

std::size_t LogicComponentReferenceReport::actionCount() const {
    std::size_t n = 0;
    for (const LogicComponentReference& ref : references)
        if (ref.slot == LogicReferenceSlot::Action) ++n;
    return n;
}

std::optional<Logic::LogicRequiredComponent> logicRequiredComponentFor(ComponentKind kind) {
    switch (kind) {
    case ComponentKind::TopDownController:
        return Logic::LogicRequiredComponent::TopDownController;
    case ComponentKind::PlatformerController:
        return Logic::LogicRequiredComponent::PlatformerController;
    case ComponentKind::SpriteAnimator:
        return Logic::LogicRequiredComponent::SpriteAnimator;
    default:
        return std::nullopt;
    }
}

LogicComponentReferenceReport collectComponentLogicReferences(
    const ProjectDocument& document,
    const ObjectTypeId& objectTypeId,
    ComponentKind component) {
    LogicComponentReferenceReport report;
    const auto required = logicRequiredComponentFor(component);
    if (!required) return report;
    const EntityDef* type = document.findObjectType(objectTypeId);
    if (!type || !type->logicBoard) return report;
    collectRequiring(report, objectTypeId, *type->logicBoard, *required);
    return report;
}

LogicComponentReferenceReport collectIncompatibleLogicReferences(
    const ProjectDocument& document,
    const ObjectTypeId& objectTypeId) {
    LogicComponentReferenceReport report;
    const EntityDef* type = document.findObjectType(objectTypeId);
    if (!type || !type->logicBoard) return report;
    const LogicBoardDef& board = *type->logicBoard;
    for (const LogicRuleDef& rule : board.rules) {
        const Logic::LogicBlockDescriptor* triggerDesc =
            Logic::findDescriptor(rule.trigger.typeId);
        if (blockIncompatible(*type, rule.trigger, nullptr)) {
            if (triggerDesc) {
                appendRef(report, objectTypeId, rule.id, LogicReferenceSlot::Trigger, 0,
                          rule.trigger, *triggerDesc);
            }
        }
        for (std::size_t i = 0; i < rule.conditions.size(); ++i) {
            const LogicBlockDef& block = rule.conditions[i].block;
            if (blockIncompatible(*type, block, triggerDesc)) {
                if (const Logic::LogicBlockDescriptor* descriptor =
                        Logic::findDescriptor(block.typeId)) {
                    appendRef(report, objectTypeId, rule.id, LogicReferenceSlot::Condition, i,
                              block, *descriptor);
                }
            }
        }
        for (std::size_t i = 0; i < rule.actions.size(); ++i) {
            const LogicBlockDef& block = rule.actions[i];
            if (blockIncompatible(*type, block, triggerDesc)) {
                if (const Logic::LogicBlockDescriptor* descriptor =
                        Logic::findDescriptor(block.typeId)) {
                    appendRef(report, objectTypeId, rule.id, LogicReferenceSlot::Action, i,
                              block, *descriptor);
                }
            }
        }
    }
    return report;
}

} // namespace ArtCade::EditorNative
