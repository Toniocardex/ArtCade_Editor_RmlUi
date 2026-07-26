#pragma once

#include "../../../core/types.h"

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace ArtCade::Logic {

inline constexpr uint32_t kLogicBoardSchemaVersion = 4;
inline constexpr uint32_t kLogicApiVersion = 2;
inline constexpr std::size_t kMaxRulesPerBoard = 128;
inline constexpr std::size_t kMaxSectionsPerBoard = 64;
inline constexpr std::size_t kMaxConditionsPerRule = 16;
inline constexpr std::size_t kMaxActionsPerRule = 16;
inline constexpr std::size_t kMaxBlocksPerProject = 8192;
inline constexpr std::size_t kMaxLogicIdLength = 128;

inline constexpr const char* kOnStart = "event.on_start";
inline constexpr const char* kEveryFrame = "event.on_update";
inline constexpr const char* kEverySeconds = "event.every_seconds";
inline constexpr const char* kKeyPressed = "input.key_pressed";
inline constexpr const char* kKeyReleased = "input.key_released";
inline constexpr const char* kKeyHeld = "input.key_held";
inline constexpr const char* kKeyDown = "input.key_down";
inline constexpr const char* kSetVisible = "entity.set_visible";
inline constexpr const char* kSpriteSetFacing = "sprite.set_facing";
inline constexpr const char* kIsVisible = "entity.is_visible";
inline constexpr const char* kSetPosition = "entity.set_position";
inline constexpr const char* kTranslateBy = "entity.translate_by";
inline constexpr const char* kSetRotation = "entity.set_rotation";
inline constexpr const char* kRotateBy = "entity.rotate_by";
inline constexpr const char* kSetScale = "entity.set_scale";
inline constexpr const char* kSetVelocity = "physics.set_velocity";
inline constexpr const char* kSpawnObject = "entity.spawn";
inline constexpr const char* kIsGrounded = "platformer.is_grounded";
inline constexpr const char* kIsFalling = "platformer.is_falling";
inline constexpr const char* kPlatformerMotionState = "platformer.motion_state";
inline constexpr const char* kMoveHorizontal = "platformer.move_horizontal";
inline constexpr const char* kJump = "platformer.jump";
inline constexpr const char* kTopDownMove = "topdown.move";
inline constexpr const char* kCollisionEnter = "collision.enter";
inline constexpr const char* kCollisionExit = "collision.exit";
inline constexpr const char* kOtherIsObjectType = "collision.other_is_object_type";
inline constexpr const char* kDestroyOther = "collision.destroy_other";
inline constexpr const char* kDestroySelf = "entity.destroy_self";
inline constexpr const char* kAnimationPlayClip = "animation.play_clip";
inline constexpr const char* kAnimationStop = "animation.stop";
inline constexpr const char* kAnimationSetPlaybackSpeed = "animation.set_playback_speed";
inline constexpr const char* kAnimationStarted = "animation.on_started";
inline constexpr const char* kAnimationFinished = "animation.on_finished";
inline constexpr const char* kAudioPlaySound = "audio.play_sound";
inline constexpr const char* kSceneRestart = "scene.restart";
inline constexpr const char* kSceneGoTo = "scene.go_to";
inline constexpr const char* kWait = "flow.wait";
inline constexpr const char* kStateSet = "state.set";
inline constexpr const char* kStateAdd = "state.add";
inline constexpr const char* kStateSubtract = "state.subtract";
inline constexpr const char* kStateCompare = "state.compare";
inline constexpr const char* kStateToggle = "state.toggle";

using LogicBlockTypeId = std::string;
using LogicCategoryId = std::string;

enum class BlockKind { Trigger, Condition, Action };
/**
 * Single Logic Board validator, purpose-selected (ADR-0013).
 * StructuralCommit — Command / Save / Load gate (corrupt-data only).
 * AuthoringDiagnostics — panel / Problems (visible, never a persist veto).
 * Executable — compileBoard, Play, Build, Export, Generated Lua.
 */
enum class LogicValidationPurpose {
    StructuralCommit,
    AuthoringDiagnostics,
    Executable,
};
enum class LogicValueKind { Bool, Integer, Number, String, Vec2, Asset, Entity, Variable, Key };
// Semantic authoring hints consumed by editor projections. They describe the
// domain meaning of a value without depending on any UI toolkit.
enum class LogicPropertySemantic {
    Generic,
    LogicKey,
    ExpectedBool,
    ObjectTypeReference,
    SceneReference,
    SpriteAnimationAsset,
    AnimationClip,
    StaticAudioAsset,
    GlobalVariable,
    CompareOperator,
    TopDownDirection,
    SpriteFacing,
    PlatformerDirection,
    PlatformerMotionState,
    HiddenSelfTarget,
};
enum class LogicNumberConstraint {
    None,
    Finite,
    Positive,
    UnitInterval,
    NormalizedAxis,
    PositiveVec2,
};
enum class NumericExpressionPolicy {
    LiteralOnly,
    PerComponentNumberExpression,
};
enum class LogicRequiredComponent { PlatformerController, TopDownController, SpriteAnimator };
enum class LogicContextCapability {
    Self,
    EventOther,
    DeltaTime,
    CollisionContact,
    MessagePayload,
};
/**
 * Trigger activation semantics from the registry (not duplicated in UI/runtime
 * switches). Pulse = discrete one-shot event; Level = continuous state.
 */
enum class LogicTriggerActivationKind { Pulse, Level };

struct LogicPropertyDescriptor {
    std::string    key;
    LogicValueKind valueKind = LogicValueKind::Bool;
    LogicValue     defaultValue = false;
    /** User-facing label; empty means fall back to @p key. */
    std::string    displayName;
    std::string    description;
    LogicPropertySemantic semantic = LogicPropertySemantic::Generic;
    LogicNumberConstraint numberConstraint = LogicNumberConstraint::None;
    bool allowEmpty = false;
    /** Stable string choices for small enum-like properties. */
    std::vector<std::string> options;
    /** ADR-0028: whether Vec2 components may hold dynamic NumberExpressions. */
    NumericExpressionPolicy numericExpressionPolicy =
        NumericExpressionPolicy::LiteralOnly;
};

struct LogicBlockDescriptor {
    LogicBlockTypeId                     typeId;
    LogicCategoryId                      categoryId;
    std::string                          displayName;
    std::string                          description;
    BlockKind                            kind = BlockKind::Trigger;
    std::vector<LogicPropertyDescriptor> properties;
    std::vector<LogicRequiredComponent>  requiredComponents;
    std::vector<LogicContextCapability>  requiredContext;
    std::vector<LogicContextCapability>  providedContext;
    std::string                          requiredFeature;
    bool                                 requiresTick = false;
    /** Stable sort key within a category (lower first). */
    int                                  catalogOrder = 0;
    /** Extra case-insensitive search terms for the Logic Catalog (registry-owned). */
    std::vector<std::string>             searchSynonyms;
    /** Pulse (default) or Level — drives OncePerActivation rising-edge gating. */
    LogicTriggerActivationKind           activationKind = LogicTriggerActivationKind::Pulse;
    /** When true, omit from Logic Board add-Event / add-Condition pickers. */
    bool                                 catalogHidden = false;
};

/**
 * Canonical authoring metadata for a component required by a Logic block.
 * @p id is the stable bridge/command value; @p displayName is user-facing.
 */
struct LogicRequiredComponentDescriptor {
    LogicRequiredComponent component = LogicRequiredComponent::PlatformerController;
    std::string            id;
    std::string            displayName;
};

struct LogicBlockAvailability {
    bool compatible = true;
    std::string reason;
};

enum class DiagnosticSeverity { Warning, Error };

struct LogicDiagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string code;
    std::string message;
    ObjectTypeId objectTypeId;
    LogicBoardId boardId;
    LogicRuleId ruleId;
    std::string blockTypeId;
    std::string propertyKey;
};

