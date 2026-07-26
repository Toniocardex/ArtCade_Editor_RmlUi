#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_command.h"

#include <optional>
#include <string>

namespace ArtCade::EditorNative {

class SetObjectTypeGaugeComponentCommand final : public EditorCommand {
public:
    SetObjectTypeGaugeComponentCommand(ObjectTypeId objectTypeId,
                                       std::optional<GaugeComponent> next);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetObjectTypeGaugeComponent"; }

private:
    ObjectTypeId objectTypeId_;
    std::optional<GaugeComponent> next_;
    std::optional<GaugeComponent> previous_;
    bool captured_ = false;
};

bool sameGaugeComponent(const GaugeComponent& lhs, const GaugeComponent& rhs);

} // namespace ArtCade::EditorNative
