#pragma once

#include "artcade/sfx/types.hpp"

#include "project-defaults.h"
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <variant>
#include <unordered_map>

namespace ArtCade {

// ============================================================================
// Primitive identifiers
// ============================================================================

using EntityId  = uint32_t;
using SceneId   = std::string;
using AssetId   = std::string;
using ObjectTypeId = std::string;
using ScriptAttachmentId = std::string;
using AnimationAssetId = std::string;
using AnimationClipId = std::string;
using SpriteFrameId = std::string;

/**
 * Stable identity of a project/global game variable.
 * MVP contract: identical to GameVariableDefinition::key (case-sensitive).
 * LogicVariableReference::id stores this value. Keys are immutable — no rename
 * in MVP; future migration may introduce a separate display name.
 */
using GameVariableId = std::string;

constexpr EntityId INVALID_ENTITY = 0;

using GameVariableValue = std::variant<double, bool, std::string>;

/**
 * Authoring definition of a project/global variable.
 * `key` is the stable identity (GameVariableId) — not a display label.
 * Comparisons and Logic Board refs are case-sensitive exact match on `key`.
 */
struct GameVariableDefinition {
    enum class Type { Number, Boolean, String };
    GameVariableId    key;   // Immutable identity, not a display label.
    Type              type = Type::Number;
    GameVariableValue initialValue = 0.0;
    std::string       description;
};

// ============================================================================
// Math (wraps glm — include glm before this header)
// ============================================================================

struct Vec2 { float x = 0.f, y = 0.f; };
struct Vec3 { float x = 1.f, y = 1.f, z = 1.f; };
struct Vec4 { float r = 1.f, g = 1.f, b = 1.f, a = 1.f; };

// ============================================================================
// Logic Board authoring model
// ============================================================================

using LogicBoardId = std::string;
using LogicRuleId  = std::string;

enum class LogicKey {
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    ArrowLeft, ArrowRight, ArrowUp, ArrowDown,
    Space, Enter,
};

struct LogicStringValue { std::string value; };
struct LogicAssetReference { AssetId id; };
/** Logic Board reference to a game variable — `id` holds GameVariableId (key). */
struct LogicVariableReference { GameVariableId id; };

struct LogicEntityReference {
    enum class Kind { Self };
    Kind kind = Kind::Self;
};

using LogicValue = std::variant<
    bool,
    int64_t,
    double,
    LogicStringValue,
    Vec2,
    LogicAssetReference,
    LogicEntityReference,
    LogicVariableReference,
    LogicKey>;

struct LogicPropertyDef {
    std::string key;
    LogicValue  value = false;
};

struct LogicBlockDef {
    std::string                   typeId;
    std::vector<LogicPropertyDef> properties;
};

/** Connector applied before a Logic condition clause. */
enum class LogicConditionJoin { And, Or };

/** Boolean condition payload plus its authored connector and optional negation. */
struct LogicConditionClause {
    LogicConditionJoin joinBefore = LogicConditionJoin::And;
    bool               negated = false;
    LogicBlockDef      block;
};

// Display-only grouping header for board readability. Sections never affect
// runtime semantics: rule execution order stays the board rule order.
struct LogicSectionDef {
    std::string id;    // stable id ("section-1"); name is a display label only
    std::string name;
};

/** How often a Logic rule may run its Action group after WHEN becomes true. */
enum class LogicExecutionMode {
    EveryOccurrence,   // default: run every time WHEN evaluates true
    OncePerActivation, // rising-edge gate on the complete WHEN expression
};

struct LogicRuleDef {
    LogicRuleId               id;
    std::string               name;       // authoring display label; never affects runtime logic
    bool                      enabled = true;
    LogicExecutionMode        executionMode = LogicExecutionMode::EveryOccurrence;
    std::string               sectionId;  // optional LogicSectionDef.id; empty = unsectioned
    LogicBlockDef             trigger;
    std::vector<LogicConditionClause> conditions;
    std::vector<LogicBlockDef> actions;
};

struct LogicBoardDef {
    LogicBoardId              id;
    uint32_t                  schemaVersion = 3;
    uint32_t                  apiVersion = 2;
    std::vector<LogicSectionDef> sections;  // display grouping; optional
    std::vector<LogicRuleDef> rules;
};

// ============================================================================
// Transform
// ============================================================================

struct Transform {
    Vec2  position;
    Vec2  scale   = {1.f, 1.f};
    float rotation = 0.f;     // radians
    Vec2  velocity;
};

// ============================================================================
// Physics
// ============================================================================

enum class BodyType    { Dynamic, Static, Kinematic };
enum class ColliderShape { Rectangle, Circle, Capsule, Polygon };
enum class CollisionShapeType { Rectangle, Circle, Capsule, Polygon };
enum class CollisionResponse { Solid, Sensor };
enum class CollisionShapeRole { Body, Feet, Hurtbox, Hitbox, Interaction };

struct Collider {
    ColliderShape shape   = ColliderShape::Rectangle;
    Vec2          size    = {1.f, 1.f};  // w/h for rect, radius in x for circle
    Vec2          offset;
    float         density  = 1.f;
    float         friction = 0.3f;
};

struct PhysicsComponent {
    BodyType bodyType     = BodyType::Dynamic;
    Collider collider;
    // Runtime compatibility mirror. Source of truth lives in RuntimeEntityGateway.
    uint32_t physicsHandle = 0;
};

struct PhysicsLayerDef {
    std::string id = "default";
    std::string name = "Default";
    uint32_t    bit = 0;
    Vec4        color = {0.55f, 0.60f, 0.70f, 1.f};
};

struct CollisionShape {
    CollisionShapeType type = CollisionShapeType::Rectangle;
    CollisionResponse  response = CollisionResponse::Solid;
    CollisionShapeRole role = CollisionShapeRole::Body;
    std::string        layerId = "default";
    std::vector<std::string> maskLayerIds = { "default" };
    Vec2               offset{};
    Vec2               size{ 32.f, 32.f };
    float              radius = 16.f;
    std::vector<Vec2>  points;
    bool               enabled = true;
    bool               oneWay = false;
    float              friction = 0.3f;
    float              restitution = 0.f;
    float              density = 1.f;
};

struct CollisionBodyComponent {
    BodyType bodyType = BodyType::Static;
    bool     enabled = true;
    std::string profileId;
    std::vector<CollisionShape> shapes;
};

enum class CollisionProfileCoordinateSpace {
    FrameNormalized,
    World,
};

struct CollisionProfileDef {
    std::string id;
    std::string name;
    CollisionProfileCoordinateSpace coordinateSpace =
        CollisionProfileCoordinateSpace::FrameNormalized;
    std::vector<CollisionShape> shapes;
    std::unordered_map<std::string, std::vector<CollisionShape>> perAnimation;
    std::unordered_map<std::string, std::vector<CollisionShape>> perFrame;
};

// ============================================================================
// Sprite / Animation
// ============================================================================

struct ImagePointDef {
    std::string id;
    float       x = 0.f;   // normalised 0..1 on sprite
    float       y = 0.f;
};

struct SpriteComponent {
    AssetId spriteAssetId;
    Vec4    tint        = {1.f, 1.f, 1.f, 1.f};
    Vec3    fillColor   = {1.f, 1.f, 1.f};  // opaque placeholder when no texture
    float   alpha       = 1.f;
    bool    pivotFromAsset = true;
    Vec2    pivot       = {0.5f, 0.5f};
    int32_t renderOrder = 0;
    std::string shaderEffect;  // "" | outline | hit_flash | palette_swap | wave
    std::string defaultClip;
    bool        playClipOnSpawn = false;
    // Facing flags, decoupled from scale: the renderer mirrors the sprite from
    // these (scale carries only magnitude). A negative authored scale is migrated
    // into these on entity creation so scale stays a pure size.
    bool        flipX = false;
    bool        flipY = false;
    // Render layer id (set at entity creation from EntityDef.layerId). Drives
    // per-layer rank + parallax in app_scene_render; "" = default layer.
    std::string layerId;
};

struct AnimationState {
    std::string currentAnim;
    uint32_t    currentFrame   = 0;
    float       frameTime      = 0.f;
    float       frameDuration  = 0.1f;
    bool        isPlaying      = false;
    bool        isLooping      = false;
};

// ============================================================================
// Gameplay ECS components (Scene Editor — Phase D1)
// Field names mirror the editor TS model (types/components.ts) so the JSON
// produced by the editor maps 1:1 with no synonyms.
// ============================================================================

/**
 * Mutually exclusive platformer locomotion projection (ADR-0016).
 * Derived each query from PlatformerRt; not an authoring field.
 */
enum class PlatformerState {
    Stopped,
    Moving,
    Jumping,
    Falling,
};

struct PlatformerControllerComponent {
    float maxSpeed      = 300.f;
    float jumpForce     = 600.f;
    float customGravity = 1500.f;
    float coyoteTime    = 0.15f;
    float jumpBuffer    = 0.1f;
    // Ladder climbing is driven by CollisionShapeRole::Interaction sensors.
    float       climbSpeed = 120.f;   // px/s
};

struct TopDownControllerComponent {
    float maxSpeed       = 260.f;
    float acceleration   = 1600.f;
    float friction       = 2200.f;
    bool  fourDirections = false;
};

struct LinearMoverComponent {
    float directionX = 1.f;
    float directionY = 0.f;
    float speed      = 300.f;
    /** Runtime pause flag (not serialised). */
    bool  _paused    = false;
};

/** Marks an entity as the 2D camera follow target (offset + smoothing). */
struct CameraTargetComponent {
    float offsetX     = 0.f;
    float offsetY     = 0.f;
    float followSpeed = 8.f;   // exponential lerp rate (1/s); 0 = snap
};

/** Pulls tagged entities toward this entity (loot magnet). */
struct MagneticItemComponent {
    std::string attractTag = "pickup";
    float       radius     = 200.f;   // px; 0 = unlimited range
    float       pullSpeed  = 400.f;   // px/s toward holder
    /** Runtime enable flag (not serialised). */
    bool        _enabled   = true;
};

/** Swarm steering: chase a class + separate from other horde members. */
struct HordeMemberComponent {
    std::string targetClass      = "Player";
    float       maxSpeed         = 120.f;
    float       separationRadius = 48.f;
    float       separationWeight = 1.5f;
    float       chaseWeight      = 1.f;
};

/** Runtime-only: not serialized from project JSON. */
struct EntityRuntimeFlags {
    bool sceneActive = true;
};

struct AutoDestroyComponent {
    float lifespan  = 0.f;   // seconds; 0 = manual (never auto-destroy)
    float _timeAlive = 0.f;  // runtime accumulator (not serialised)
};

/** Text label (score, titles) drawn above the entity sprite (or fixed on screen). */
struct TextComponent {
    std::string text;
    std::string bindKey;             // state var to auto-display; empty = static text
    std::string bindScope = "global";
    std::string format  = "text";    // text|integer|padded|time|percent|decimals
    int         digits  = 2;         // pad width (padded) / decimal places (decimals)
    std::string prefix;              // shown before the bound value
    std::string suffix;              // shown after the bound value
    std::string fontPath;            // project-relative; empty = default font
    int         size    = 24;
    Vec4        color   = {1.f, 1.f, 1.f, 1.f};
    // 3×3 anchor point at the entity position. Canonical: "{v}-{h}" with the
    // dead-centre collapsing to "center" (e.g. "top-left", "bottom-center").
    // Legacy horizontal-only values ("left"|"center"|"right") still parse.
    std::string align   = "top-left";
    float       offsetX = 0.f;
    float       offsetY = 0.f;
    bool        screenSpace = false; // draw fixed on screen (HUD) vs in the world
};

/** Filled bar driven by a variable (mana, progress, score). */
struct GaugeComponent {
    std::string bindKey;             // variable read as current value
    std::string bindScope = "global";
    float       maxValue   = 100.f;  // value mapped to a full bar
    float       width      = 64.f;
    float       height     = 8.f;
    Vec4        fillColor  = {0.23f, 0.82f, 0.23f, 1.f};
    Vec4        bgColor    = {0.13f, 0.13f, 0.13f, 1.f};
    std::string direction  = "horizontal"; // "horizontal" | "vertical"
    float       offsetX    = 0.f;
    float       offsetY    = -40.f;
    bool        screenSpace = false;
};

/** NPC / talkable — references dialogs/{dialogId}.json in project root. */
enum class BoxColliderMode {
    Solid,
    Trigger,
    OneWayPlatform,
};

struct BoxCollider2DComponent {
    Vec2 offset;
    Vec2 size{32.f, 32.f};
    bool enabled = true;
    BoxColliderMode mode = BoxColliderMode::Solid;
};

struct DialogComponent {
    std::string dialogId;
    std::string startNode;       // optional override; empty = graph startNode
    float       textSpeed = 40.f;
    std::string triggerMessage;  // optional Logic Board message name
};

// LifecycleEvent — emitted by EntityRegistry signals (on_construct/on_destroy
// of Identity) when an entity gains its className/tags (Spawned) or is being
// destroyed (Destroyed). Drained once per frame by the gateway and routed to
// Lua handlers registered via `lifecycle.onSpawn(class, fn)` /
// `lifecycle.onDestroy(class, fn)`. Order matches registry insertion order
// for Spawned events and reverse-insertion for Destroyed (signal fires
// before the entity is physically removed). See ECS_IMPLEMENTATION_GUIDE.md §10.
struct LifecycleEvent {
    enum class Kind { Spawned, Destroyed };
    Kind                     kind      = Kind::Spawned;
    EntityId                 id        = 0;
    std::string              className;
    std::vector<std::string> tags;
};

/** Object-Type-owned static sprite rendering defaults (project format v9). */
struct SpriteRendererComponent {
    AssetId imageAssetId; // "" = no static image
    bool    visible = true;
};

/** Object-Type-owned sprite animation playback defaults (project format v9).
 *  Owns the animation asset reference and the initial clip; SpriteRenderer
 *  remains the static fallback presentation. */
struct SpriteAnimatorComponent {
    AnimationAssetId animationAssetId;
    AnimationClipId  defaultClipId;
    bool             autoPlay = true;
    float            playbackSpeed = 1.f;
};

/** One stable, ordered reference to a manual Script Asset. */
struct ScriptAttachmentDef {
    ScriptAttachmentId id;
    AssetId             scriptAssetId;
    bool                enabled = true;
};

/** Authored only on Object Types. Scene instances never override scripts. */
struct ScriptComponent {
    std::vector<ScriptAttachmentDef> attachments;
};

/** Sparse per-instance delta over SpriteRendererComponent. Null means inherit. */
struct SpriteRendererOverride {
    std::optional<AssetId> imageAssetId;
    std::optional<bool>    visible;
    // Migration-only compatibility bit for v3 projects where only some
    // instances had the component. Normal authoring never exposes this field.
    std::optional<bool>    capabilityEnabled;
};

/** Sparse per-instance delta over SpriteAnimatorComponent. Null means inherit. */
struct SpriteAnimatorOverride {
    std::optional<AnimationAssetId> animationAssetId;
    std::optional<AnimationClipId>  defaultClipId;
    std::optional<bool>             autoPlay;
    std::optional<float>            playbackSpeed;
    // See SpriteRendererOverride::capabilityEnabled.
    std::optional<bool>             capabilityEnabled;
};

// Project-format v10 authoring model. A Sprite has one presentation source;
// runtime materialisation may split it into transient renderer/animator data.
struct SpritePresentationNone {};

struct SpritePresentationImage {
    AssetId imageAssetId;
};

struct SpritePresentationAnimation {
    AnimationAssetId animationAssetId;
    AnimationClipId  defaultClipId;
    bool             autoPlay = true;
    float            playbackSpeed = 1.f;
};

using SpritePresentationSource = std::variant<
    SpritePresentationNone,
    SpritePresentationImage,
    SpritePresentationAnimation>;

struct SpritePresentationComponent {
    bool                     visible = true;
    SpritePresentationSource source = SpritePresentationNone{};
};

// A source override replaces the complete source alternative. This prevents
// partially inherited animation state from becoming a second authority.
struct SpritePresentationOverride {
    std::optional<bool>                     visible;
    std::optional<SpritePresentationSource> source;
};

// ============================================================================
// Entity / Scene definitions
// ============================================================================

struct EntityDef {
    EntityId         id;
    std::string      name;
    std::string      className;
    std::string      layerId;      // render layer id ("" = default layer)
    std::vector<std::string> tags;
    Transform        transform;
    SpriteComponent  sprite;
    std::optional<SpritePresentationComponent> spritePresentation;
    // Deprecated v9 authoring input. Migration clears these fields; runtime
    // still uses the component types as transient materialisation data.
    std::optional<SpriteRendererComponent> spriteRenderer;
    std::optional<SpriteAnimatorComponent> spriteAnimator;
    std::optional<ScriptComponent> scripts;
    PhysicsComponent physics;
    // Session scratch only (ADR-0014). Not current authoring — derived from
    // boxCollider2D by materializeBoxCollider2D / applyEntityDefToRegistry.
    // Legacy JSON "collisionBody" on entities/objectTypes is ignored.
    std::optional<CollisionBodyComponent> collisionBody;
    AnimationState   animation;
    // Optional gameplay components (Phase D1)
    std::optional<PlatformerControllerComponent> platformerController;
    std::optional<TopDownControllerComponent>    topDownController;
    std::optional<LinearMoverComponent>          linearMover;
    std::optional<CameraTargetComponent>         cameraTarget;
    std::optional<MagneticItemComponent>         magneticItem;
    std::optional<HordeMemberComponent>          hordeMember;
    std::optional<AutoDestroyComponent>          autoDestroy;
    std::optional<DialogComponent>               dialog;
    std::optional<TextComponent>                 text;
    std::optional<GaugeComponent>                gauge;
    std::optional<BoxCollider2DComponent>        boxCollider2D;
    /** Design-time flag: when false the sprite is hidden in play / shipped
     *  builds. The editor preview always draws the sprite (with a dashed
     *  outline). Runtime Logic Board setVisible() toggles sprite alpha. */
    bool                                         visible = true;
    EntityRuntimeFlags                           runtime;
    std::vector<GameVariableDefinition>          localVariables;
    std::unordered_map<std::string, GameVariableValue> localVariableOverrides;
    // Authored only on ProjectDoc.objectTypes. Scene instances never override it.
    std::optional<LogicBoardDef>                       logicBoard;
};

// Tilemap (Scene Editor Phase D2) — field names mirror editor TS.
// Grid dimension limits: see tilemap_grid.h (sync with editor/src/types/tilemap-grid.ts).
//
// legacy — no authoring path in artcade-editor; see ADR-0001. Do not add new
// usages. Still compiled/consumed by the separate runtime-cpp repo
// (World::activeTilemap_ / tilemap-renderer.cpp / scene-json.cpp) via the
// vendor/artcade-runtime symlink — do not remove without confirming that
// repo no longer needs it.
struct TilesetSourceRef {
    std::string tilesetAssetId;
};

// legacy, see comment above TilesetSourceRef
struct TilemapData {
    float            tileSize = 32.f;
    int              cols     = 0;   // 0 = no tilemap
    int              rows     = 0;
    std::vector<int> data;           // size cols*rows, row-major, 0 = empty
    std::vector<int> sourceIndices;  // parallel to data; 0 = empty; 1..N → tilesetSources[N-1]
    std::vector<TilesetSourceRef> tilesetSources;
    std::string      tilesetAssetId; // legacy migration only
    std::string      defaultTilesetAssetId;
};

/** Per-layer parallax scroll factor (1 = moves with world, <1 = far, >1 = near). */
struct LayerParallax {
    float x = 1.f;
    float y = 1.f;
};

/** Optional repeating background image painted under a layer's entities. */
struct LayerBackground {
    std::string imageId;        // ImageAsset id; empty = no background
    bool        tileX   = true; // repeat horizontally to fill the view
    bool        tileY   = true; // repeat vertically to fill the view
    float       scrollX = 0.f;  // constant auto-scroll px/s (camera-independent)
    float       scrollY = 0.f;
};

/**
 * Per-scene render layer (SceneDef.layers).
 * Order: index 0 = background (drawn first), last = foreground (drawn on top).
 * Identity + display name + editor lock; visual props live in SceneLayerSettings.
 */
struct SceneLayerDef {
    std::string     id;            // stable layer id (referenced by instances)
    std::string     name;          // display only
    bool            locked  = false; // editor-only (pick / transform gating)
};

/** Per-scene visual overrides for a render layer (keyed by layer id). */
struct SceneLayerSettings {
    bool            visible = true;
    float           opacity = 1.f;
    LayerParallax   parallax;
    LayerBackground background;
};

// Pixel-size-first slicing config for a TilesetAsset's source image: tile
// width/height are authored directly, columns/rows are derived (see
// tileset_slicing.h) rather than stored, since they're a function of the
// image's own pixel dimensions.
struct TilesetSlicing {
    int tileWidth  = 32;
    int tileHeight = 32;
    int marginX    = 0;
    int marginY    = 0;
    int spacingX   = 0;
    int spacingY   = 0;
};

// One sliced cell of a TilesetAsset. The id is stable within its parent
// tileset (survives reordering/save/reload); it is not a re-slicing identity
// guarantee across a changed TilesetSlicing (see tileset_slicing.h).
struct TileDefinition {
    std::string id;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// Spritesheet tileset asset.
struct TilesetAsset {
    AssetId        assetId;
    std::string    name;
    AssetId        imageAssetId;   // references ProjectDoc.imageAssets; never a raw path
    TilesetSlicing slicing;
    std::vector<TileDefinition> tiles;   // empty until sliced
};

using TileId = std::string;

// Per-cell transform, shaped for the spec's already-planned flip/rotate
// (Slice 10 — Funzioni avanzate) so that feature is additive to this format
// later rather than a JSON migration. Unused (always None) until then.
enum class TileTransformFlags : std::uint8_t {
    None  = 0,
    FlipX = 1 << 0,
    FlipY = 1 << 1,
    Rot90 = 1 << 2,
};

struct TilemapCellValue {
    TileId              tileId;
    TileTransformFlags  flags = TileTransformFlags::None;

