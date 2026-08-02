#include "../include/logic-core.h"
#include "../include/logic-number-expression-compiler.h"
#include "../include/logic-number-expression-format.h"
#include "../include/logic-number-expression-validation.h"
#include "logic-codegen-internal.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <set>
#include <sstream>
#include <unordered_set>

namespace ArtCade::Logic {
namespace {

using CodegenInternal::emitConditionExpression;
using CodegenInternal::emitConditionGuard;
using CodegenInternal::emitConditionsExpression;
using CodegenInternal::escapeLua;

LogicEntityReference selfReference() { return {}; }

const std::vector<LogicRequiredComponentDescriptor>& requiredComponentRegistry()
{
    static const std::vector<LogicRequiredComponentDescriptor> value{
        {LogicRequiredComponent::PlatformerController,
         "platformerController",
         "Platformer Controller"},
        {LogicRequiredComponent::TopDownController,
         "topDownController",
         "Top Down Controller"},
        {LogicRequiredComponent::SpriteAnimator,
         "spriteAnimator",
         "Sprite Animator"},
    };
    return value;
}

LogicDiagnostic makeError(const ObjectTypeId& objectTypeId, const LogicBoardDef& board,
                          std::string code, std::string message,
                          const LogicRuleDef* rule = nullptr,
                          const LogicBlockDef* block = nullptr,
                          std::string property = {}) {
    LogicDiagnostic d;
    d.objectTypeId = objectTypeId;
    d.boardId = board.id;
    d.ruleId = rule ? rule->id : LogicRuleId{};
    d.blockTypeId = block ? block->typeId : std::string{};
    d.propertyKey = std::move(property);
    d.code = std::move(code);
    d.message = std::move(message);
    return d;
}

/**
 * The literal behind a Number property, or nullopt when it is dynamic.
 * ADR-0029 made NumberExpression the only Number arm, so every reader goes
 * through here instead of reaching for a `double` that no longer exists.
 */
std::optional<double> literalNumberOf(const LogicPropertyDef* property) {
    if (!property) return std::nullopt;
    if (const auto* expression = std::get_if<NumberExpression>(&property->value))
        return literalNumberValue(*expression);
    return std::nullopt;
}

/**
 * Literal value for codegen, distinguishing "absent" (documented default
 * still applies, @p out set to @p fallback) from "present but the compiler
 * could not translate it" (ADR-0038 Finding 3: a compile error, not a silent
 * default). Reachable only in practice if a property escapes the
 * NumericExpressionPolicy enforced in validateBlock below — defense in depth,
 * not the primary guard.
 */
bool literalNumberForCodegen(const LogicPropertyDef* property, double fallback, double& out,
                             const ObjectTypeId& objectTypeId, const LogicBoardDef& board,
                             const LogicRuleDef& rule, const LogicBlockDef& block,
                             const std::string& propertyKey,
                             std::vector<LogicDiagnostic>& codegenDiagnostics) {
    out = fallback;
    if (!property) return true;
    const auto* expression = std::get_if<NumberExpression>(&property->value);
    if (!expression) return true;
    if (const auto literal = literalNumberValue(*expression)) {
        out = *literal;
        return true;
    }
    codegenDiagnostics.push_back(makeError(
        objectTypeId, board, "LB_CODEGEN_LITERAL_ONLY",
        "Property holds a dynamic expression that could not be translated: " + propertyKey,
        &rule, &block, propertyKey));
    return false;
}

/**
 * Emit a two-component call whose operands may be dynamic: evaluate both, then
 * guard on finiteness and rate-limit the complaint. Set Position had this
 * inline; every Vec2 action needs the same shape, and a second hand-written
 * copy of a runtime guard is how one of them ends up missing the check.
 *
 * An untranslatable component fails the compile with the compiler's own
 * error (ADR-0038 Finding 3) instead of silently substituting `0` — reachable
 * only if a null child box slips past validation's NE_INCOMPLETE check, so
 * this is defense in depth, not the primary guard.
 */
bool emitGuardedVec2(std::ostringstream& lua, const LogicVec2Value& value,
                     const std::string& diagnosticKey, const std::string& call,
                     const ObjectTypeId& objectTypeId, const LogicBoardDef& board,
                     const LogicRuleDef& rule, const LogicBlockDef& block,
                     const std::string& propertyKey,
                     std::vector<LogicDiagnostic>& codegenDiagnostics) {
    const CompiledNumberExpression compiledX = compileNumberExpressionToLua(value.x);
    const CompiledNumberExpression compiledY = compileNumberExpressionToLua(value.y);
    if (!compiledX.ok || !compiledY.ok) {
        codegenDiagnostics.push_back(makeError(
            objectTypeId, board, "LB_CODEGEN_EXPRESSION",
            !compiledX.ok ? compiledX.error : compiledY.error, &rule, &block, propertyKey));
        return false;
    }
    lua << "      do\n";
    lua << "        local _x = " << compiledX.luaSource << "\n";
    lua << "        local _y = " << compiledY.luaSource << "\n";
    lua << "        if logic.number.is_finite(_x) and logic.number.is_finite(_y) then\n";
    lua << "          " << call << "\n";
    lua << "        else\n";
    lua << "          logic.diagnostics.expression_once(\""
        << escapeLua(diagnosticKey) << "\")\n";
    lua << "        end\n";
    lua << "      end\n";
    return true;
}

LogicValueKind kindOf(const LogicValue& value) {
    if (std::holds_alternative<bool>(value)) return LogicValueKind::Bool;
    if (std::holds_alternative<int64_t>(value)) return LogicValueKind::Integer;
    if (std::holds_alternative<NumberExpression>(value)) return LogicValueKind::Number;
    if (std::holds_alternative<LogicStringValue>(value)) return LogicValueKind::String;
    if (std::holds_alternative<LogicVec2Value>(value)) return LogicValueKind::Vec2;
    if (std::holds_alternative<LogicAssetReference>(value)) return LogicValueKind::Asset;
    if (std::holds_alternative<LogicEntityReference>(value)) return LogicValueKind::Entity;
    if (std::holds_alternative<LogicVariableReference>(value)) return LogicValueKind::Variable;
    return LogicValueKind::Key;
}

bool validId(const std::string& id) {
    return !id.empty() && id.size() <= kMaxLogicIdLength;
}

bool containsCapability(const std::vector<LogicContextCapability>& values,
                        LogicContextCapability expected) {
    return std::find(values.begin(), values.end(), expected) != values.end();
}

LogicBlockAvailability availabilityFor(const EntityDef& owner,
                                       const LogicBlockDescriptor& candidate,
                                       const LogicBlockDescriptor* trigger) {
    for (const LogicRequiredComponent component : candidate.requiredComponents) {
        if (!hasRequiredComponent(owner, component)) {
            const LogicRequiredComponentDescriptor *descriptor =
                requiredComponentDescriptor(component);
            return {false, descriptor ? "Requires " + descriptor->displayName
                                      : "Requires an unsupported component"};
        }
    }
    for (const LogicContextCapability capability : candidate.requiredContext) {
        // Event-slot authoring passes a null parent trigger: Self is the board
        // owner. EventOther and other contextual caps still need a real parent.
        if (!trigger && capability == LogicContextCapability::Self) continue;
        if (!trigger || !containsCapability(trigger->providedContext, capability)) {
            return {false, "Requires a trigger that provides the required context"};
        }
    }
    return {};
}

const SpriteAnimationAssetDef* findAnimationAsset(const ProjectDoc& project,
                                                  const AssetId& assetId) {
    for (const SpriteAnimationAssetDef& asset : project.spriteAnimationAssets) {
        if (asset.id == assetId) return &asset;
    }
    return nullptr;
}

const SpriteAnimationClipDef* findAnimationClip(const SpriteAnimationAssetDef& asset,
                                                const std::string& clipId) {
    for (const SpriteAnimationClipDef& clip : asset.clips) {
        if (clip.id == clipId) return &clip;
    }
    return nullptr;
}

const AudioAssetDef* findAudioAsset(const ProjectDoc& project, const AssetId& assetId) {
    for (const AudioAssetDef& asset : project.audioAssets) {
        if (asset.assetId == assetId) return &asset;
    }
    return nullptr;
}

void validateBlock(const ObjectTypeId& objectTypeId, const LogicBoardDef& board,
                   const LogicRuleDef& rule, const LogicBlockDef& block,
                   BlockKind expected, const EntityDef* owner,
                   const LogicBlockDescriptor* trigger,
                   const ProjectDoc* project,
                   LogicValidationPurpose purpose,
                   std::vector<LogicDiagnostic>& out) {
    const bool structuralOnly = purpose == LogicValidationPurpose::StructuralCommit;
    const bool draftSoft = purpose == LogicValidationPurpose::AuthoringDiagnostics;
    const auto pushSemantic = [&](LogicDiagnostic diagnostic) {
        if (purpose == LogicValidationPurpose::Executable && !rule.enabled)
            diagnostic.severity = DiagnosticSeverity::Warning;
        out.push_back(std::move(diagnostic));
    };

    const LogicBlockDescriptor* descriptor = findDescriptor(block.typeId);
    if (!descriptor) {
        // Unknown catalog entries stay loadable/editable (ADR-0013).
        if (!structuralOnly) {
            pushSemantic(makeError(objectTypeId, board, "LB_UNKNOWN_BLOCK",
                                   "Unknown Logic Board block type: " + block.typeId,
                                   &rule, &block));
        }
        return;
    }
    const bool eventEligibleCondition = expected == BlockKind::Trigger
        && descriptor->kind == BlockKind::Condition
        && isEventEligible(*descriptor);
    if (descriptor->kind != expected && !eventEligibleCondition) {
        out.push_back(makeError(objectTypeId, board, "LB_WRONG_BLOCK_KIND",
                                "Block is used in the wrong rule section", &rule, &block));
        return;
    }
    if (!structuralOnly && owner) {
        const LogicBlockAvailability availability = availabilityFor(*owner, *descriptor, trigger);
        if (!availability.compatible) {
            pushSemantic(makeError(objectTypeId, board, "LB_INCOMPATIBLE_BLOCK",
                                   availability.reason, &rule, &block));
        }
    }

    std::unordered_set<std::string> seen;
    for (const LogicPropertyDef& property : block.properties) {
        if (!seen.insert(property.key).second) {
            out.push_back(makeError(objectTypeId, board, "LB_DUPLICATE_PROPERTY",
                                    "Duplicate property: " + property.key,
                                    &rule, &block, property.key));
            continue;
        }
        const auto it = std::find_if(descriptor->properties.begin(), descriptor->properties.end(),
            [&](const LogicPropertyDescriptor& p) { return p.key == property.key; });
        // ADR-0012: legacy Move Horizontal boards may still store numeric `axis`.
        // It is not in the catalog, but remains valid for compile/Play.
        if (it == descriptor->properties.end()) {
            if (block.typeId == kMoveHorizontal && property.key == "axis") {
                if (const auto literal = literalNumberOf(&property)) {
                    const double* value = &*literal;
                    if (!std::isfinite(*value)) {
                        out.push_back(makeError(objectTypeId, board, "LB_NON_FINITE",
                                                "Number property must be finite", &rule, &block,
                                                property.key));
                    } else if (!structuralOnly && (*value < -1.0 || *value > 1.0)) {
                        pushSemantic(makeError(objectTypeId, board, "LB_AXIS_RANGE",
                                               "Platformer movement axis must be between -1 and 1",
                                               &rule, &block, property.key));
                    }
                } else {
                    out.push_back(makeError(objectTypeId, board, "LB_PROPERTY_TYPE",
                                            "Property has the wrong value type: " + property.key,
                                            &rule, &block, property.key));
                }
                continue;
            }
            out.push_back(makeError(objectTypeId, board, "LB_UNKNOWN_PROPERTY",
                                    "Unknown property: " + property.key,
                                    &rule, &block, property.key));
            continue;
        }
        if (kindOf(property.value) != it->valueKind) {
            out.push_back(makeError(objectTypeId, board, "LB_PROPERTY_TYPE",
                                    "Property has the wrong value type: " + property.key,
                                    &rule, &block, property.key));
        }
        // ADR-0038 Finding 3: NumericExpressionPolicy applies to any property
        // holding a NumberExpression, scalar or Vec2 — not only the Vec2
        // branch. A dedicated Camera Shake special case used to duplicate
        // this for intensity/duration; it is redundant now and removed.
        const auto requireLiteral = [&](const NumberExpression& expression) {
            const NumberExpressionValidationResult result =
                requireLiteralNumberExpression(expression);
            if (!result.ok) {
                out.push_back(makeError(objectTypeId, board, result.errorCode,
                                        result.message, &rule, &block, property.key));
            }
        };
        const auto validateDynamic = [&](const NumberExpression& expression) {
            NumberExpressionContext context;
            context.globalVariables = project ? &project->globalVariables : nullptr;
            context.localVariables = owner ? &owner->localVariables : nullptr;
            context.deltaSecondsAvailable =
                trigger && trigger->typeId == kEveryFrame;
            const NumberExpressionValidationResult result =
                validateNumberExpression(expression, context);
            if (!result.ok) {
                out.push_back(makeError(objectTypeId, board, result.errorCode,
                                        result.message, &rule, &block, property.key));
            }
        };
        if (const NumberExpression* n = std::get_if<NumberExpression>(&property.value)) {
            if (it->numericExpressionPolicy == NumericExpressionPolicy::LiteralOnly) {
                requireLiteral(*n);
            } else {
                validateDynamic(*n);
            }
        } else if (const LogicVec2Value* v = std::get_if<LogicVec2Value>(&property.value)) {
            if (it->numericExpressionPolicy == NumericExpressionPolicy::LiteralOnly) {
                requireLiteral(v->x);
                requireLiteral(v->y);
                if (!structuralOnly && block.typeId == kSetScale && property.key == "scale") {
                    if (const std::optional<Vec2> lit = literalVec2Value(*v)) {
                        if (lit->x <= 0.f || lit->y <= 0.f) {
                            pushSemantic(makeError(objectTypeId, board, "LB_SCALE_POSITIVE",
                                                   "Scale axes must be greater than 0",
                                                   &rule, &block, property.key));
                        }
                    }
                }
            } else {
                validateDynamic(v->x);
                validateDynamic(v->y);
            }
        }
        if (const auto literal = literalNumberOf(&property)) {
                    const double* value = &*literal;
            if (!std::isfinite(*value)) {
                out.push_back(makeError(objectTypeId, board, "LB_NON_FINITE",
                                        "Number property must be finite", &rule, &block,
                                        property.key));
            } else if (!structuralOnly) {
                if (block.typeId == kAnimationSetPlaybackSpeed
                    && property.key == "speed" && *value <= 0.0) {
                    pushSemantic(makeError(objectTypeId, board, "LB_ANIMATION_SPEED",
                                           "Animation playback speed must be positive",
                                           &rule, &block, property.key));
                } else if (block.typeId == kAudioPlaySound && property.key == "volume"
                           && (*value < 0.0 || *value > 1.0)) {
                    pushSemantic(makeError(objectTypeId, board, "LB_AUDIO_VOLUME_RANGE",
                                           "Audio volume must be between 0 and 1",
                                           &rule, &block, property.key));
                } else if ((block.typeId == kEverySeconds || block.typeId == kWait)
                           && property.key == "seconds" && *value <= 0.0) {
                    pushSemantic(makeError(objectTypeId, board, "LB_TIMER_INTERVAL",
                                           "Seconds must be greater than 0",
                                           &rule, &block, property.key));
                } else if (block.typeId == kCameraShake && property.key == "intensity"
                           && (*value < 0.0 || *value > 1.0)) {
                    pushSemantic(makeError(objectTypeId, board, "LB_CAMERA_SHAKE_INTENSITY",
                                           "Camera Shake intensity must be between 0 and 1",
                                           &rule, &block, property.key));
                } else if (block.typeId == kCameraShake && property.key == "duration"
                           && *value <= 0.0) {
                    pushSemantic(makeError(objectTypeId, board, "LB_CAMERA_SHAKE_DURATION",
                                           "Camera Shake duration must be greater than 0",
                                           &rule, &block, property.key));
                } else if (block.typeId == kOutsideSceneBounds && property.key == "margin"
                           && *value < 0.0) {
                    pushSemantic(makeError(objectTypeId, board, "LB_OUTSIDE_SCENE_MARGIN",
                                           "Outside Scene margin must be greater than or equal to 0",
                                           &rule, &block, property.key));
                }
            }
        }
        if (structuralOnly) continue;

        if ((block.typeId == kOtherIsObjectType || block.typeId == kSpawnObject
             || block.typeId == kCollisionEnter || block.typeId == kCollisionExit)
            && property.key == "objectTypeId") {
            const auto* referencedType = std::get_if<LogicStringValue>(&property.value);
            const bool emptyAllowed = block.typeId == kCollisionEnter
                || block.typeId == kCollisionExit;
            if (!referencedType
                || (!emptyAllowed && referencedType->value.empty())
                || (!referencedType->value.empty()
                    && project && project->objectTypes.count(referencedType->value) == 0)) {
                pushSemantic(makeError(objectTypeId, board, "LB_OBJECT_TYPE_REFERENCE",
                                       block.typeId == kSpawnObject
                                           ? "Spawn must reference an existing Object Type"
                                           : "Collision object type must reference an existing Object Type",
                                       &rule, &block, property.key));
            }
        }
        if (block.typeId == kSceneGoTo && property.key == "sceneId") {
            const auto* referencedScene = std::get_if<LogicStringValue>(&property.value);
            if (!referencedScene || referencedScene->value.empty()
                || (project && project->scenes.count(referencedScene->value) == 0)) {
                pushSemantic(makeError(objectTypeId, board, "LB_SCENE_REFERENCE",
                                       "Go To Scene must reference an existing scene",
                                       &rule, &block, property.key));
            }
        }
        if ((block.typeId == kStateSet || block.typeId == kStateAdd
             || block.typeId == kStateSubtract || block.typeId == kStateCompare
             || block.typeId == kStateToggle || block.typeId == kStateCompareBoolean
             || block.typeId == kStateCompareString)
            && property.key == "key") {
            const auto* ref = std::get_if<LogicVariableReference>(&property.value);
            const auto required = requiredVariableType(block.typeId);
            if (!ref || ref->id.empty()) {
                LogicDiagnostic diagnostic = makeError(
                    objectTypeId, board, "LB_VARIABLE_REFERENCE_EMPTY",
                    "Select a project variable.",
                    &rule, &block, property.key);
                if (draftSoft) diagnostic.severity = DiagnosticSeverity::Warning;
                pushSemantic(std::move(diagnostic));
            } else if (!project) {
                // Project context missing — defer existence checks.
            } else if (const GameVariableDefinition* def =
                           findGlobalVariable(*project, ref->id)) {
                if (required && def->type != *required) {
                    const char* need = (*required == GameVariableDefinition::Type::Boolean)
                        ? "Boolean"
                        : (*required == GameVariableDefinition::Type::String)
                            ? "String" : "Number";
                    pushSemantic(makeError(
                        objectTypeId, board, "LB_VARIABLE_TYPE_MISMATCH",
                        std::string("This Logic block requires a ") + need + " variable.",
                        &rule, &block, property.key));
                }
            } else {
                LogicDiagnostic diagnostic = makeError(
                    objectTypeId, board, "LB_VARIABLE_REFERENCE_MISSING",
                    "The referenced project variable does not exist.",
                    &rule, &block, property.key);
                if (draftSoft) diagnostic.severity = DiagnosticSeverity::Warning;
                pushSemantic(std::move(diagnostic));
            }
        }
        if ((block.typeId == kStateCompare || block.typeId == kStateCompareString)
            && property.key == "op") {
            const auto* op = std::get_if<LogicStringValue>(&property.value);
            const bool allowOrdering = block.typeId == kStateCompare;
            const bool ok = op
                && (op->value == "==" || op->value == "!="
                    || (allowOrdering
                        && (op->value == "<" || op->value == "<=" || op->value == ">"
                            || op->value == ">=")));
            if (!ok) {
                pushSemantic(makeError(objectTypeId, board, "LB_COMPARE_OP",
                                       allowOrdering
                                           ? "Compare operator must be == != < <= > >="
                                           : "Compare operator must be == or !=",
                                       &rule, &block, property.key));
            }
        }
        if (block.typeId == kTopDownMove && property.key == "direction") {
            const auto* direction = std::get_if<LogicStringValue>(&property.value);
            const bool ok = direction && (direction->value == "Left"
                || direction->value == "Right" || direction->value == "Up"
                || direction->value == "Down");
            if (!ok) {
                pushSemantic(makeError(objectTypeId, board, "LB_TOPDOWN_DIRECTION",
                                       "Top Down direction must be Left, Right, Up, or Down",
                                       &rule, &block, property.key));
            }
        }
        if (block.typeId == kSpriteSetFacing && property.key == "facing") {
            const auto* facing = std::get_if<LogicStringValue>(&property.value);
            const bool ok = facing
                && (facing->value == "Left" || facing->value == "Right");
            if (!ok) {
                pushSemantic(makeError(objectTypeId, board, "LB_SPRITE_FACING",
                                       "Sprite facing must be Left or Right",
                                       &rule, &block, property.key));
            }
        }
        if (block.typeId == kMoveHorizontal && property.key == "direction") {
            const auto* direction = std::get_if<LogicStringValue>(&property.value);
            const bool ok = direction
                && (direction->value == "Left" || direction->value == "Right");
            if (!ok) {
                pushSemantic(makeError(objectTypeId, board, "LB_PLATFORMER_DIRECTION",
                                       "Platformer move direction must be Left or Right",
                                       &rule, &block, property.key));
            }
        }
        if (block.typeId == kPlatformerMotionState && property.key == "state") {
            const auto* state = std::get_if<LogicStringValue>(&property.value);
            const bool ok = state
                && (state->value == "Stopped" || state->value == "Moving"
                    || state->value == "Jumping" || state->value == "Falling"
                    || state->value == "Climbing" || state->value == "WallSliding");
            if (!ok) {
                pushSemantic(makeError(objectTypeId, board, "LB_PLATFORMER_MOTION_STATE",
                                       "Platformer State must be Stopped, Moving, Jumping, "
                                       "Falling, Climbing, or WallSliding",
                                       &rule, &block, property.key));
            }
        }
    }
    if (structuralOnly) return;

    if (block.typeId == kAnimationPlayClip) {
        const LogicPropertyDef* assetProperty = findProperty(block, "animationAssetId");
        const LogicPropertyDef* clipProperty = findProperty(block, "clipId");
        const auto* assetRef = assetProperty
            ? std::get_if<LogicAssetReference>(&assetProperty->value) : nullptr;
        const auto* clipRef = clipProperty
            ? std::get_if<LogicStringValue>(&clipProperty->value) : nullptr;
        const SpriteAnimationAssetDef* asset = nullptr;
        const bool emptyAssetSelection = assetRef && assetRef->id.empty();
        if (!assetRef || emptyAssetSelection
            || (project && !(asset = findAnimationAsset(*project, assetRef->id)))) {
            LogicDiagnostic diagnostic = makeError(
                objectTypeId, board, "LB_ANIMATION_ASSET_REFERENCE",
                "Animation action must reference an existing animation asset",
                &rule, &block, "animationAssetId");
            if (draftSoft && emptyAssetSelection)
                diagnostic.severity = DiagnosticSeverity::Warning;
            pushSemantic(std::move(diagnostic));
        }
        const bool emptyClipSelection = clipRef && clipRef->value.empty();
        if (!clipRef || emptyClipSelection
            || (project && asset && !findAnimationClip(*asset, clipRef->value))) {
            LogicDiagnostic diagnostic = makeError(
                objectTypeId, board, "LB_ANIMATION_CLIP_REFERENCE",
                "Animation action clip must belong to its animation asset",
                &rule, &block, "clipId");
            if (draftSoft && emptyClipSelection)
                diagnostic.severity = DiagnosticSeverity::Warning;
            pushSemantic(std::move(diagnostic));
        }
    }
    if (block.typeId == kAudioPlaySound) {
        const LogicPropertyDef* assetProperty = findProperty(block, "audioAssetId");
        const auto* assetRef = assetProperty
            ? std::get_if<LogicAssetReference>(&assetProperty->value) : nullptr;
        const AudioAssetDef* asset = nullptr;
        const bool emptyAssetSelection = assetRef && assetRef->id.empty();
        if (!assetRef || emptyAssetSelection
            || (project && !(asset = findAudioAsset(*project, assetRef->id)))) {
            LogicDiagnostic diagnostic = makeError(
                objectTypeId, board, "LB_AUDIO_ASSET_REFERENCE",
                "Audio action must reference an existing audio asset",
                &rule, &block, "audioAssetId");
            if (draftSoft && emptyAssetSelection)
                diagnostic.severity = DiagnosticSeverity::Warning;
            pushSemantic(std::move(diagnostic));
        } else if (project && asset && asset->loadMode != AudioLoadMode::StaticSound) {
            pushSemantic(makeError(objectTypeId, board, "LB_AUDIO_REQUIRES_STATIC",
                                   "Play Sound requires a static audio asset",
                                   &rule, &block, "audioAssetId"));
        }
    }
    for (const LogicPropertyDescriptor& property : descriptor->properties) {
        if (!findProperty(block, property.key)) {
            // ADR-0012: legacy Move Horizontal boards store numeric `axis` only.
            if (block.typeId == kMoveHorizontal && property.key == "direction"
                && findProperty(block, "axis")) {
                continue;
            }
            pushSemantic(makeError(objectTypeId, board, "LB_MISSING_PROPERTY",
                                   "Missing property: " + property.key,
                                   &rule, &block, property.key));
        }
    }
}

void emitAction(std::ostringstream& lua, const LogicBlockDef& action,
                 std::set<std::string>& features,
                 const ObjectTypeId& objectTypeId, const LogicBoardDef& board,
                 const LogicRuleDef& rule, const LogicActionId& actionId,
                 std::vector<LogicDiagnostic>& codegenDiagnostics) {
    if (action.typeId == kSetVisible) {
        const LogicPropertyDef* p = findProperty(action, "visible");
        const bool value = std::get<bool>(p->value);
        lua << "      context.self:set_visible(" << (value ? "true" : "false") << ")\n";
    } else if (action.typeId == kSpriteSetFacing) {
        const LogicPropertyDef* p = findProperty(action, "facing");
        const auto* facing = p ? std::get_if<LogicStringValue>(&p->value) : nullptr;
        // Art faces Right by default; Left enables flipX.
        const bool flipX = facing && facing->value == "Left";
        lua << "      context.self:set_flip_x(" << (flipX ? "true" : "false") << ")\n";
    } else if (action.typeId == kSetPosition) {
        const LogicPropertyDef* p = findProperty(action, "position");
        const LogicVec2Value& value = std::get<LogicVec2Value>(p->value);
        emitGuardedVec2(lua, value,
                        board.id + ":" + rule.id + ":" + actionId
                            + ":position",
                        "context.self:set_position(_x, _y)",
                        objectTypeId, board, rule, action, "position", codegenDiagnostics);
    } else if (action.typeId == kTranslateBy) {
        const LogicPropertyDef* p = findProperty(action, "offset");
        const LogicVec2Value& value = std::get<LogicVec2Value>(p->value);
        emitGuardedVec2(lua, value,
                        board.id + ":" + rule.id + ":" + actionId
                            + ":offset",
                        "context.self:translate(_x, _y)",
                        objectTypeId, board, rule, action, "offset", codegenDiagnostics);
    } else if (action.typeId == kSetRotation) {
        const LogicPropertyDef* p = findProperty(action, "degrees");
        double degrees = 0.0;
        if (literalNumberForCodegen(p, 0.0, degrees, objectTypeId, board, rule, action,
                                    "degrees", codegenDiagnostics)) {
            // Authoring unit is degrees; runtime Transform stores radians.
            const double radians = degrees * 0.017453292519943295;
            lua << "      context.self:set_rotation(" << numberLiteralText(radians) << ")\n";
        }
    } else if (action.typeId == kRotateBy) {
        const LogicPropertyDef* p = findProperty(action, "degrees");
        double degrees = 0.0;
        if (literalNumberForCodegen(p, 0.0, degrees, objectTypeId, board, rule, action,
                                    "degrees", codegenDiagnostics)) {
            const double radians = degrees * 0.017453292519943295;
            lua << "      context.self:rotate_by(" << numberLiteralText(radians) << ")\n";
        }
    } else if (action.typeId == kSetScale) {
        const LogicPropertyDef* p = findProperty(action, "scale");
        const LogicVec2Value& value = std::get<LogicVec2Value>(p->value);
        emitGuardedVec2(lua, value,
                        board.id + ":" + rule.id + ":" + actionId
                            + ":scale",
                        "context.self:set_scale(_x, _y)",
                        objectTypeId, board, rule, action, "scale", codegenDiagnostics);
    } else if (action.typeId == kSetVelocity) {
        const LogicPropertyDef* p = findProperty(action, "velocity");
        const LogicVec2Value& value = std::get<LogicVec2Value>(p->value);
        emitGuardedVec2(lua, value,
                        board.id + ":" + rule.id + ":" + actionId
                            + ":velocity",
                        "context.self:set_velocity(_x, _y)",
                        objectTypeId, board, rule, action, "velocity", codegenDiagnostics);
    } else if (action.typeId == kSpawnObject) {
        const LogicPropertyDef* typeProp = findProperty(action, "objectTypeId");
        const LogicPropertyDef* posProp = findProperty(action, "position");
        const auto* type = typeProp ? std::get_if<LogicStringValue>(&typeProp->value) : nullptr;
        const Vec2 position = [&]() -> Vec2 {
            if (!posProp) return {};
            if (const auto* vec = std::get_if<LogicVec2Value>(&posProp->value)) {
                if (const std::optional<Vec2> lit = literalVec2Value(*vec)) return *lit;
            }
            return {};
        }();
        lua << "      context:spawn_object(\""
            << escapeLua(type ? type->value : std::string{}) << "\", "
            << numberLiteralText(position.x) << ", " << numberLiteralText(position.y) << ")\n";
    } else if (action.typeId == kMoveHorizontal) {
        if (const LogicPropertyDef* directionProp = findProperty(action, "direction")) {
            const auto* facing = std::get_if<LogicStringValue>(&directionProp->value);
            const bool left = facing && facing->value == "Left";
            lua << "      context.self:platformer_move(" << (left ? -1 : 1) << ")\n";
        } else {
            // Legacy boards authored numeric axis in [-1, 1].
            const LogicPropertyDef* p = findProperty(action, "axis");
            double axis = 0.0;
            if (literalNumberForCodegen(p, 0.0, axis, objectTypeId, board, rule, action,
                                        "axis", codegenDiagnostics)) {
                lua << "      context.self:platformer_move(" << numberLiteralText(axis) << ")\n";
            }
        }
    } else if (action.typeId == kTopDownMove) {
        const LogicPropertyDef* p = findProperty(action, "direction");
        const auto* direction = p ? std::get_if<LogicStringValue>(&p->value) : nullptr;
        const std::string value = direction ? direction->value : "Right";
        const Vec2 movement = value == "Left" ? Vec2{-1.f, 0.f}
                            : value == "Up" ? Vec2{0.f, -1.f}
                            : value == "Down" ? Vec2{0.f, 1.f}
                            : Vec2{1.f, 0.f};
        lua << "      context.self:topdown_move(" << numberLiteralText(movement.x) << ", "
            << numberLiteralText(movement.y) << ")\n";
    } else if (action.typeId == kJump) {
        lua << "      context.self:platformer_jump()\n";
    } else if (action.typeId == kWallJump) {
        double horizontal = 220.0;
        double vertical = 520.0;
        const LogicPropertyDef* hProp = findProperty(action, "horizontalSpeed");
        const LogicPropertyDef* vProp = findProperty(action, "verticalSpeed");
        const LogicPropertyDef* sideProp = findProperty(action, "side");
        const auto* side = sideProp ? std::get_if<LogicStringValue>(&sideProp->value) : nullptr;
        const std::string sideName = side && !side->value.empty() ? side->value : "Event";
        if (literalNumberForCodegen(hProp, 220.0, horizontal, objectTypeId, board, rule, action,
                                    "horizontalSpeed", codegenDiagnostics)
            && literalNumberForCodegen(vProp, 520.0, vertical, objectTypeId, board, rule, action,
                                       "verticalSpeed", codegenDiagnostics)) {
            lua << "      context.self:platformer_wall_jump(\"" << escapeLua(sideName) << "\", "
                << numberLiteralText(horizontal) << ", " << numberLiteralText(vertical) << ")\n";
        }
    } else if (action.typeId == kWallSlide) {
        double maxFall = 80.0;
        const LogicPropertyDef* speedProp = findProperty(action, "maxFallSpeed");
        const LogicPropertyDef* sideProp = findProperty(action, "side");
        const auto* side = sideProp ? std::get_if<LogicStringValue>(&sideProp->value) : nullptr;
        const std::string sideName = side && !side->value.empty() ? side->value : "Event";
        if (literalNumberForCodegen(speedProp, 80.0, maxFall, objectTypeId, board, rule, action,
                                    "maxFallSpeed", codegenDiagnostics)) {
            lua << "      context.self:platformer_wall_slide(\"" << escapeLua(sideName) << "\", "
                << numberLiteralText(maxFall) << ")\n";
        }
    } else if (action.typeId == kDestroySelf) {
        lua << "      context.self:destroy_self()\n";
    } else if (action.typeId == kDestroyOther) {
        // `other` is the collision callback's parameter; validation restricts
        // this action to triggers that provide EventOther, so it is always in
        // scope here (Wait continuations capture it as an upvalue).
        lua << "      context:destroy_other(other)\n";
    } else if (action.typeId == kAnimationPlayClip) {
        const LogicPropertyDef* asset = findProperty(action, "animationAssetId");
        const LogicPropertyDef* clip = findProperty(action, "clipId");
        const auto* assetRef = asset ? std::get_if<LogicAssetReference>(&asset->value) : nullptr;
        const auto* clipRef = clip ? std::get_if<LogicStringValue>(&clip->value) : nullptr;
        lua << "      context.self:play_animation_clip(\""
            << escapeLua(assetRef ? assetRef->id : std::string{}) << "\", \""
            << escapeLua(clipRef ? clipRef->value : std::string{}) << "\")\n";
    } else if (action.typeId == kAnimationStop) {
        lua << "      context.self:stop_animation()\n";
    } else if (action.typeId == kAnimationSetPlaybackSpeed) {
        const LogicPropertyDef* p = findProperty(action, "speed");
        double speed = 1.0;
        if (literalNumberForCodegen(p, 1.0, speed, objectTypeId, board, rule, action,
                                    "speed", codegenDiagnostics)) {
            lua << "      context.self:set_animation_playback_speed("
                << numberLiteralText(speed) << ")\n";
        }
    } else if (action.typeId == kAudioPlaySound) {
        const LogicPropertyDef* asset = findProperty(action, "audioAssetId");
        const LogicPropertyDef* volume = findProperty(action, "volume");
        const auto* assetRef = asset ? std::get_if<LogicAssetReference>(&asset->value) : nullptr;
        double volumeValue = 1.0;
        if (literalNumberForCodegen(volume, 1.0, volumeValue, objectTypeId, board, rule, action,
                                    "volume", codegenDiagnostics)) {
            lua << "      context:play_sound(\""
                << escapeLua(assetRef ? assetRef->id : std::string{}) << "\", "
                << numberLiteralText(volumeValue) << ")\n";
        }
    } else if (action.typeId == kSceneRestart) {
        lua << "      context:scene_restart()\n";
    } else if (action.typeId == kSceneGoTo) {
        const LogicPropertyDef* p = findProperty(action, "sceneId");
        const auto* scene = p ? std::get_if<LogicStringValue>(&p->value) : nullptr;
        lua << "      context:scene_go_to(\""
            << escapeLua(scene ? scene->value : std::string{}) << "\")\n";
    } else if (action.typeId == kCameraShake) {
        double intensity = 0.5;
        double duration = 0.5;
        const LogicPropertyDef* intensityProp = findProperty(action, "intensity");
        const LogicPropertyDef* durationProp = findProperty(action, "duration");
        const bool intensityOk = literalNumberForCodegen(
            intensityProp, 0.5, intensity, objectTypeId, board, rule, action,
            "intensity", codegenDiagnostics);
        const bool durationOk = literalNumberForCodegen(
            durationProp, 0.5, duration, objectTypeId, board, rule, action,
            "duration", codegenDiagnostics);
        if (intensityOk && durationOk) {
            lua << "      context:camera_shake(" << numberLiteralText(intensity) << ", "
                << numberLiteralText(duration) << ")\n";
        }
    } else if (action.typeId == kStateSet) {
        const LogicPropertyDef* keyProp = findProperty(action, "key");
        const LogicPropertyDef* valueProp = findProperty(action, "value");
        const auto* key = keyProp ? std::get_if<LogicVariableReference>(&keyProp->value) : nullptr;
        double value = 0.0;
        if (literalNumberForCodegen(valueProp, 0.0, value, objectTypeId, board, rule, action,
                                    "value", codegenDiagnostics)) {
            lua << "      context:state_set_number(\"" << escapeLua(key ? key->id : std::string{})
                << "\", " << numberLiteralText(value) << ")\n";
        }
    } else if (action.typeId == kStateAdd || action.typeId == kStateSubtract) {
        const LogicPropertyDef* keyProp = findProperty(action, "key");
        const LogicPropertyDef* amountProp = findProperty(action, "amount");
        const auto* key = keyProp ? std::get_if<LogicVariableReference>(&keyProp->value) : nullptr;
        double amount = 0.0;
        if (literalNumberForCodegen(amountProp, 0.0, amount, objectTypeId, board, rule, action,
                                    "amount", codegenDiagnostics)) {
            if (action.typeId == kStateSubtract) amount = -amount;
            lua << "      context:state_add_number(\"" << escapeLua(key ? key->id : std::string{})
                << "\", " << numberLiteralText(amount) << ")\n";
        }
    } else if (action.typeId == kStateToggle) {
        const LogicPropertyDef* keyProp = findProperty(action, "key");
        const auto* key = keyProp ? std::get_if<LogicVariableReference>(&keyProp->value) : nullptr;
        lua << "      context:state_toggle_boolean(\""
            << escapeLua(key ? key->id : std::string{}) << "\")\n";
    }
    if (const LogicBlockDescriptor* descriptor = findDescriptor(action.typeId))
        if (!descriptor->requiredFeature.empty()) features.insert(descriptor->requiredFeature);
}

void emitActions(std::ostringstream& lua, const std::vector<LogicActionDef>& actions,
                 std::size_t start, LogicTriggerActivationKind activationKind,
                 std::set<std::string>& features, const ObjectTypeId& objectTypeId,
                 const LogicBoardDef& board, const LogicRuleDef& rule,
                 std::vector<LogicDiagnostic>& codegenDiagnostics) {
    for (std::size_t i = start; i < actions.size(); ++i) {
        const LogicActionDef& action = actions[i];
        const std::string prefix = "_action_" + std::to_string(i + 1);
        const std::string stateKey = board.id + ":" + rule.id + ":" + action.id;
        if (action.executionMode == LogicExecutionMode::OncePerActivation)
            features.insert("logic.execution.once_per_activation");
        lua << "    local " << prefix << "_runs = context:should_execute(\""
            << escapeLua(stateKey) << "\", \""
            << logicExecutionModeToString(action.executionMode) << "\", \""
            << logicTriggerActivationKindToString(activationKind)
            << "\", _shared_eligible)\n";
        lua << "    if " << prefix << "_runs then\n";
        const std::size_t diagnosticsBefore = codegenDiagnostics.size();
        if (action.block.typeId == kWait) {
            const LogicPropertyDef* secondsProp = findProperty(action.block, "seconds");
            double seconds = 1.0;
            literalNumberForCodegen(secondsProp, 1.0, seconds, objectTypeId, board, rule,
                                    action.block,
                                    "seconds", codegenDiagnostics);
            if (const LogicBlockDescriptor* descriptor = findDescriptor(action.block.typeId))
                if (!descriptor->requiredFeature.empty())
                    features.insert(descriptor->requiredFeature);
            lua << "      context:wait(" << numberLiteralText(seconds) << ", function()\n";
            emitActions(lua, actions, i + 1, activationKind, features,
                        objectTypeId, board, rule,
                       codegenDiagnostics);
            lua << "      end)\n";
            lua << "    else\n";
            emitActions(lua, actions, i + 1, activationKind, features,
                        objectTypeId, board, rule, codegenDiagnostics);
            lua << "    end\n";
            for (std::size_t n = diagnosticsBefore;
                 n < codegenDiagnostics.size(); ++n) {
                if (codegenDiagnostics[n].actionId.empty())
                    codegenDiagnostics[n].actionId = action.id;
            }
            return;
        }
        emitAction(lua, action.block, features, objectTypeId, board, rule, action.id,
                   codegenDiagnostics);
        for (std::size_t n = diagnosticsBefore;
             n < codegenDiagnostics.size(); ++n) {
            if (codegenDiagnostics[n].actionId.empty())
                codegenDiagnostics[n].actionId = action.id;
        }
        lua << "    end\n";
    }
}

} // namespace

bool LogicCompileResult::ok() const {
    return std::none_of(diagnostics.begin(), diagnostics.end(),
        [](const LogicDiagnostic& d) { return d.severity == DiagnosticSeverity::Error; });
}

const LogicRequiredComponentDescriptor* requiredComponentDescriptor(LogicRequiredComponent component)
{
    const auto &value = requiredComponentRegistry();
    const auto it = std::find_if(value.begin(), value.end(),
                                 [component](const LogicRequiredComponentDescriptor &descriptor) {
                                     return descriptor.component == component;
                                 });
    return it == value.end() ? nullptr : &*it;
}

const LogicRequiredComponentDescriptor* requiredComponentDescriptor(const std::string& id)
{
    const auto &value = requiredComponentRegistry();
    const auto it = std::find_if(value.begin(), value.end(),
                                 [&id](const LogicRequiredComponentDescriptor &descriptor) {
                                     return descriptor.id == id;
                                 });
    return it == value.end() ? nullptr : &*it;
}

bool hasRequiredComponent(const EntityDef& owner, LogicRequiredComponent component)
{
    switch (component) {
    case LogicRequiredComponent::PlatformerController:
        return owner.platformerController.has_value();
    case LogicRequiredComponent::TopDownController:
        return owner.topDownController.has_value();
    case LogicRequiredComponent::SpriteAnimator:
        if (owner.spritePresentation) {
            return std::holds_alternative<SpritePresentationAnimation>(
                owner.spritePresentation->source);
        }
        return owner.spriteRenderer.has_value() && owner.spriteAnimator.has_value();
    }
    return false;
}

const std::vector<LogicBlockDescriptor>& registry() {
    static const std::vector<LogicBlockDescriptor> value = [] {
        std::vector<LogicBlockDescriptor> descriptors{
        {kOnStart, "system", "On Start", "Runs once when Play begins.",
            BlockKind::Trigger, {}, {}, {}, {LogicContextCapability::Self}, "event.start",
            false, 10, {"begin", "startup", "init"}},
        {kOnSceneStart, "scene", "On Scene Start",
            "Runs once after the active scene composition and scopes are prepared.",
            BlockKind::Trigger, {}, {}, {LogicContextCapability::Scene}, {},
            "scene.on_start", false, 5, {"scene", "level", "begin", "startup"}},
        {kOnDestroy, "lifecycle", "On Destroy",
            "Runs once when explicit gameplay destruction of Self is accepted, before teardown.",
            BlockKind::Trigger, {}, {}, {}, {LogicContextCapability::Self},
            "lifecycle.on_destroy", false, 10,
            {"death", "die", "despawn", "removed", "destroyed"}},
        {kEveryFrame, "system", "Every Frame", "Runs once every simulation frame.",
            BlockKind::Trigger, {}, {}, {}, {LogicContextCapability::Self, LogicContextCapability::DeltaTime},
            "event.on_update", true, 20, {"tick", "update", "frame"},
            LogicTriggerActivationKind::Level},
        {kEverySeconds, "system", "Every Second",
            "Runs on a repeating timer. Default interval is 1 second.",
            BlockKind::Trigger,
            {{"seconds", LogicValueKind::Number, NumberExpression::literal(1.0), "Seconds"}}, {}, {},
            {LogicContextCapability::Self}, "event.every_seconds", true, 30, {"timer", "interval"}},
        {kKeyPressed, "input", "Key Pressed", "Runs when the selected key is pressed.",
            BlockKind::Trigger,
            {{"key", LogicValueKind::Key, LogicKey::Space, "Key"}}, {}, {},
            {LogicContextCapability::Self}, "input.key_pressed", false, 10, {"keyboard", "input"},
            LogicTriggerActivationKind::Pulse, false, LogicPhaseRequirement::PreOnly},
        {kKeyReleased, "input", "Key Released", "Runs when the selected key is released.",
            BlockKind::Trigger,
            {{"key", LogicValueKind::Key, LogicKey::Space, "Key"}}, {}, {},
            {LogicContextCapability::Self}, "input.key_released", false, 20, {"keyboard", "input"},
            LogicTriggerActivationKind::Pulse, false, LogicPhaseRequirement::PreOnly},
        {kKeyHeld, "input", "While Key Held", "Runs once per tick while the selected key is held.",
            BlockKind::Trigger,
            {{"key", LogicValueKind::Key, LogicKey::Space, "Key"}}, {}, {},
            {LogicContextCapability::Self}, "input.key_held", true, 30, {"keyboard", "input", "hold"},
            LogicTriggerActivationKind::Level, false, LogicPhaseRequirement::PreOnly},
        {kKeyDown, "input", "Is Key Down", "True while the selected key is held.",
            BlockKind::Condition,
            {{"key", LogicValueKind::Key, LogicKey::Space, "Key"}}, {},
            {LogicContextCapability::Self}, {LogicContextCapability::Self},
            "input.key_down", false, 10, {"keyboard", "input"},
            LogicTriggerActivationKind::Level},
        {kSetVisible, "entity", "Set Visible", "Shows or hides Self.",
            BlockKind::Action,
            {{"target", LogicValueKind::Entity, selfReference(), "Target"},
             {"visible", LogicValueKind::Bool, true, "Visible"}},
            {}, {LogicContextCapability::Self}, {}, "entity.visibility", false, 10,
            {"visibility", "show", "hide"}},
        {kSpriteSetFacing, "entity", "Set Flip",
            "Mirrors Self's sprite horizontally for left/right facing. "
            "Art is assumed to face Right when not flipped.",
            BlockKind::Action,
            {{"facing", LogicValueKind::String, LogicStringValue{"Right"}, "Facing"}},
            {}, {LogicContextCapability::Self}, {}, "sprite.facing", false, 12,
            {"flip", "mirror", "facing", "left", "right", "flip horizontal"}},
        {kIsVisible, "entity", "Is Visible", "Checks whether Self is currently visible.",
            BlockKind::Condition,
            {{"expected", LogicValueKind::Bool, true, "Expected"}},
            {}, {LogicContextCapability::Self}, {LogicContextCapability::Self},
            "entity.visibility", false, 15, {"shown", "hidden", "visibility"},
            LogicTriggerActivationKind::Level},
        {kSetPosition, "entity", "Set Position", "Moves Self to an absolute world position.",
            BlockKind::Action,
            {{"target", LogicValueKind::Entity, selfReference(), "Target"},
             expressionVec2Property("position", LogicVec2Value{}, "Position",
                                    NumericExpressionPolicy::PerComponentNumberExpression)},
            {}, {LogicContextCapability::Self}, {}, "entity.transform", false, 20,
            {"teleport", "coords"}},
        {kTranslateBy, "entity", "Add Position", "Adds an offset to Self's current world position.",
            BlockKind::Action,
            {expressionVec2Property("offset", LogicVec2Value{}, "Offset",
                                    NumericExpressionPolicy::PerComponentNumberExpression)},
            {}, {LogicContextCapability::Self}, {}, "entity.transform", false, 30,
            {"translate", "offset", "nudge", "move by"}},
        {kSetRotation, "entity", "Set Rotation", "Sets Self's absolute rotation in degrees.",
            BlockKind::Action,
            {{"degrees", LogicValueKind::Number, NumberExpression::literal(0.0), "Degrees"}},
            {}, {LogicContextCapability::Self}, {}, "entity.transform", false, 40,
            {"angle", "orient"}},
        {kRotateBy, "entity", "Add Rotation", "Adds a relative rotation in degrees to Self.",
            BlockKind::Action,
            {{"degrees", LogicValueKind::Number, NumberExpression::literal(0.0), "Degrees"}},
            {}, {LogicContextCapability::Self}, {}, "entity.transform", false, 50,
            {"turn", "spin", "rotate by"}},
        {kSetScale, "entity", "Set Scale", "Sets Self's runtime scale X/Y (positive only).",
            BlockKind::Action,
            {{"scale", LogicValueKind::Vec2, LogicVec2Value::literal(1.0, 1.0), "Scale"}},
            {}, {LogicContextCapability::Self}, {}, "entity.transform", false, 60,
            {"size", "resize"}},
        {kSpawnObject, "entity", "Spawn Object", "Spawns an Object Type at a world position.",
            BlockKind::Action,
            {{"objectTypeId", LogicValueKind::String, LogicStringValue{}, "Object Type"},
             {"position", LogicValueKind::Vec2, LogicVec2Value{}, "Position"}},
            {}, {}, {}, "entity.spawn", false, 70,
            {"create", "instantiate"}},
        {kDestroySelf, "entity", "Destroy Self", "Removes Self from the runtime world after event dispatch.",
            BlockKind::Action, {}, {}, {LogicContextCapability::Self}, {}, "entity.destroy", false, 80,
            {"delete", "remove", "kill"}},
        {kSetVelocity, "physics", "Set Velocity", "Sets Self's linear velocity.",
            BlockKind::Action,
            {expressionVec2Property("velocity", LogicVec2Value{}, "Velocity",
                                    NumericExpressionPolicy::PerComponentNumberExpression)},
            {}, {LogicContextCapability::Self}, {}, "physics.set_velocity", false, 10,
            {"speed", "motion"}},
        {kIsGrounded, "platformer", "Is Grounded", "Checks whether Self is touching valid ground.",
            BlockKind::Condition,
            {{"expected", LogicValueKind::Bool, true, "Expected"}},
            {LogicRequiredComponent::PlatformerController}, {LogicContextCapability::Self},
            {LogicContextCapability::Self},
            "platformer.grounded", false, 10, {"floor", "landing"},
            LogicTriggerActivationKind::Level, false, LogicPhaseRequirement::PostOnly},
        {kIsFalling, "platformer", "Is Falling",
            "Deprecated: use Platformer State → Falling (ADR-0016). "
            "Checks whether Self is airborne and moving downward (+Y down).",
            BlockKind::Condition,
            {{"expected", LogicValueKind::Bool, true, "Expected"}},
            {LogicRequiredComponent::PlatformerController}, {LogicContextCapability::Self},
            {LogicContextCapability::Self},
            "platformer.falling", false, 15, {"airborne", "descent", "drop"},
            LogicTriggerActivationKind::Level, true, LogicPhaseRequirement::PostOnly},
        {kPlatformerMotionState, "platformer", "Platformer State",
            "Mutually exclusive locomotion of Self: Stopped/Moving grounded, "
            "Jumping/Falling airborne, Climbing on ladders, WallSliding while "
            "the wall-slide intent is active (ADR-0016 / ADR-0052).",
            BlockKind::Condition,
            {{"state", LogicValueKind::String, LogicStringValue{"Moving"}, "State"}},
            {LogicRequiredComponent::PlatformerController}, {LogicContextCapability::Self},
            {LogicContextCapability::Self},
            "platformer.motion_state", false, 18,
            {"moving", "stopped", "idle", "walking", "running", "jumping", "falling",
             "climbing", "wall sliding", "velocity", "motion", "state"},
            LogicTriggerActivationKind::Level, false, LogicPhaseRequirement::PostOnly},
        {kIsBlockedByWall, "platformer", "Is Blocked By Wall",
            "True when Platformer X resolution was blocked this fixed step "
            "(not a persistent wall probe).",
            BlockKind::Condition,
            {{"side", LogicValueKind::String, LogicStringValue{"Either"}, "Side",
              "", LogicPropertySemantic::Generic, LogicNumberConstraint::None, false,
              {"Left", "Right", "Either"}}},
            {LogicRequiredComponent::PlatformerController}, {LogicContextCapability::Self},
            {LogicContextCapability::Self},
            "platformer.contact_projection_v1", false, 19,
            {"wall", "blocked", "side"},
            LogicTriggerActivationKind::Level, false, LogicPhaseRequirement::PostOnly},
        {kOnLanded, "platformer", "On Landed",
            "Fires once when Self lands on valid support (validated floor + grounded).",
            BlockKind::Trigger, {},
            {LogicRequiredComponent::PlatformerController}, {},
            {LogicContextCapability::Self, LogicContextCapability::EventOther,
             LogicContextCapability::PlatformerLandingImpact},
            "platformer.contact_projection_v1", false, 40,
            {"land", "landing", "floor", "impact"},
            LogicTriggerActivationKind::Pulse, false, LogicPhaseRequirement::PostOnly},
        {kOnWallBlocked, "platformer", "On Wall Blocked",
            "Fires once when Platformer X is first blocked on a side while pushing.",
            BlockKind::Trigger,
            {{"side", LogicValueKind::String, LogicStringValue{"Either"}, "Side",
              "", LogicPropertySemantic::Generic, LogicNumberConstraint::None, false,
              {"Left", "Right", "Either"}}},
            {LogicRequiredComponent::PlatformerController}, {},
            {LogicContextCapability::Self, LogicContextCapability::EventOther,
             LogicContextCapability::PlatformerWallSide},
            "platformer.contact_projection_v1", false, 50,
            {"wall", "blocked", "contact"},
            LogicTriggerActivationKind::Pulse, false, LogicPhaseRequirement::PostOnly},
        {kOnCeilingHit, "platformer", "On Ceiling Hit",
            "Fires when Self hits a solid ceiling this fixed step.",
            BlockKind::Trigger, {},
            {LogicRequiredComponent::PlatformerController}, {},
            {LogicContextCapability::Self, LogicContextCapability::EventOther},
            "platformer.contact_projection_v1", false, 60,
            {"ceiling", "head", "bump"},
            LogicTriggerActivationKind::Pulse, false, LogicPhaseRequirement::PostOnly},
        {kMoveHorizontal, "platformer", "Platformer Move",
            "Requests horizontal platformer movement for Self for this input frame "
            "(Left or Right). Pair with While Key Held so movement stops on release.",
            BlockKind::Action,
            {{"direction", LogicValueKind::String, LogicStringValue{"Right"}, "Direction"}},
            {LogicRequiredComponent::PlatformerController}, {LogicContextCapability::Self}, {},
            "platformer.move", false, 20,
            {"walk", "run", "strafe", "left", "right", "move horizontal"},
            LogicTriggerActivationKind::Pulse, false, LogicPhaseRequirement::Flexible,
            LogicEffectTiming::NextSimulationStep},
        {kJump, "platformer", "Jump", "Requests a platformer jump.",
            BlockKind::Action, {}, {LogicRequiredComponent::PlatformerController},
            {LogicContextCapability::Self}, {}, "platformer.jump", false, 30, {"leap", "hop"},
            LogicTriggerActivationKind::Pulse, false, LogicPhaseRequirement::Flexible,
            LogicEffectTiming::NextSimulationStep},
        {kWallJump, "platformer", "Wall Jump",
            "Queues a wall jump for the next fixed step. Side is taken from the "
            "Wall Blocked event context (or Side property when used elsewhere).",
            BlockKind::Action,
            {{"horizontalSpeed", LogicValueKind::Number, NumberExpression::literal(220.0),
              "Horizontal Speed"},
             {"verticalSpeed", LogicValueKind::Number, NumberExpression::literal(520.0),
              "Vertical Speed"},
             {"side", LogicValueKind::String, LogicStringValue{"Event"}, "Side",
              "", LogicPropertySemantic::Generic, LogicNumberConstraint::None, false,
              {"Event", "Left", "Right"}}},
            {LogicRequiredComponent::PlatformerController}, {LogicContextCapability::Self}, {},
            "platformer.wall_intents_v1", false, 35,
            {"wall jump", "kick"},
            LogicTriggerActivationKind::Pulse, false, LogicPhaseRequirement::Flexible,
            LogicEffectTiming::NextSimulationStep},
        {kWallSlide, "platformer", "Wall Slide",
            "Queues a per-step fall-speed clamp while blocked against a wall "
            "(re-emit each Post tick to sustain).",
            BlockKind::Action,
            {{"maxFallSpeed", LogicValueKind::Number, NumberExpression::literal(80.0),
              "Max Fall Speed"},
             {"side", LogicValueKind::String, LogicStringValue{"Event"}, "Side",
              "", LogicPropertySemantic::Generic, LogicNumberConstraint::None, false,
              {"Event", "Left", "Right", "Either"}}},
            {LogicRequiredComponent::PlatformerController}, {LogicContextCapability::Self}, {},
            "platformer.wall_intents_v1", false, 36,
            {"wall slide", "cling"},
            LogicTriggerActivationKind::Pulse, false, LogicPhaseRequirement::Flexible,
            LogicEffectTiming::NextSimulationStep},
        {kTopDownMove, "topdown", "Top Down Movement",
            "Contributes a movement direction for Self during this input frame.",
            BlockKind::Action,
            {{"direction", LogicValueKind::String, LogicStringValue{"Right"}, "Direction"}},
            {LogicRequiredComponent::TopDownController}, {LogicContextCapability::Self}, {},
            "topdown.move", false, 10,
            {"move", "walk", "direction", "eight way", "top down move"}},
        {kCollisionEnter, "collision", "On Collision Enter", "Runs once when Self begins overlapping another collider.",
            BlockKind::Trigger,
            {{"objectTypeId", LogicValueKind::String, LogicStringValue{}, "Other Type"}}, {}, {},
            {LogicContextCapability::Self, LogicContextCapability::EventOther,
             LogicContextCapability::CollisionContact}, "collision.enter", false, 10,
            {"hit", "overlap", "touch"}},
        {kCollisionExit, "collision", "On Collision Exit", "Runs once when Self stops overlapping another collider.",
            BlockKind::Trigger,
            {{"objectTypeId", LogicValueKind::String, LogicStringValue{}, "Other Type"}}, {}, {},
            {LogicContextCapability::Self, LogicContextCapability::EventOther,
             LogicContextCapability::CollisionContact}, "collision.exit", false, 20,
            {"leave", "overlap"}},
        {kOtherIsObjectType, "collision", "Other Is Object Type",
            "Legacy condition filter. Prefer the Other Type property on collision events.",
            BlockKind::Condition,
            {{"objectTypeId", LogicValueKind::String, LogicStringValue{}, "Object Type"}},
            {}, {LogicContextCapability::EventOther}, {}, "collision.other_type", false, 30},
        {kDestroyOther, "collision", "Destroy Other",
            "Removes the collided entity (Other) from the runtime world after "
            "event dispatch. Requires a collision event.",
            BlockKind::Action, {}, {}, {LogicContextCapability::EventOther}, {},
            "collision.destroy_other", false, 40,
            {"delete", "remove", "kill", "collect", "pickup", "other"}},
        {kAnimationPlayClip, "animation", "Play Clip", "Plays an animation clip on Self.",
            BlockKind::Action,
            {{"animationAssetId", LogicValueKind::Asset, LogicAssetReference{}, "Animation"},
             {"clipId", LogicValueKind::String, LogicStringValue{}, "Clip"}},
            {LogicRequiredComponent::SpriteAnimator}, {LogicContextCapability::Self}, {},
            "animation.play_clip", false, 10, {"animate", "sprite", "clip"}},
        {kAnimationStop, "animation", "Stop Animation", "Stops Self's animation playback.",
            BlockKind::Action, {}, {LogicRequiredComponent::SpriteAnimator},
            {LogicContextCapability::Self}, {}, "animation.stop", false, 20},
        {kAnimationSetPlaybackSpeed, "animation", "Set Playback Speed",
            "Changes Self's runtime animation playback speed.",
            BlockKind::Action,
            {{"speed", LogicValueKind::Number, NumberExpression::literal(1.0), "Speed"}},
            {LogicRequiredComponent::SpriteAnimator}, {LogicContextCapability::Self}, {},
            "animation.set_playback_speed", false, 30},
        {kAnimationStarted, "animation", "Animation Started", "Runs when Self begins playing a clip.",
            BlockKind::Trigger, {}, {LogicRequiredComponent::SpriteAnimator}, {},
            {LogicContextCapability::Self}, "animation.on_started", false, 40},
        {kAnimationFinished, "animation", "Animation Finished",
            "Runs when Self finishes a non-looping clip.",
            BlockKind::Trigger, {}, {LogicRequiredComponent::SpriteAnimator}, {},
            {LogicContextCapability::Self}, "animation.on_finished", false, 50},
        {kAudioPlaySound, "audio", "Play Sound", "Plays a short audio asset.",
            BlockKind::Action,
            {{"audioAssetId", LogicValueKind::Asset, LogicAssetReference{}, "Sound"},
             {"volume", LogicValueKind::Number, NumberExpression::literal(1.0), "Volume"}},
            {}, {}, {}, "audio.play_sound", false, 10,
            {"sfx", "sound", "audio"}},
        {kSceneRestart, "scene", "Restart Scene",
            "Restores the current scene's authored layout and re-runs On Start. "
            "Applied at a safe point after this frame's events.",
            BlockKind::Action, {}, {}, {}, {}, "scene.restart", false, 10,
            {"reset", "retry", "reload", "restart"}},
        {kSceneGoTo, "scene", "Go To Scene",
            "Switches Play to another scene of the project. "
            "Applied at a safe point after this frame's events.",
            BlockKind::Action,
            {{"sceneId", LogicValueKind::String, LogicStringValue{}, "Scene"}},
            {}, {}, {}, "scene.go_to", false, 20,
            {"load", "change", "switch", "level", "next"}},
        {kOutsideSceneBounds, "scene", "Outside Scene",
            "True when Self's runtime position is beyond the active scene bounds plus Margin.",
            BlockKind::Condition,
            {{"margin", LogicValueKind::Number, NumberExpression::literal(0.0), "Margin"}},
            {}, {LogicContextCapability::Self}, {LogicContextCapability::Self},
            "scene.outside_bounds", true, 30,
            {"offscreen", "out of bounds", "boundary", "despawn"},
            LogicTriggerActivationKind::Level, false, LogicPhaseRequirement::PostOnly},
        {kCameraShake, "camera", "Camera Shake",
            "Adds camera trauma. Repeated calls stack intensity up to 1; "
            "the latest duration controls the current trauma decay.",
            BlockKind::Action,
            {{"intensity", LogicValueKind::Number, NumberExpression::literal(0.5), "Intensity"},
             {"duration", LogicValueKind::Number, NumberExpression::literal(0.5), "Duration"}},
            {}, {}, {}, "camera.shake", false, 10,
            {"shake", "trauma", "screen"}},
        {kWait, "flow", "Wait", "Waits, then continues with the following actions.",
            BlockKind::Action,
            {{"seconds", LogicValueKind::Number, NumberExpression::literal(1.0), "Seconds"}},
            {}, {}, {}, "flow.wait", true, 10, {"delay", "pause", "sleep"}},
        {kStateSet, "state", "Set Number", "Sets a project Number variable.",
            BlockKind::Action,
            {{"key", LogicValueKind::Variable, LogicVariableReference{}, "Variable"},
             {"value", LogicValueKind::Number, NumberExpression::literal(0.0), "Value"}},
            {}, {}, {}, "state.set_number", false, 10, {"variable", "score", "set"}},
        {kStateAdd, "state", "Add to Number", "Adds to a project Number variable.",
            BlockKind::Action,
            {{"key", LogicValueKind::Variable, LogicVariableReference{}, "Variable"},
             {"amount", LogicValueKind::Number, NumberExpression::literal(1.0), "Amount"}},
            {}, {}, {}, "state.add_number", false, 20, {"variable", "increment", "add"}},
        {kStateSubtract, "state", "Subtract from Number",
            "Subtracts from a project Number variable.",
            BlockKind::Action,
            {{"key", LogicValueKind::Variable, LogicVariableReference{}, "Variable"},
             {"amount", LogicValueKind::Number, NumberExpression::literal(1.0), "Amount"}},
            {}, {}, {}, "state.add_number", false, 30, {"variable", "decrement", "subtract"}},
        {kStateCompare, "state", "Compare Number", "Compares a project Number variable.",
            BlockKind::Condition,
            {{"key", LogicValueKind::Variable, LogicVariableReference{}, "Variable"},
             {"op", LogicValueKind::String, LogicStringValue{"=="}, "Operator"},
             {"value", LogicValueKind::Number, NumberExpression::literal(0.0), "Value"}},
            {}, {}, {}, "state.compare_number", false, 40,
            {"variable", "equals", "compare"},
            LogicTriggerActivationKind::Level},
        {kStateCompareBoolean, "state", "Compare Boolean",
            "Compares a project Boolean variable.",
            BlockKind::Condition,
            // No operator: `expected` also covers the Event-slot case, where
            // there is no clause to negate.
            {{"key", LogicValueKind::Variable, LogicVariableReference{}, "Variable"},
             {"expected", LogicValueKind::Bool, true, "Expected"}},
            {}, {}, {}, "state.compare_boolean", false, 42,
            {"variable", "boolean", "equals", "compare"},
            LogicTriggerActivationKind::Level},
        {kStateCompareString, "state", "Compare String",
            "Compares a project String variable. Exact, case-sensitive match.",
            BlockKind::Condition,
            {{"key", LogicValueKind::Variable, LogicVariableReference{}, "Variable"},
             {"op", LogicValueKind::String, LogicStringValue{"=="}, "Operator"},
             {"value", LogicValueKind::String, LogicStringValue{}, "Value"}},
            {}, {}, {}, "state.compare_string", false, 44,
            {"variable", "string", "text", "equals", "compare"},
            LogicTriggerActivationKind::Level},
        {kStateToggle, "state", "Toggle Boolean", "Toggles a project Boolean variable.",
            BlockKind::Action,
            {{"key", LogicValueKind::Variable, LogicVariableReference{}, "Variable"}},
            {}, {}, {}, "state.toggle_boolean", false, 50, {"variable", "bool", "toggle"}},
        };
        for (LogicBlockDescriptor& block : descriptors) {
            for (LogicPropertyDescriptor& property : block.properties) {
                if (property.valueKind == LogicValueKind::Key) {
                    property.semantic = LogicPropertySemantic::LogicKey;
                } else if (property.valueKind == LogicValueKind::Variable) {
                    property.semantic = LogicPropertySemantic::GlobalVariable;
                } else if (property.valueKind == LogicValueKind::Entity) {
                    property.semantic = LogicPropertySemantic::HiddenSelfTarget;
                } else if (property.key == "expected") {
                    property.semantic = LogicPropertySemantic::ExpectedBool;
                } else if (property.key == "objectTypeId") {
                    property.semantic = LogicPropertySemantic::ObjectTypeReference;
                    property.allowEmpty =
                        block.typeId == kCollisionEnter || block.typeId == kCollisionExit;
                } else if (property.key == "sceneId") {
                    property.semantic = LogicPropertySemantic::SceneReference;
                } else if (property.key == "animationAssetId") {
                    property.semantic = LogicPropertySemantic::SpriteAnimationAsset;
                } else if (property.key == "clipId") {
                    property.semantic = LogicPropertySemantic::AnimationClip;
                } else if (property.key == "audioAssetId") {
                    property.semantic = LogicPropertySemantic::StaticAudioAsset;
                } else if (block.typeId == kStateCompare && property.key == "op") {
                    property.semantic = LogicPropertySemantic::CompareOperator;
                    property.options = {"==", "!=", "<", "<=", ">", ">="};
                } else if (block.typeId == kStateCompareString && property.key == "op") {
                    property.semantic = LogicPropertySemantic::CompareOperator;
                    property.options = {"==", "!="};
                } else if (block.typeId == kTopDownMove && property.key == "direction") {
                    property.semantic = LogicPropertySemantic::TopDownDirection;
                    property.options = {"Left", "Right", "Up", "Down"};
                } else if (block.typeId == kSpriteSetFacing && property.key == "facing") {
                    property.semantic = LogicPropertySemantic::SpriteFacing;
                    property.options = {"Left", "Right"};
                } else if (block.typeId == kMoveHorizontal && property.key == "direction") {
                    property.semantic = LogicPropertySemantic::PlatformerDirection;
                    property.options = {"Left", "Right"};
                } else if (block.typeId == kPlatformerMotionState && property.key == "state") {
                    property.semantic = LogicPropertySemantic::PlatformerMotionState;
                    property.options = {"Stopped", "Moving", "Jumping", "Falling",
                                        "Climbing", "WallSliding"};
                }
                if (block.typeId == kStateCompareString && property.key == "value") {
                    property.allowEmpty = true;
                }

                if (property.valueKind == LogicValueKind::Number) {
                    property.numberConstraint = LogicNumberConstraint::Finite;
                    if (property.key == "seconds" || property.key == "speed")
                        property.numberConstraint = LogicNumberConstraint::Positive;
                    if (property.key == "volume")
                        property.numberConstraint = LogicNumberConstraint::UnitInterval;
                    if (property.key == "axis")
                        property.numberConstraint = LogicNumberConstraint::NormalizedAxis;
                    if (block.typeId == kCameraShake) {
                        if (property.key == "intensity") {
                            property.numberConstraint =
                                LogicNumberConstraint::UnitInterval;
                        } else if (property.key == "duration") {
                            property.numberConstraint =
                                LogicNumberConstraint::Positive;
                        }
                    }
                } else if (property.valueKind == LogicValueKind::Vec2
                           && property.key == "scale") {
                    property.numberConstraint = LogicNumberConstraint::PositiveVec2;
                }
            }
        }
        return descriptors;
    }();
    return value;
}

std::string propertyDisplayName(const LogicPropertyDescriptor& property)
{
    return property.displayName.empty() ? property.key : property.displayName;
}

const LogicBlockDescriptor* findDescriptor(const std::string& typeId) {
    const auto& all = registry();
    const auto it = std::find_if(all.begin(), all.end(),
        [&](const LogicBlockDescriptor& d) { return d.typeId == typeId; });
    return it == all.end() ? nullptr : &*it;
}

const LogicPropertyDef* findProperty(const LogicBlockDef& block, const std::string& key) {
    const auto it = std::find_if(block.properties.begin(), block.properties.end(),
        [&](const LogicPropertyDef& p) { return p.key == key; });
    return it == block.properties.end() ? nullptr : &*it;
}

LogicBlockDef makeDefaultBlock(const LogicBlockTypeId& typeId, BlockKind expected) {
    const LogicBlockDescriptor* descriptor = findDescriptor(typeId);
    if (!descriptor || descriptor->kind != expected) return {};
    LogicBlockDef block;
    block.typeId = descriptor->typeId;
    for (const LogicPropertyDescriptor& property : descriptor->properties)
        block.properties.push_back({property.key, property.defaultValue});
    return block;
}

LogicPropertyDescriptor expressionVec2Property(
    std::string key,
    LogicVec2Value defaultValue,
    std::string displayName,
    NumericExpressionPolicy policy) {
    LogicPropertyDescriptor property;
    property.key = std::move(key);
    property.valueKind = LogicValueKind::Vec2;
    property.defaultValue = std::move(defaultValue);
    property.displayName = std::move(displayName);
    property.numericExpressionPolicy = policy;
    return property;
}

bool isEventEligible(const LogicBlockDescriptor& descriptor) {
    if (descriptor.kind == BlockKind::Trigger) return true;
    if (descriptor.kind != BlockKind::Condition) return false;
    return !containsCapability(descriptor.requiredContext, LogicContextCapability::EventOther);
}

LogicBlockDef makeDefaultEventBlock(const LogicBlockTypeId& typeId) {
    const LogicBlockDescriptor* descriptor = findDescriptor(typeId);
    if (!descriptor || !isEventEligible(*descriptor)) return {};
    LogicBlockDef block;
    block.typeId = descriptor->typeId;
    for (const LogicPropertyDescriptor& property : descriptor->properties)
        block.properties.push_back({property.key, property.defaultValue});
    return block;
}

std::optional<GameVariableDefinition::Type> requiredVariableType(
    const LogicBlockTypeId& typeId) {
    if (typeId == kStateSet || typeId == kStateAdd || typeId == kStateSubtract
        || typeId == kStateCompare) {
        return GameVariableDefinition::Type::Number;
    }
    if (typeId == kStateToggle || typeId == kStateCompareBoolean)
        return GameVariableDefinition::Type::Boolean;
    if (typeId == kStateCompareString) return GameVariableDefinition::Type::String;
    return std::nullopt;
}

const GameVariableDefinition* findGlobalVariable(
    const ProjectDoc& project, const GameVariableId& id) {
    if (id.empty()) return nullptr;
    for (const GameVariableDefinition& def : project.globalVariables) {
        if (def.key == id) return &def;
    }
    return nullptr;
}

void applyDeterministicVariableDefault(const ProjectDoc& doc, LogicBlockDef& block) {
    const auto required = requiredVariableType(block.typeId);
    if (!required) return;
    LogicPropertyDef* keyProp = nullptr;
    for (LogicPropertyDef& property : block.properties) {
        if (property.key == "key") {
            keyProp = &property;
            break;
        }
    }
    if (!keyProp) return;
    auto* ref = std::get_if<LogicVariableReference>(&keyProp->value);
    if (!ref || !ref->id.empty()) return;

    std::vector<GameVariableId> matching;
    matching.reserve(doc.globalVariables.size());
    for (const GameVariableDefinition& def : doc.globalVariables) {
        if (def.type == *required && !def.key.empty()) matching.push_back(def.key);
    }
    std::sort(matching.begin(), matching.end());
    if (!matching.empty()) ref->id = matching.front();
}

LogicBlockAvailability blockAvailability(const EntityDef& owner,
                                         const LogicBlockDescriptor& candidate,
                                         const LogicBlockDescriptor* trigger) {
    return availabilityFor(owner, candidate, trigger);
}

LogicBlockAvailability sceneBlockAvailability(const LogicBlockDescriptor& candidate,
                                              const LogicBlockDescriptor* trigger) {
    if (candidate.typeId == kOnStart || candidate.typeId == kOnDestroy) {
        return {false, "Requires an entity lifecycle"};
    }
    if (!candidate.requiredComponents.empty()) {
        return {false, "Requires an entity component"};
    }
    for (const LogicContextCapability capability : candidate.requiredContext) {
        if (capability == LogicContextCapability::Self
            || capability == LogicContextCapability::EventOther
            || capability == LogicContextCapability::CollisionContact
            || capability == LogicContextCapability::PlatformerWallSide
            || capability == LogicContextCapability::PlatformerLandingImpact) {
            return {false, "Requires an entity Self or collision event"};
        }
        if (capability == LogicContextCapability::Scene) {
            if (candidate.typeId != kOnSceneStart || trigger != nullptr)
                return {false, "Requires a Scene lifecycle event"};
        } else if (!trigger || !containsCapability(trigger->providedContext, capability)) {
            return {false, "Requires a trigger that provides the required context"};
        }
    }
    if (candidate.typeId == kOnSceneStart && candidate.kind != BlockKind::Trigger) {
        return {false, "On Scene Start is available only as a Scene Logic event"};
    }
    return {};
}

LogicBlockDef makeDefaultTrigger() { return makeDefaultBlock(kOnStart, BlockKind::Trigger); }

LogicActionDef makeDefaultAction(LogicActionId id) {
    return LogicActionDef{
        std::move(id),
        LogicExecutionMode::EveryOccurrence,
        makeDefaultBlock(kSetVisible, BlockKind::Action),
    };
}

LogicBlockDef makeDefaultCondition() { return makeDefaultBlock(kIsGrounded, BlockKind::Condition); }

LogicRuleDef makeDefaultRule(LogicRuleId id) {
    LogicRuleDef rule;
    rule.id = std::move(id);
    rule.name = rule.id;
    rule.trigger = makeDefaultTrigger();
    rule.actions.push_back(makeDefaultAction());
    return rule;
}

const char* logicExecutionModeToString(LogicExecutionMode mode) {
    switch (mode) {
    case LogicExecutionMode::EveryOccurrence: return "every_occurrence";
    case LogicExecutionMode::OncePerActivation: return "once_per_activation";
    }
    return "every_occurrence";
}

std::optional<LogicExecutionMode> logicExecutionModeFromString(const std::string& value) {
    if (value == "every_occurrence") return LogicExecutionMode::EveryOccurrence;
    if (value == "once_per_activation") return LogicExecutionMode::OncePerActivation;
    return std::nullopt;
}

const char* logicTriggerActivationKindToString(LogicTriggerActivationKind kind) {
    switch (kind) {
    case LogicTriggerActivationKind::Pulse: return "pulse";
    case LogicTriggerActivationKind::Level: return "level";
    }
    return "pulse";
}

std::optional<LogicTriggerActivationKind> logicTriggerActivationKindFromString(
    const std::string& value) {
    if (value == "pulse") return LogicTriggerActivationKind::Pulse;
    if (value == "level") return LogicTriggerActivationKind::Level;
    return std::nullopt;
}

std::vector<LogicKey> supportedLogicKeys() {
    std::vector<LogicKey> keys;
    for (int i = static_cast<int>(LogicKey::A); i <= static_cast<int>(LogicKey::Enter); ++i)
        keys.push_back(static_cast<LogicKey>(i));
    return keys;
}

std::string logicKeyName(LogicKey key) {
    const int v = static_cast<int>(key);
    if (v >= static_cast<int>(LogicKey::A) && v <= static_cast<int>(LogicKey::Z))
        return std::string(1, static_cast<char>('A' + v));
    if (v >= static_cast<int>(LogicKey::Num0) && v <= static_cast<int>(LogicKey::Num9))
        return std::string(1, static_cast<char>('0' + v - static_cast<int>(LogicKey::Num0)));
    switch (key) {
        case LogicKey::ArrowLeft: return "ArrowLeft";
        case LogicKey::ArrowRight: return "ArrowRight";
        case LogicKey::ArrowUp: return "ArrowUp";
        case LogicKey::ArrowDown: return "ArrowDown";
        case LogicKey::Space: return "Space";
        case LogicKey::Enter: return "Enter";
        default: return {};
    }
}

std::optional<LogicKey> logicKeyFromName(const std::string& name) {
    for (LogicKey key : supportedLogicKeys())
        if (logicKeyName(key) == name) return key;
    return std::nullopt;
}

std::string logicInputCode(LogicKey key) {
    const int value = static_cast<int>(key);
    if (value >= static_cast<int>(LogicKey::A) && value <= static_cast<int>(LogicKey::Z))
        return "Key" + logicKeyName(key);
    if (value >= static_cast<int>(LogicKey::Num0) && value <= static_cast<int>(LogicKey::Num9))
        return "Digit" + logicKeyName(key);
    return logicKeyName(key);
}

std::vector<LogicDiagnostic> validateBoard(const ObjectTypeId& objectTypeId,
                                           const LogicBoardDef& board,
                                           const EntityDef* owner,
                                           const ProjectDoc* project,
                                           LogicValidationPurpose purpose) {
    std::vector<LogicDiagnostic> out;
    if (!validId(board.id)) out.push_back(makeError(objectTypeId, board, "LB_BOARD_ID", "Invalid board id"));
    if (board.schemaVersion != kLogicBoardSchemaVersion)
        out.push_back(makeError(objectTypeId, board, "LB_SCHEMA_VERSION", "Unsupported Logic Board schema version"));
    if (board.apiVersion != kLogicApiVersion)
        out.push_back(makeError(objectTypeId, board, "LB_API_VERSION", "Unsupported Logic API version"));
    if (board.rules.size() > kMaxRulesPerBoard)
        out.push_back(makeError(objectTypeId, board, "LB_RULE_LIMIT", "Logic Board exceeds the rule limit"));

    std::unordered_set<std::string> ids;
    for (const LogicRuleDef& rule : board.rules) {
        if (!validId(rule.id) || !ids.insert(rule.id).second)
            out.push_back(makeError(objectTypeId, board, "LB_RULE_ID", "Invalid or duplicate rule id", &rule));
        if (rule.name.empty())
            out.push_back(makeError(objectTypeId, board, "LB_RULE_NAME", "Logic rule name is required", &rule));
        if (rule.actions.size() > kMaxActionsPerRule)
            out.push_back(makeError(objectTypeId, board, "LB_ACTION_LIMIT",
                                    "Rule exceeds the action limit", &rule));
        if (rule.actions.empty() && purpose == LogicValidationPurpose::Executable
            && rule.enabled) {
            out.push_back(makeError(objectTypeId, board, "LB_NO_ACTIONS",
                                    "Enabled rule has no actions", &rule));
        }
        std::unordered_set<std::string> actionIds;
        const LogicBlockDescriptor* trigger = findDescriptor(rule.trigger.typeId);
        validateBlock(objectTypeId, board, rule, rule.trigger, BlockKind::Trigger, owner,
                      nullptr, project, purpose, out);
        for (std::size_t index = 0; index < rule.conditions.size(); ++index) {
            const LogicConditionClause& clause = rule.conditions[index];
            if (index == 0 && clause.joinBefore != LogicConditionJoin::And) {
                out.push_back(makeError(objectTypeId, board, "LB_FIRST_CONDITION_JOIN",
                                        "First condition must use AND.", &rule,
                                        &clause.block, {}));
            }
            switch (clause.joinBefore) {
            case LogicConditionJoin::And:
            case LogicConditionJoin::Or:
                break;
            default:
                out.push_back(makeError(objectTypeId, board, "LB_UNKNOWN_CONDITION_JOIN",
                                        "Unknown condition join operator.", &rule,
                                        &clause.block, {}));
                break;
            }
            validateBlock(objectTypeId, board, rule, clause.block, BlockKind::Condition, owner,
                          trigger, project, purpose, out);
        }
        for (const LogicActionDef& action : rule.actions) {
            if (!validId(action.id) || !actionIds.insert(action.id).second) {
                LogicDiagnostic diagnostic = makeError(
                    objectTypeId, board, "LB_ACTION_ID",
                    "Invalid or duplicate Logic action id", &rule);
                diagnostic.actionId = action.id;
                out.push_back(std::move(diagnostic));
            }
            const std::size_t before = out.size();
            validateBlock(objectTypeId, board, rule, action.block, BlockKind::Action,
                          owner, trigger, project, purpose, out);
            for (std::size_t n = before; n < out.size(); ++n)
                out[n].actionId = action.id;
        }
        if (rule.conditions.size() > kMaxConditionsPerRule)
            out.push_back(makeError(objectTypeId, board, "LB_CONDITION_LIMIT",
                                    "Rule exceeds the condition limit", &rule));
    }
    return out;
}

std::vector<LogicDiagnostic> validateSceneBoard(const SceneId& sceneId,
                                                const LogicBoardDef& board,
                                                const ProjectDoc* project,
                                                LogicValidationPurpose purpose) {
    std::vector<LogicDiagnostic> out = validateBoard({}, board, nullptr, project, purpose);
    const auto reject = [&](const LogicRuleDef& rule, const LogicBlockDef& block,
                            LogicActionId actionId = {}) {
        const LogicBlockDescriptor* descriptor = findDescriptor(block.typeId);
        if (!descriptor || purpose == LogicValidationPurpose::StructuralCommit) return;
        const bool sceneStart = block.typeId == kOnSceneStart;
        const bool entityOnly = block.typeId == kOnStart || block.typeId == kOnDestroy
            || std::find(descriptor->requiredContext.begin(), descriptor->requiredContext.end(),
                         LogicContextCapability::Self) != descriptor->requiredContext.end()
            || !descriptor->requiredComponents.empty();
        if (!sceneStart && entityOnly) {
            LogicDiagnostic d = makeError({}, board, "LB_SCENE_INCOMPATIBLE_BLOCK",
                "This block requires an entity Self and cannot be used by Scene Logic",
                &rule, &block);
            d.actionId = std::move(actionId);
            out.push_back(std::move(d));
        }
        if (sceneStart && (&block != &rule.trigger || rule.trigger.typeId != kOnSceneStart)) {
            LogicDiagnostic d = makeError({}, board, "LB_SCENE_START_ONLY_TRIGGER",
                "On Scene Start is available only as a Scene Logic event", &rule, &block);
            d.actionId = std::move(actionId);
            out.push_back(std::move(d));
        }
    };
    for (const LogicRuleDef& rule : board.rules) {
        reject(rule, rule.trigger);
        for (const LogicConditionClause& clause : rule.conditions) reject(rule, clause.block);
        for (const LogicActionDef& action : rule.actions) reject(rule, action.block, action.id);
        const bool unconditionalRestart = rule.enabled
            && rule.trigger.typeId == kOnSceneStart && rule.conditions.empty()
            && std::any_of(rule.actions.begin(), rule.actions.end(),
                [](const LogicActionDef& action) {
                    return action.block.typeId == kSceneRestart;
                });
        if (unconditionalRestart && purpose != LogicValidationPurpose::StructuralCommit) {
            LogicDiagnostic diagnostic = makeError(
                {}, board, "LB_SCENE_RESTART_ON_START_LOOP",
                "Restart Scene from an unconditional On Scene Start rule repeats forever.",
                &rule, &rule.trigger);
            diagnostic.severity = DiagnosticSeverity::Warning;
            out.push_back(std::move(diagnostic));
        }
    }
    (void)sceneId;
    return out;
}

bool hasLogicErrors(const std::vector<LogicDiagnostic>& diagnostics) {
    return std::any_of(diagnostics.begin(), diagnostics.end(),
        [](const LogicDiagnostic& diagnostic) {
            return diagnostic.severity == DiagnosticSeverity::Error;
        });
}

std::string firstLogicErrorMessage(const std::vector<LogicDiagnostic>& diagnostics) {
    for (const LogicDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity != DiagnosticSeverity::Error) continue;
        return diagnostic.code + ": " + diagnostic.message;
    }
    return {};
}