struct LogicProgram {
    ObjectTypeId objectTypeId;
    LogicBoardId boardId;
    std::string  source;
    bool         requiresTick = false;
    std::vector<std::string> requiredFeatures;
};

struct LogicCompileResult {
    std::vector<LogicProgram>    programs;
    std::vector<LogicDiagnostic> diagnostics;
    bool                         requiresTick = false;

    bool ok() const;
};

struct LogicJsonResult {
    bool ok = false;
    std::string error;
};

const std::vector<LogicBlockDescriptor>& registry();
const LogicBlockDescriptor* findDescriptor(const std::string& typeId);
/** Returns canonical metadata for @p component, or nullptr when unsupported. */
const LogicRequiredComponentDescriptor* requiredComponentDescriptor(LogicRequiredComponent component);
/** Returns canonical metadata for stable authoring @p id, or nullptr when unknown. */
const LogicRequiredComponentDescriptor* requiredComponentDescriptor(const std::string& id);
/** Returns whether @p owner satisfies the complete requirement for @p component. */
bool hasRequiredComponent(const EntityDef& owner, LogicRequiredComponent component);
const LogicPropertyDef* findProperty(const LogicBlockDef& block, const std::string& key);
/** User-facing property label; falls back to @p key when displayName is empty. */
[[nodiscard]] std::string propertyDisplayName(const LogicPropertyDescriptor& property);
LogicBlockDef makeDefaultBlock(const LogicBlockTypeId& typeId, BlockKind expected);
/**
 * Vec2 catalog property with an explicit NumericExpressionPolicy (ADR-0028).
 * Used for Set Position position; other Vec2 properties keep LiteralOnly default.
 */