    bool operator==(const TilemapCellValue& other) const {
        return tileId == other.tileId && flags == other.flags;
    }
};

// nullopt = empty cell. No sentinel/invented tile id (mirrors
// TilesetEditorState::selectedTileId's own optional<string> shape). `tileId`,
// when set, names a TileDefinition::id within the owning TilemapComponent's
// tilesetAssetId.
using TilemapCell = std::optional<TilemapCellValue>;

// A fixed-size block of cells, addressed by chunk coordinates. Chunks are
// created lazily by a future paint slice; Tileset/Tilemap Editor Slice 4
// never creates one, so `chunks` is always empty in every real component —
// the shape exists now so painting only appends to it later, never changes
// it. See tilemap_chunk_math.h for the cell <-> chunk coordinate math.
struct TilemapChunk {
    int                       chunkX = 0;
    int                       chunkY = 0;
    std::vector<TilemapCell>  cells;   // size chunkSize*chunkSize, row-major
};

// Entity-owned persistent tile grid (Tileset/Tilemap Editor, Slice 4). One
// per placed SceneInstanceDef (ADR-0001: entity-owned, not scene/layer-keyed
// like the legacy TilemapData above). World origin of cell (0,0) is the
// owning instance's existing Transform.position — this component
// intentionally has no origin/position field of its own (ADR-0001: no
// hidden synchronization, no two ownership paths for one fact).
struct TilemapComponent {
    AssetId                   tilesetAssetId;          // references ProjectDoc.tilesets
    Vec2                      cellSize  = {32.f, 32.f}; // local, unscaled cell size
    // Chosen at creation, immutable for the component's lifetime in this
    // MVP (no setter command exists). Changing it once chunks are populated
    // means re-bucketing every cell, an unsolved problem left to whichever
    // future slice needs it.
    int                       chunkSize = 16;
    std::vector<TilemapChunk> chunks;                  // always [] in Slice 4
};

/** Scene placement of an object type (project format v2). */
struct SceneInstanceDef {
    EntityId    id           = 0;
    std::string objectTypeId;
    std::string instanceName;
    Transform   transform;
    bool        visible      = true;
    std::string layerId;      // render layer id ("" = default layer)
    std::optional<SpritePresentationOverride> spritePresentationOverride;
    // Deprecated v9 authoring input. Migration clears these fields.
    std::optional<SpriteRendererOverride> spriteRendererOverride;
    std::optional<SpriteAnimatorOverride> spriteAnimatorOverride;
    // v3 decoder/migration input only. No authoring/runtime path may read
    // these, and the v4 serializer never emits them.
    std::optional<SpriteRendererComponent> legacySpriteRendererV3;
    std::optional<SpriteAnimatorComponent> legacySpriteAnimatorV3;
    std::optional<TilemapComponent>        tilemap;
    // Native-editor authority: one placed instance, never an Object Type.
    std::optional<CameraTargetComponent>   cameraTarget;
    std::unordered_map<std::string, GameVariableValue> localVariableOverrides;
};

struct SceneDef {
    SceneId             id;
    std::string         name;
    Vec2                worldSize    = {
        ProjectDefaults::kSceneWorldWidth,
        ProjectDefaults::kSceneWorldHeight,
    };
    Vec2                viewportSize = {
        ProjectDefaults::kSceneViewportWidth,
        ProjectDefaults::kSceneViewportHeight,
    };
    /** World-space top-left of the camera's initial view (player's start view). */
    Vec2                cameraStart  = { 0.f, 0.f };
    Vec4                backgroundColor;
    std::vector<EntityId> entityIds;
    // Per-scene render layers — SINGLE authority of draw order
    // (index 0 = background, last = foreground). `defaultLayerId` is the
    // persistent fallback every scene must have. No project-global layer list.
    std::vector<SceneLayerDef> layers;
    std::string               defaultLayerId;
    std::vector<SceneInstanceDef> instances;
    /** Merged grid for physics / legacy single-layer projects. */
    // legacy, see comment above TilesetSourceRef
    TilemapData         tilemap;     // cols==0 → absent
    /** Per-layer paint grids keyed by layer id (editor tilemapLayers). */
    // legacy, see comment above TilesetSourceRef
    std::unordered_map<std::string, TilemapData> tilemapLayers;
    /** Per-scene visual overrides keyed by layer id (visible/opacity/parallax/bg). */
    std::unordered_map<std::string, SceneLayerSettings> layerSettings;
};

struct TilePaletteEntry {
    int         id    = 0;
    std::string name;
    Vec4        color = {0.5f, 0.5f, 0.5f, 1.f};
    std::optional<CollisionBodyComponent> collisionBody;
};

/** Runtime cache per tile id (from tilePalette). */
struct TileSurfaceMeta {
    bool        blocks      = false;
    bool        oneWay      = false;
    std::optional<CollisionBodyComponent> collisionBody;
};

// ============================================================================
// Project document (root data model)
// ============================================================================

struct AnimationFrameRect {
    float x = 0.f;
    float y = 0.f;
    float w = 0.f;
    float h = 0.f;
};

struct AnimationClipDef {
    std::string              name;
    std::vector<AnimationFrameRect> frames;
    float                    fps  = 12.f;
    bool                     loop = true;
};

struct ImageAssetDef {
    std::string assetId;
    std::string name;
    std::string sourcePath; // authoring/import path resolved by derived rendering resources.
    Vec2 defaultPivot = {0.5f, 0.5f};
    std::vector<ImagePointDef> imagePoints;
    std::vector<AnimationClipDef> clips;
};

// Short SFX load into memory (LoadSound); longer music plays via a stream
// (LoadMusicStream). The mode is authoring data chosen at import, not deduced
// rigidly from the extension.
enum class AudioLoadMode { StaticSound, Stream };

struct AudioAssetDef {
    std::string   assetId;
    std::string   name;
    std::string   sourcePath;
    AudioLoadMode loadMode = AudioLoadMode::StaticSound;
    /** Historical provenance only: which Generated SFX produced this WAV.
     *  Not a second recipe authority; empty for imported/independent audio. */
    std::optional<std::string> generatedFromSfxId;
};

// Which glyph ranges to rasterise when the font is consumed (a font cache, later).
// European covers Italian/Latin text; CustomText would derive ranges from project
// strings. Authoring data only — no rasterisation happens at import.
enum class FontGlyphPreset { BasicLatin, European, CustomText };

struct FontAssetDef {
    std::string     assetId;
    std::string     name;
    std::string     sourcePath;
    int             defaultPixelSize = 32;
    FontGlyphPreset glyphPreset = FontGlyphPreset::European;
};

// Manual Lua source registered by the authoring editor. The source remains an
// external project-relative .lua file; ProjectDoc owns metadata only. AssetId
// is the stable identity used by future type-owned Script attachments.
struct ScriptAssetDef {
    AssetId     assetId;
    std::string name;
    std::string sourcePath;
};

enum class AnimationPlaybackMode { Loop, Once };

/** Authoritative rectangle in the animation asset frame pool (schema v9). */
struct SpriteFrameDef {
    SpriteFrameId id;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool operator==(const SpriteFrameDef& other) const {
        return id == other.id && x == other.x && y == other.y
            && width == other.width && height == other.height;
    }
};

// Schema v9: clips reference the asset frame pool by id. The same SpriteFrameId
// may repeat in frameIds to hold a pose longer without per-frame durations.
struct SpriteAnimationClipDef {
    AnimationClipId id;
    std::string name;
    std::vector<SpriteFrameId> frameIds;
    float framesPerSecond = 12.f;
    AnimationPlaybackMode playbackMode = AnimationPlaybackMode::Loop;
};

struct SpriteAnimationAssetDef {
    AnimationAssetId id;
    std::string name;
    AssetId sourceImageAssetId;
    std::vector<SpriteFrameDef> frames;
    std::vector<SpriteAnimationClipDef> clips;
};

enum class PhysicsMode {
    Auto,
    Off,
    On,
};

/** How the game viewport is scaled to the OS window / backbuffer in play. */
enum class OutputPolicy {
    Fit,
    Fill,
    Stretch,
};

struct WorldSettings {
    float       gravity           = 9.81f;
    float       pixelsPerMeter    = 100.f;
    float       timeScale         = 1.f;
    PhysicsMode physicsMode       = PhysicsMode::Auto;
    bool        physicsDebugDraw  = false;
    OutputPolicy outputPolicy     = OutputPolicy::Fit;
};

/** Runtime timing + physics from project JSON (editor WASM + native load). */
struct ProjectRuntimeSettings {
    float       targetFPS         = 60.f;
    PhysicsMode physicsMode       = PhysicsMode::Auto;
    float       gravity           = 9.81f;
    float       pixelsPerMeter    = 100.f;
    float       timeScale         = 1.f;
    bool        physicsDebugDraw  = false;
    OutputPolicy outputPolicy     = OutputPolicy::Fit;
};

struct ProjectDoc {
    // Durable logical identity. Display name and folder path may be reused;
    // a New Project always allocates a fresh projectId even in the same root.
    std::string  projectId;
    std::string  projectName;
    std::string  version         = "2.0.0";
    int          formatVersion   = 0;
    std::string  licenseTier     = "free";
    float        targetFPS       = 60.f;
    SceneId      activeSceneId;
    std::string  mainScriptPath  = "scripts/main.luac";