namespace {
bool isLevelConditionTrigger(const LogicBlockTypeId& typeId) {
    const LogicBlockDescriptor* descriptor = findDescriptor(typeId);
    return descriptor && descriptor->kind == BlockKind::Condition
        && descriptor->activationKind == LogicTriggerActivationKind::Level;
}

LogicPhaseRequirement phaseRequirementOf(const LogicBlockTypeId& typeId) {
    const LogicBlockDescriptor* descriptor = findDescriptor(typeId);
    return descriptor ? descriptor->phaseRequirement : LogicPhaseRequirement::Flexible;
}

bool resolveRuleEvaluationPhase(const LogicRuleDef& rule,
                                LogicEvaluationPhase& outPhase,
                                std::string& conflictMessage) {
    LogicPhaseRequirement triggerReq = phaseRequirementOf(rule.trigger.typeId);
    // Every Frame is Flexible: PostOnly conditions pull the rule to Post.
    if (rule.trigger.typeId == kEveryFrame)
        triggerReq = LogicPhaseRequirement::Flexible;

    const LogicBlockDescriptor* triggerDesc = findDescriptor(rule.trigger.typeId);
    // Pulse + PreOnly (Key Pressed/Released) fires at input time and may read
    // the last completed projection. PostOnly conditions must not relocate it
    // (unlike Level PreOnly Key Held, which cannot mix with PostOnly).
    const bool pulsePreTrigger = triggerReq == LogicPhaseRequirement::PreOnly
        && triggerDesc
        && triggerDesc->activationKind == LogicTriggerActivationKind::Pulse;

    bool wantsPost = triggerReq == LogicPhaseRequirement::PostOnly;
    bool wantsPre = triggerReq == LogicPhaseRequirement::PreOnly;
    for (const LogicConditionClause& clause : rule.conditions) {
        const LogicPhaseRequirement req = phaseRequirementOf(clause.block.typeId);
        if (req == LogicPhaseRequirement::PostOnly) {
            if (!pulsePreTrigger) wantsPost = true;
        }
        if (req == LogicPhaseRequirement::PreOnly) wantsPre = true;
    }
    // Level-condition-as-event (Platformer State / Is Grounded as trigger).
    if (isLevelConditionTrigger(rule.trigger.typeId)
        && phaseRequirementOf(rule.trigger.typeId) == LogicPhaseRequirement::PostOnly) {
        wantsPost = true;
    }
    if (wantsPre && wantsPost) {
        conflictMessage =
            "Rule mixes PreOnly and PostOnly evaluation requirements";
        return false;
    }
    outPhase = wantsPost ? LogicEvaluationPhase::PostSimulation
                         : LogicEvaluationPhase::PreSimulation;
    return true;
}
} // namespace