[[nodiscard]] LogicPropertyDescriptor expressionVec2Property(
    std::string key,
    LogicVec2Value defaultValue,
    std::string displayName,
    NumericExpressionPolicy policy);
/**
 * True when @p descriptor may occupy the rule Event/trigger slot.
 * Triggers always qualify. Conditions qualify only when they do not require
 * EventOther (those stay collision-filter properties or legacy condition clauses).
 */
[[nodiscard]] bool isEventEligible(const LogicBlockDescriptor& descriptor);
/** Default block for the Event slot — Trigger or event-eligible Condition. */
[[nodiscard]] LogicBlockDef makeDefaultEventBlock(const LogicBlockTypeId& typeId);
/**
 * Required global variable type for a state block typeId, or nullopt if the
 * block does not reference a typed project variable.
 */
[[nodiscard]] std::optional<GameVariableDefinition::Type> requiredVariableType(
    const LogicBlockTypeId& typeId);
/**
 * Finds a project global by exact case-sensitive key.
 */
[[nodiscard]] const GameVariableDefinition* findGlobalVariable(
    const ProjectDoc& project, const GameVariableId& id);
/**
 * After makeDefaultBlock, fill empty Variable refs from the first compatible
 * global sorted by key. Pure registry remains project-agnostic.
 */
void applyDeterministicVariableDefault(const ProjectDoc& doc, LogicBlockDef& block);
LogicBlockAvailability blockAvailability(const EntityDef& owner,
                                         const LogicBlockDescriptor& candidate,
                                         const LogicBlockDescriptor* trigger = nullptr);

LogicBlockDef makeDefaultTrigger();
LogicBlockDef makeDefaultAction();
LogicBlockDef makeDefaultCondition();
LogicRuleDef makeDefaultRule(LogicRuleId id);

/** Persistable token for LogicExecutionMode ("every_occurrence", …). */
[[nodiscard]] const char* logicExecutionModeToString(LogicExecutionMode mode);
[[nodiscard]] std::optional<LogicExecutionMode> logicExecutionModeFromString(
    const std::string& value);
/** Persistable token for LogicTriggerActivationKind ("pulse", "level"). */
[[nodiscard]] const char* logicTriggerActivationKindToString(
    LogicTriggerActivationKind kind);
[[nodiscard]] std::optional<LogicTriggerActivationKind> logicTriggerActivationKindFromString(
    const std::string& value);

std::string logicKeyName(LogicKey key);
std::optional<LogicKey> logicKeyFromName(const std::string& name);
std::vector<LogicKey> supportedLogicKeys();
/**
 * Maps a LogicKey to the Input module key code ("KeyA", "Digit0", "Space", …).
 * Single source for app loop dispatch and Logic host isKeyDown.
 */
std::string logicInputCode(LogicKey key);

std::vector<LogicDiagnostic> validateBoard(
    const ObjectTypeId& objectTypeId,
    const LogicBoardDef& board,
    const EntityDef* owner = nullptr,
    const ProjectDoc* project = nullptr,
    LogicValidationPurpose purpose = LogicValidationPurpose::Executable);
/** True when any diagnostic has Error severity. */
[[nodiscard]] bool hasLogicErrors(const std::vector<LogicDiagnostic>& diagnostics);
/** First Error as "CODE: message", or empty when none. */
[[nodiscard]] std::string firstLogicErrorMessage(
    const std::vector<LogicDiagnostic>& diagnostics);
LogicCompileResult compileBoard(const ObjectTypeId& objectTypeId,
                                const LogicBoardDef& board,
                                const EntityDef* owner = nullptr,
                                const ProjectDoc* project = nullptr);
LogicCompileResult compileProjectLogic(const ProjectDoc& project);

nlohmann::json logicBoardToJson(const LogicBoardDef& board);
LogicJsonResult logicBoardFromJson(const nlohmann::json& json, LogicBoardDef& out);

} // namespace ArtCade::Logic