    /** Object type catalog (v2). Key = type id; prototype uses className == key. */
    std::unordered_map<std::string, EntityDef> objectTypes;
    std::unordered_map<EntityId, EntityDef> entities;
    std::unordered_map<SceneId,  SceneDef>  scenes;
    std::unordered_map<SceneId,  std::string> thumbnails;
    std::vector<PhysicsLayerDef>  physicsLayers; // collision filtering, separate from render layers
    std::unordered_map<std::string, CollisionProfileDef> collisionProfiles;
    std::unordered_map<std::string, std::string> spritePathToAssetId;
    std::vector<TilePaletteEntry> tilePalette;   // Phase D2
    std::vector<TilesetAsset>     tilesets;      // Phase F3
    std::vector<ImageAssetDef>    imageAssets;   // editor assets + image points
    std::vector<SpriteAnimationAssetDef> spriteAnimationAssets;
    std::vector<AudioAssetDef>    audioAssets;   // authoring import catalog
    // Authoring-only procedural SFX recipes. Runtime systems consume only the
    // AudioAssetDef referenced by GeneratedSfxDef::outputAssetId.
    std::vector<artcade::sfx::GeneratedSfxDef> generatedSfx;
    std::vector<FontAssetDef>     fontAssets;    // authoring import catalog
    std::vector<ScriptAssetDef>   scriptAssets;  // external manual Lua source metadata
    WorldSettings                 world{};
    std::vector<GameVariableDefinition> globalVariables;
};

inline ProjectRuntimeSettings runtimeSettingsFromProjectDoc(const ProjectDoc& doc) {
    ProjectRuntimeSettings s;
    s.targetFPS          = doc.targetFPS;
    s.physicsMode        = doc.world.physicsMode;
    s.gravity            = doc.world.gravity;
    s.pixelsPerMeter     = doc.world.pixelsPerMeter;
    s.timeScale          = doc.world.timeScale;
    s.physicsDebugDraw   = doc.world.physicsDebugDraw;
    s.outputPolicy       = doc.world.outputPolicy;
    return s;
}

// ============================================================================
// Global state (runtime key-value store)
// ============================================================================

using StateValue = std::variant<int32_t, float, std::string, bool>;
using GlobalState = std::unordered_map<std::string, StateValue>;

// ============================================================================
// Input
// ============================================================================

struct InputState {
    bool keysDown[512]             = {};
    bool keysPressedThisFrame[512] = {};
    bool keysReleasedThisFrame[512]= {};
    Vec2 mousePosition;
    bool mouseButtonDown[3]        = {};
};

} // namespace ArtCade