static LogicCompileResult compileBoardForOwner(const std::string& ownerId,
                                               const LogicBoardDef& board,
                                               const EntityDef* owner,
                                               const ProjectDoc* project,
                                               LogicBoardOwnerKind ownerKind) {
    LogicCompileResult result;
    result.diagnostics = ownerKind == LogicBoardOwnerKind::Scene
        ? validateSceneBoard(ownerId, board, project, LogicValidationPurpose::Executable)
        : validateBoard(ownerId, board, owner, project, LogicValidationPurpose::Executable);
    if (!result.ok()) return result;

    std::set<std::string> features;
    std::vector<LogicDiagnostic> codegenDiagnostics;
    std::ostringstream lua;
    lua << "logic.require_api_version(" << kLogicApiVersion << ")\n";
    lua << (ownerKind == LogicBoardOwnerKind::Scene ? "logic.define_scene_board(\""
                                                    : "logic.define_board(\"")
        << escapeLua(board.id) << "\", \"" << escapeLua(ownerId) << "\", function(context)\n";
    for (const LogicRuleDef& rule : board.rules) {
        if (!rule.enabled) continue;
        const LogicBlockDescriptor* triggerDescriptor = findDescriptor(rule.trigger.typeId);
        const LogicTriggerActivationKind activationKind = triggerDescriptor
            ? triggerDescriptor->activationKind
            : LogicTriggerActivationKind::Pulse;
        const bool hasOncePerActivation = std::any_of(
            rule.actions.begin(), rule.actions.end(), [](const LogicActionDef& action) {
                return action.executionMode == LogicExecutionMode::OncePerActivation;
            });
        // Level + OncePerActivation must observe false→true and true→false, so
        // Key Held is compiled as on_update + is_key_down (not on_key_held).
        const bool levelGateViaUpdate = hasOncePerActivation
            && activationKind == LogicTriggerActivationKind::Level
            && rule.trigger.typeId == kKeyHeld;

        LogicEvaluationPhase evaluationPhase = LogicEvaluationPhase::PreSimulation;
        std::string phaseConflict;
        if (!resolveRuleEvaluationPhase(rule, evaluationPhase, phaseConflict)) {
            result.diagnostics.push_back(makeError(
                ownerId, board, "LB_INCOMPATIBLE_EVALUATION_PHASE",
                phaseConflict, &rule, &rule.trigger, {}));
            return result;
        }
        if (evaluationPhase == LogicEvaluationPhase::PostSimulation)
            features.insert("logic.post_simulation_v1");

        if (rule.trigger.typeId == kOnStart) {
            lua << "  context:on_start(\"" << escapeLua(rule.id) << "\", function()\n";
        } else if (rule.trigger.typeId == kOnSceneStart) {
            lua << "  context:on_scene_start(\"" << escapeLua(rule.id) << "\", function()\n";
        } else if (rule.trigger.typeId == kOnDestroy) {
            lua << "  context:on_destroy(\"" << escapeLua(rule.id) << "\", function()\n";
        } else if (rule.trigger.typeId == kOnLanded) {
            lua << "  context:on_platformer_landed(\"" << escapeLua(rule.id)
                << "\", function(other)\n";
            features.insert("platformer.contact_projection_v1");
        } else if (rule.trigger.typeId == kOnWallBlocked) {
            lua << "  context:on_platformer_wall_contact(\"" << escapeLua(rule.id)
                << "\", function(other)\n";
            features.insert("platformer.contact_projection_v1");
        } else if (rule.trigger.typeId == kOnCeilingHit) {
            lua << "  context:on_platformer_ceiling_hit(\"" << escapeLua(rule.id)
                << "\", function(other)\n";
            features.insert("platformer.contact_projection_v1");
        } else if (rule.trigger.typeId == kEveryFrame || levelGateViaUpdate) {
            if (evaluationPhase == LogicEvaluationPhase::PostSimulation)
                lua << "  context:on_post_simulation(\"" << escapeLua(rule.id)
                    << "\", function()\n";
            else
                lua << "  context:on_update(\"" << escapeLua(rule.id) << "\", function()\n";
            result.requiresTick = true;
        } else if (rule.trigger.typeId == kEverySeconds) {
            const LogicPropertyDef* seconds = findProperty(rule.trigger, "seconds");
            double interval = 1.0;
            literalNumberForCodegen(seconds, 1.0, interval, ownerId, board, rule,
                                    rule.trigger, "seconds", codegenDiagnostics);
            lua << "  context:on_every_seconds(\"" << escapeLua(rule.id) << "\", "
                << numberLiteralText(interval) << ", function()\n";
        } else if (rule.trigger.typeId == kAnimationStarted) {
            lua << "  context:on_animation_started(\"" << escapeLua(rule.id)
                << "\", function()\n";
        } else if (rule.trigger.typeId == kAnimationFinished) {
            lua << "  context:on_animation_finished(\"" << escapeLua(rule.id)
                << "\", function()\n";
        } else if (rule.trigger.typeId == kCollisionEnter || rule.trigger.typeId == kCollisionExit) {
            const char* registerMethod = rule.trigger.typeId == kCollisionEnter
                ? "on_collision_enter" : "on_collision_exit";
            lua << "  context:" << registerMethod << "(\"" << escapeLua(rule.id)
                << "\", function(other)\n";
        } else if (rule.trigger.typeId == kKeyPressed || rule.trigger.typeId == kKeyReleased
                   || rule.trigger.typeId == kKeyHeld) {
            const LogicPropertyDef* key = findProperty(rule.trigger, "key");
            const char* registerMethod = rule.trigger.typeId == kKeyReleased ? "on_key_released"
                : rule.trigger.typeId == kKeyHeld ? "on_key_held" : "on_key_pressed";
            lua << "  context:" << registerMethod << "(\"" << escapeLua(rule.id) << "\", \""
                << logicKeyName(std::get<LogicKey>(key->value)) << "\", function()\n";
        } else if (isLevelConditionTrigger(rule.trigger.typeId)) {
            if (evaluationPhase == LogicEvaluationPhase::PostSimulation)
                lua << "  context:on_post_simulation(\"" << escapeLua(rule.id)
                    << "\", function()\n";
            else
                lua << "  context:on_update(\"" << escapeLua(rule.id) << "\", function()\n";
            result.requiresTick = true;
        } else {
            result.diagnostics.push_back(makeError(
                ownerId, board, "LB_UNKNOWN_TRIGGER",
                "Unsupported Logic Board trigger: " + rule.trigger.typeId, &rule,
                &rule.trigger, {}));
            return result;
        }
        if (triggerDescriptor) {
            if (!triggerDescriptor->requiredFeature.empty())
                features.insert(triggerDescriptor->requiredFeature);
            result.requiresTick = result.requiresTick || triggerDescriptor->requiresTick;
        }

        // Shared conditions are evaluated exactly once per dispatch. Each
        // action then applies its own stable-identity gate in authored order.
        std::string sharedEligible = "true";
        if (levelGateViaUpdate) {
            const LogicPropertyDef* key = findProperty(rule.trigger, "key");
            sharedEligible = "context:is_key_down(\""
                + logicKeyName(std::get<LogicKey>(key->value)) + "\")";
            features.insert("input.key_down");
        } else if (rule.trigger.typeId == kCollisionEnter || rule.trigger.typeId == kCollisionExit) {
            const LogicPropertyDef* filter = findProperty(rule.trigger, "objectTypeId");
            const auto* type = filter ? std::get_if<LogicStringValue>(&filter->value) : nullptr;
            if (type && !type->value.empty()) {
                sharedEligible = "context:other_is_object_type(other, \""
                    + escapeLua(type->value) + "\")";
                features.insert("collision.other_type");
            }
        } else if (rule.trigger.typeId == kOnWallBlocked) {
            const LogicPropertyDef* sideProp = findProperty(rule.trigger, "side");
            const auto* side = sideProp ? std::get_if<LogicStringValue>(&sideProp->value) : nullptr;
            const std::string sideName = side && !side->value.empty() ? side->value : "Either";
            if (sideName == "Left" || sideName == "Right") {
                sharedEligible = "(context:contact_side() == \"" + escapeLua(sideName) + "\")";
            }
        } else if (isLevelConditionTrigger(rule.trigger.typeId)) {
            sharedEligible = emitConditionExpression(rule.trigger, features);
        }
        const std::string sharedConditions = emitConditionsExpression(rule.conditions, features);
        if (!sharedConditions.empty())
            sharedEligible = "(" + sharedEligible + ") and (" + sharedConditions + ")";
        lua << "    local _shared_eligible = " << sharedEligible << "\n";
        emitActions(lua, rule.actions, 0, activationKind, features, ownerId,
                    board, rule, codegenDiagnostics);
        if (features.count("flow.wait")) result.requiresTick = true;
        lua << "  end)\n";
    }
    lua << "end)\n";

    // ADR-0038 Finding 3: a property that escaped policy enforcement above and
    // could not be translated fails the compile here instead of publishing a
    // program built on a silently substituted default.
    if (!codegenDiagnostics.empty()) {
        result.diagnostics.insert(result.diagnostics.end(),
            codegenDiagnostics.begin(), codegenDiagnostics.end());
        return result;
    }

    LogicProgram program;
    program.ownerKind = ownerKind;
    if (ownerKind == LogicBoardOwnerKind::Scene) program.sceneId = ownerId;
    else program.objectTypeId = ownerId;
    program.boardId = board.id;
    program.source = lua.str();
    program.requiresTick = result.requiresTick;
    program.requiredFeatures.assign(features.begin(), features.end());
    result.programs.push_back(std::move(program));
    return result;
}

LogicCompileResult compileBoard(const ObjectTypeId& objectTypeId,
                                const LogicBoardDef& board,
                                const EntityDef* owner,
                                const ProjectDoc* project) {
    return compileBoardForOwner(objectTypeId, board, owner, project,
                                LogicBoardOwnerKind::ObjectType);
}

LogicCompileResult compileSceneBoard(const SceneId& sceneId, const LogicBoardDef& board,
                                     const ProjectDoc* project) {
    return compileBoardForOwner(sceneId, board, nullptr, project, LogicBoardOwnerKind::Scene);
}

LogicCompileResult compileProjectLogic(const ProjectDoc& project) {
    LogicCompileResult result;
    std::vector<ObjectTypeId> ids;
    ids.reserve(project.objectTypes.size());
    for (const auto& [id, unused] : project.objectTypes) {
        (void)unused;
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    std::size_t blocks = 0;
    for (const ObjectTypeId& id : ids) {
        const EntityDef& type = project.objectTypes.at(id);
        if (!type.logicBoard) continue;
        for (const LogicRuleDef& rule : type.logicBoard->rules) {
            blocks += 1 + rule.conditions.size() + rule.actions.size();
        }
        LogicCompileResult one = compileBoard(id, *type.logicBoard, &type, &project);
        result.programs.insert(result.programs.end(),
            std::make_move_iterator(one.programs.begin()), std::make_move_iterator(one.programs.end()));
        result.diagnostics.insert(result.diagnostics.end(),
            std::make_move_iterator(one.diagnostics.begin()), std::make_move_iterator(one.diagnostics.end()));
        result.requiresTick = result.requiresTick || one.requiresTick;
    }
    std::vector<SceneId> sceneIds;
    sceneIds.reserve(project.scenes.size());
    for (const auto& [id, scene] : project.scenes) {
        if (scene.logicBoard) sceneIds.push_back(id);
    }
    std::sort(sceneIds.begin(), sceneIds.end());
    for (const SceneId& id : sceneIds) {
        const SceneDef& scene = project.scenes.at(id);
        for (const LogicRuleDef& rule : scene.logicBoard->rules)
            blocks += 1 + rule.conditions.size() + rule.actions.size();
        LogicCompileResult one = compileSceneBoard(id, *scene.logicBoard, &project);
        result.programs.insert(result.programs.end(),
            std::make_move_iterator(one.programs.begin()), std::make_move_iterator(one.programs.end()));
        result.diagnostics.insert(result.diagnostics.end(),
            std::make_move_iterator(one.diagnostics.begin()), std::make_move_iterator(one.diagnostics.end()));
        result.requiresTick = result.requiresTick || one.requiresTick;
    }
    if (blocks > kMaxBlocksPerProject) {
        LogicDiagnostic d;
        d.code = "LB_PROJECT_BLOCK_LIMIT";
        d.message = "Project exceeds the Logic Board block limit";
        result.diagnostics.push_back(std::move(d));
        result.programs.clear();
    }
    return result;
}

} // namespace ArtCade::Logic
