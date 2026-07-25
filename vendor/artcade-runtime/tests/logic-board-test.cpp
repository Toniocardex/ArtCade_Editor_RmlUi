#include "modules/logic-core/include/logic-core.h"
#include "modules/logic-runtime/include/logic-runtime.h"
#include "modules/lua-runtime/include/lua-host.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace ArtCade;
using namespace ArtCade::Logic;

static int passed = 0;
static int failed = 0;
#define CHECK(x) do { if (x) ++passed; else { ++failed; std::cerr << "FAIL " #x " line " << __LINE__ << "\n"; } } while (0)

struct Host final : ILogicRuntimeHost {
    std::vector<std::string> calls;
    std::vector<std::pair<EntityId, float>> rotations;
    std::vector<std::pair<EntityId, float>> rotationDeltas;
    std::vector<std::pair<EntityId, Vec2>> scales;
    LogicRuntime* runtime = nullptr;
    std::optional<ScopeToken> cancelOnVisible;
    bool failVisible = false;
    std::unordered_set<EntityId> grounded;
    std::unordered_set<EntityId> falling;
    std::unordered_set<EntityId> movingHorizontally;
    std::unordered_map<EntityId, PlatformerState> platformerStates;
    std::unordered_map<EntityId, bool> visible;
    std::unordered_map<std::string, double> state;
    std::unordered_map<std::string, bool> boolState;
    bool keyDown = false;
    /** Pre-declare a Number key (mirrors VariableManager catalog materialization). */
    void declareNumber(const GameVariableId& id, double initial = 0.0) {
        state[id] = initial;
    }
    /** Pre-declare a Boolean key (mirrors VariableManager catalog materialization). */
    void declareBoolean(const GameVariableId& id, bool initial = false) {
        boolState[id] = initial;
    }
    bool setVisible(EntityId owner, bool value) override {
        calls.push_back("visible:" + std::to_string(owner) + ":" + (value ? "1" : "0"));
        visible[owner] = value;
        if (runtime && cancelOnVisible) runtime->cancelScope(*cancelOnVisible);
        return !failVisible;
    }
    bool isVisible(EntityId owner) override {
        calls.push_back("is_visible:" + std::to_string(owner));
        const auto it = visible.find(owner);
        return it == visible.end() ? true : it->second;
    }
    bool setSpriteFlipX(EntityId owner, bool flipX) override {
        calls.push_back("flip_x:" + std::to_string(owner) + ":" + (flipX ? "1" : "0"));
        return true;
    }
    bool setPosition(EntityId owner, Vec2 value) override {
        calls.push_back("position:" + std::to_string(owner) + ":"
                        + std::to_string(static_cast<int>(value.x)) + ","
                        + std::to_string(static_cast<int>(value.y)));
        return true;
    }
    bool translate(EntityId owner, Vec2 delta) override {
        calls.push_back("translate:" + std::to_string(owner) + ":"
                        + std::to_string(static_cast<int>(delta.x)) + ","
                        + std::to_string(static_cast<int>(delta.y)));
        return true;
    }
    bool setRotation(EntityId owner, float radians) override {
        rotations.emplace_back(owner, radians);
        calls.push_back("rotation:" + std::to_string(owner) + ":" + std::to_string(radians));
        return true;
    }
    bool rotateBy(EntityId owner, float deltaRadians) override {
        rotationDeltas.emplace_back(owner, deltaRadians);
        calls.push_back("rotate_by:" + std::to_string(owner) + ":" + std::to_string(deltaRadians));
        return true;
    }
    bool setScale(EntityId owner, Vec2 scale) override {
        scales.emplace_back(owner, scale);
        calls.push_back("scale:" + std::to_string(owner) + ":"
                        + std::to_string(scale.x) + "," + std::to_string(scale.y));
        return true;
    }
    bool isGrounded(EntityId owner) override {
        return grounded.count(owner) != 0;
    }
    bool isFalling(EntityId owner) override {
        return platformerState(owner) == PlatformerState::Falling;
    }
    PlatformerState platformerState(EntityId owner) override {
        const auto it = platformerStates.find(owner);
        if (it != platformerStates.end()) return it->second;
        if (movingHorizontally.count(owner) != 0) return PlatformerState::Moving;
        if (falling.count(owner) != 0) return PlatformerState::Falling;
        return PlatformerState::Stopped;
    }
    bool isPlatformerMoving(EntityId owner) override {
        return platformerState(owner) == PlatformerState::Moving;
    }
    bool requestPlatformerMove(EntityId owner, float axis) override {
        calls.push_back("platformer_move:" + std::to_string(owner) + ":" + std::to_string(axis));
        return true;
    }
    bool requestTopDownMove(EntityId owner, Vec2 direction) override {
        calls.push_back("topdown_move:" + std::to_string(owner) + ":"
                        + std::to_string(static_cast<int>(direction.x)) + ","
                        + std::to_string(static_cast<int>(direction.y)));
        return true;
    }
    bool requestPlatformerJump(EntityId owner) override {
        calls.push_back("platformer_jump:" + std::to_string(owner));
        return true;
    }
    bool isObjectType(EntityId, const ObjectTypeId&) override { return false; }
    bool requestDestroy(EntityId owner) override {
        calls.push_back("destroy:" + std::to_string(owner));
        return true;
    }
    bool playAnimationClip(EntityId owner, const AssetId& animationAssetId,
                           const std::string& clipId) override {
        calls.push_back("play_clip:" + std::to_string(owner) + ":" + animationAssetId + ":" + clipId);
        return true;
    }
    bool stopAnimation(EntityId owner) override {
        calls.push_back("stop_animation:" + std::to_string(owner));
        return true;
    }
    bool setAnimationPlaybackSpeed(EntityId owner, float speed) override {
        calls.push_back("animation_speed:" + std::to_string(owner) + ":" + std::to_string(speed));
        return true;
    }
    bool playSound(EntityId owner, const AssetId& audioAssetId, float volume) override {
        calls.push_back("play_sound:" + std::to_string(owner) + ":" + audioAssetId + ":"
                       + std::to_string(volume));
        return true;
    }
    bool setStateNumber(const GameVariableId& id, double value) override {
        if (state.find(id) == state.end()) return false;
        state[id] = value;
        calls.push_back("state_set:" + id + ":" + std::to_string(static_cast<int>(value)));
        return true;
    }
    bool addStateNumber(const GameVariableId& id, double delta) override {
        if (state.find(id) == state.end()) return false;
        state[id] = state[id] + delta;
        calls.push_back("state_add:" + id + ":" + std::to_string(static_cast<int>(delta)));
        return true;
    }
    bool toggleStateBoolean(const GameVariableId& id) override {
        const auto it = boolState.find(id);
        if (it == boolState.end()) return false;
        it->second = !it->second;
        calls.push_back("state_toggle:" + id + ":" + (it->second ? "true" : "false"));
        return true;
    }
    std::optional<double> getStateNumber(const GameVariableId& id) const override {
        const auto it = state.find(id);
        if (it == state.end()) return std::nullopt;
        return it->second;
    }
    bool setVelocity(EntityId owner, Vec2 velocity) override {
        calls.push_back("velocity:" + std::to_string(owner) + ":"
                        + std::to_string(static_cast<int>(velocity.x)) + ","
                        + std::to_string(static_cast<int>(velocity.y)));
        return true;
    }
    bool isKeyDown(LogicKey) override { return keyDown; }
    EntityId nextSpawnId = 99;
    bool failSpawn = false;
    std::vector<EntityId> destroyedSpawns;
    EntityId spawnObjectType(EntityId owner, const ObjectTypeId& objectTypeId,
                             float x, float y) override {
        calls.push_back("spawn:" + std::to_string(owner) + ":" + objectTypeId + ":"
                        + std::to_string(static_cast<int>(x)) + ","
                        + std::to_string(static_cast<int>(y)));
        if (failSpawn) {
            // Mirrors RuntimeLogicHostAdapter: install failure → no id returned.
            destroyedSpawns.push_back(nextSpawnId);
            calls.push_back("spawn_rollback:" + std::to_string(nextSpawnId));
            return INVALID_ENTITY;
        }
        return nextSpawnId;
    }
    bool requestSceneRestart() override {
        calls.push_back("scene_restart");
        return true;
    }
    bool requestSceneGoTo(const SceneId& sceneId) override {
        if (sceneId.empty()) return false;
        calls.push_back("scene_go_to:" + sceneId);
        return true;
    }
};

static LogicConditionClause makeClause(
    LogicBlockDef block,
    LogicConditionJoin join = LogicConditionJoin::And,
    bool negated = false) {
    return {join, negated, std::move(block)};
}

static LogicBoardDef makeBoard() {
    LogicBoardDef board;
    board.id = "logic:Hero";
    LogicRuleDef start = makeDefaultRule("rule-1");
    std::get<bool>(start.actions[0].properties[1].value) = false;
    board.rules.push_back(start);

    LogicRuleDef key = makeDefaultRule("rule-2");
    key.name = "Logic 02";
    key.trigger = {kKeyPressed, {{"key", LogicKey::Space}}};
    key.actions[0] = {kSetPosition,
        {{"target", LogicEntityReference{}}, {"position", Vec2{12.f, 34.f}}}};
    board.rules.push_back(key);
    return board;
}

static void testCompilerAndJson() {
    LogicBoardDef board = makeBoard();
    board.rules[0].name = "Player Movement";
    board.rules[0].conditions = {
        makeClause(makeDefaultCondition()),
        makeClause(makeDefaultBlock(kKeyDown, BlockKind::Condition),
                   LogicConditionJoin::Or, true),
    };
    LogicCompileResult a = compileBoard("Hero", board);
    LogicCompileResult b = compileBoard("Hero", board);
    CHECK(a.ok());
    CHECK(a.programs.size() == 1);
    CHECK(a.programs[0].source == b.programs[0].source);
    CHECK(!a.programs[0].requiresTick);

    const auto json = logicBoardToJson(board);
    LogicBoardDef loaded;
    CHECK(logicBoardFromJson(json, loaded).ok);
    CHECK(logicBoardToJson(loaded) == json);
    CHECK(loaded.rules[0].name == "Player Movement");
    CHECK(loaded.rules[0].conditions.size() == 2);
    CHECK(loaded.rules[0].conditions[1].joinBefore == LogicConditionJoin::Or);
    CHECK(loaded.rules[0].conditions[1].negated);

    auto stale_schema = json;
    stale_schema["schemaVersion"] = 2u;
    CHECK(!logicBoardFromJson(stale_schema, loaded).ok);

    auto obsolete_condition_shape = json;
    obsolete_condition_shape["rules"][0]["conditions"][0] =
        obsolete_condition_shape["rules"][0]["conditions"][0]["block"];
    CHECK(!logicBoardFromJson(obsolete_condition_shape, loaded).ok);

    auto unknown_join = json;
    unknown_join["rules"][0]["conditions"][1]["join"] = "xor";
    CHECK(!logicBoardFromJson(unknown_join, loaded).ok);

    auto first_or = json;
    first_or["rules"][0]["conditions"][0]["join"] = "or";
    CHECK(!logicBoardFromJson(first_or, loaded).ok);
    LogicBoardDef invalid_for_serialization = board;
    invalid_for_serialization.rules[0].conditions[0].joinBefore = LogicConditionJoin::Or;
    bool rejected_serialization = false;
    try {
        (void)logicBoardToJson(invalid_for_serialization);
    } catch (const std::logic_error&) {
        rejected_serialization = true;
    }
    CHECK(rejected_serialization);

    auto missing_name = json;
    missing_name["rules"][0].erase("name");
    CHECK(!logicBoardFromJson(missing_name, loaded).ok);

    auto wrong_variable_kind = json;
    wrong_variable_kind["rules"][0]["actions"][0]["properties"][0]["value"] = {
        {"kind", "string"}, {"value", "score"}};
    CHECK(!logicBoardFromJson(wrong_variable_kind, loaded).ok);

    loaded.apiVersion = 999;
    CHECK(!validateBoard("Hero", loaded).empty());
    loaded = board;
    loaded.rules[0].trigger.typeId = "unknown.trigger";
    CHECK(!compileBoard("Hero", loaded).ok());

    ProjectDoc project;
    EntityDef z; z.logicBoard = board; z.logicBoard->id = "logic:Z";
    z.platformerController = PlatformerControllerComponent{};
    EntityDef aType; aType.logicBoard = board; aType.logicBoard->id = "logic:A";
    aType.platformerController = PlatformerControllerComponent{};
    project.objectTypes.emplace("Z", z);
    project.objectTypes.emplace("A", aType);
    const LogicCompileResult projectCompiled = compileProjectLogic(project);
    CHECK(projectCompiled.ok());
    CHECK(projectCompiled.programs.size() == 2);
    CHECK(projectCompiled.programs[0].objectTypeId == "A");
    CHECK(projectCompiled.programs[1].objectTypeId == "Z");
}

static void testRuntime() {
    const LogicCompileResult compiled = compileBoard("Hero", makeBoard());
    Host host;
    LogicRuntime runtime(host);
    std::string error;
    CHECK(runtime.loadPrograms(compiled.programs, &error));
    const auto one = runtime.install("Hero", 10, &error);
    const auto two = runtime.install("Hero", 20, &error);
    CHECK(one.has_value());
    CHECK(two.has_value());

    runtime.dispatchStart();
    CHECK(host.calls.size() == 2);
    CHECK(host.calls[0] == "visible:10:0");
    CHECK(host.calls[1] == "visible:20:0");

    runtime.dispatchKeyPressed(LogicKey::Space);
    CHECK(host.calls.size() == 4);
    CHECK(host.calls[2] == "position:10:12,34");
    CHECK(host.calls[3] == "position:20:12,34");
    CHECK(runtime.cancelScope(*one));
    CHECK(!runtime.cancelScope(*one));
    runtime.dispatchKeyPressed(LogicKey::Space);
    CHECK(host.calls.size() == 5);
    CHECK(host.calls.back() == "position:20:12,34");
    runtime.shutdown();
}

static void testStrictSandboxAndBudget() {
    using namespace ArtCade::Modules;
    LuaHost host({LuaSandboxProfile::LogicBoardStrict, 1024u * 1024u});
    CHECK(host.init());
    CHECK(host.loadLuaSource(
        "assert(io == nil and os == nil and package == nil and require == nil)\n"
        "assert(debug == nil and dofile == nil and loadfile == nil and load == nil)\n"
        "assert(coroutine == nil and collectgarbage == nil)\n"
        "__artcade_requires_tick = false"));
    CHECK(!host.isScriptTickRequired());
    host.shutdown();

    Host runtimeHost;
    LogicRuntimeLimits limits;
    limits.maxInstructionsPerCallback = 2000;
    LogicRuntime runtime(runtimeHost, limits);
    LogicProgram program;
    program.objectTypeId = "Loop";
    program.boardId = "logic:Loop";
    program.source =
        "logic.require_api_version(2)\n"
        "logic.define_board('logic:Loop', 'Loop', function(context)\n"
        " context:on_start('rule-loop', function() while true do end end)\n"
        "end)\n";
    std::string error;
    CHECK(runtime.loadPrograms({program}, &error));
    CHECK(runtime.install("Loop", 1, &error).has_value());
    runtime.dispatchStart();
    CHECK(!runtime.diagnostics().empty());
    runtime.dispatchStart(); // disabled subscription is not retried
    CHECK(runtime.diagnostics().size() == 1);
}

static LogicProgram customProgram(std::string objectTypeId, std::string body) {
    LogicProgram program;
    program.objectTypeId = objectTypeId;
    program.boardId = "logic:" + objectTypeId;
    program.source = "logic.require_api_version(2)\nlogic.define_board('" + program.boardId
        + "', '" + objectTypeId + "', function(context)\n" + body + "\nend)\n";
    return program;
}

static void testLimitsSnapshotAndIsolation() {
    {
        Host host;
        LogicRuntimeLimits limits;
        limits.maxEventDepth = 0;
        LogicRuntime runtime(host, limits);
        const auto compiled = compileBoard("Hero", makeBoard());
        std::string error;
        CHECK(runtime.loadPrograms(compiled.programs, &error));
        CHECK(runtime.install("Hero", 1, &error).has_value());
        runtime.beginFrame();
        runtime.dispatchStart();
        CHECK(host.calls.empty());
        CHECK(!runtime.diagnostics().empty());
    }
    {
        Host host;
        LogicRuntimeLimits limits;
        limits.maxScopes = 1;
        LogicRuntime runtime(host, limits);
        const auto compiled = compileBoard("Hero", makeBoard());
        std::string error;
        CHECK(runtime.loadPrograms(compiled.programs, &error));
        CHECK(runtime.install("Hero", 1, &error).has_value());
        CHECK(!runtime.install("Hero", 2, &error).has_value());
    }
    {
        Host host;
        LogicRuntimeLimits limits;
        limits.maxSubscriptionsPerScope = 1;
        LogicRuntime runtime(host, limits);
        std::string error;
        const LogicProgram two = customProgram("Two",
            " context:on_start('one', function() end)\n"
            " context:on_start('two', function() end)");
        CHECK(runtime.loadPrograms({two}, &error));
        CHECK(!runtime.install("Two", 1, &error).has_value());
    }
    {
        Host host;
        LogicRuntimeLimits limits;
        limits.maxEventsPerDispatch = 1;
        LogicRuntime runtime(host, limits);
        const auto compiled = compileBoard("Hero", makeBoard());
        std::string error;
        CHECK(runtime.loadPrograms(compiled.programs, &error));
        CHECK(runtime.install("Hero", 1, &error).has_value());
        CHECK(runtime.install("Hero", 2, &error).has_value());
        runtime.beginFrame();
        runtime.dispatchStart();
        CHECK(host.calls.size() == 1);
        CHECK(!runtime.diagnostics().empty());
    }
    {
        Host host;
        LogicRuntime runtime(host);
        host.runtime = &runtime;
        const auto compiled = compileBoard("Hero", makeBoard());
        std::string error;
        CHECK(runtime.loadPrograms(compiled.programs, &error));
        CHECK(runtime.install("Hero", 1, &error).has_value());
        const auto second = runtime.install("Hero", 2, &error);
        CHECK(second.has_value());
        host.cancelOnVisible = second;
        runtime.beginFrame();
        runtime.dispatchStart();
        CHECK(host.calls.size() == 1); // snapshot token re-check observes unsubscribe
    }
    {
        Host host;
        host.failVisible = true;
        LogicRuntime runtime(host);
        std::string error;
        const LogicProgram isolated = customProgram("Isolated",
            " context:on_start('bad', function() context.self:set_visible(false) end)\n"
            " context:on_start('good', function() context.self:set_position(7, 8) end)");
        CHECK(runtime.loadPrograms({isolated}, &error));
        CHECK(runtime.install("Isolated", 1, &error).has_value());
        runtime.beginFrame();
        runtime.dispatchStart();
        CHECK(host.calls.size() == 2);
        CHECK(host.calls.back() == "position:1:7,8");
        CHECK(runtime.diagnostics().size() == 1);
    }
    {
        Host host;
        LogicRuntimeLimits limits;
        limits.maxMemoryBytes = 2u * 1024u * 1024u;
        LogicRuntime runtime(host, limits);
        std::string error;
        const LogicProgram memory = customProgram("Memory",
            " context:on_start('memory', function() local x = string.rep('x', 8388608) end)");
        CHECK(runtime.loadPrograms({memory}, &error));
        CHECK(runtime.install("Memory", 1, &error).has_value());
        runtime.beginFrame();
        runtime.dispatchStart();
        CHECK(!runtime.isEnabled());
        CHECK(!runtime.diagnostics().empty());
    }
}

static void testIsGroundedCondition() {
    // Registry: Is Grounded exists and is a Condition.
    const LogicBlockDescriptor* descriptor = findDescriptor(kIsGrounded);
    CHECK(descriptor != nullptr);
    CHECK(descriptor && descriptor->kind == BlockKind::Condition);

    LogicBlockDef condition = makeDefaultCondition();
    CHECK(condition.typeId == kIsGrounded);

    LogicBoardDef board;
    board.id = "logic:Grounded";
    LogicRuleDef rule = makeDefaultRule("rule-1");
    rule.trigger = {kKeyPressed, {{"key", LogicKey::Space}}};
    rule.conditions.push_back(makeClause(condition));
    board.rules.push_back(rule);

    // Validation: expected is a required boolean property.
    CHECK(validateBoard("Hero", board).empty());

    // Compiler: expected=true compiles to `== true`; declares the capability.
    LogicCompileResult trueCompiled = compileBoard("Hero", board);
    CHECK(trueCompiled.ok());
    CHECK(trueCompiled.programs[0].source.find("is_grounded() == true") != std::string::npos);
    const auto& trueFeatures = trueCompiled.programs[0].requiredFeatures;
    CHECK(std::find(trueFeatures.begin(), trueFeatures.end(), "platformer.grounded") != trueFeatures.end());

    // Compiler: expected=false compiles to a real negation, not a no-op.
    LogicBoardDef falseBoard = board;
    std::get<bool>(falseBoard.rules[0].conditions[0].block.properties[0].value) = false;
    LogicCompileResult falseCompiled = compileBoard("Hero", falseBoard);
    CHECK(falseCompiled.ok());
    CHECK(falseCompiled.programs[0].source.find("is_grounded() == false") != std::string::npos);

    // Zero conditions: actions run directly, no guard emitted.
    LogicCompileResult noCondCompiled = compileBoard("Hero", makeBoard());
    CHECK(noCondCompiled.programs[0].source.find("is_grounded") == std::string::npos);

    // Multiple conditions: deterministic AND.
    LogicBoardDef multi = board;
    multi.rules[0].conditions.push_back(makeClause(condition));
    LogicCompileResult multiCompiled = compileBoard("Hero", multi);
    CHECK(multiCompiled.ok());
    CHECK(multiCompiled.programs[0].source.find(" and ") != std::string::npos);

    // Runtime: grounded=false blocks the action, grounded=true runs it.
    {
        Host host;
        LogicRuntime runtime(host);
        std::string error;
        CHECK(runtime.loadPrograms(trueCompiled.programs, &error));
        CHECK(runtime.install("Hero", 1, &error).has_value());
        runtime.beginFrame();
        runtime.dispatchKeyPressed(LogicKey::Space);
        CHECK(host.calls.empty());
        host.grounded.insert(1);
        runtime.beginFrame();
        runtime.dispatchKeyPressed(LogicKey::Space);
        CHECK(!host.calls.empty());
    }

    // Compatibility: an unrecognized required feature is rejected up front,
    // not silently executed against a nonexistent Lua method.
    {
        Host host;
        LogicRuntime runtime(host);
        LogicProgram program = customProgram("Hero", " context:on_start('r', function() end)");
        program.requiredFeatures = {"future.unsupported"};
        std::string error;
        CHECK(!runtime.loadPrograms({program}, &error));
        CHECK(!error.empty());
    }
}

static void testIsGroundedAsEvent() {
    CHECK(isEventEligible(*findDescriptor(kIsGrounded)));
    CHECK(isEventEligible(*findDescriptor(kKeyDown)));
    CHECK(isEventEligible(*findDescriptor(kStateCompare)));
    CHECK(!isEventEligible(*findDescriptor(kOtherIsObjectType)));

    LogicBlockDef event = makeDefaultEventBlock(kIsGrounded);
    CHECK(event.typeId == kIsGrounded);

    LogicBoardDef board;
    board.id = "logic:GroundedEvent";
    LogicRuleDef rule = makeDefaultRule("rule-1");
    rule.trigger = event;
    rule.actions = {makeDefaultBlock(kJump, BlockKind::Action)};
    board.rules.push_back(rule);

    EntityDef owner;
    owner.platformerController = PlatformerControllerComponent{};
    CHECK(validateBoard("Hero", board, &owner).empty());

    LogicCompileResult compiled = compileBoard("Hero", board, &owner);
    CHECK(compiled.ok());
    CHECK(compiled.requiresTick);
    CHECK(compiled.programs[0].source.find("on_update") != std::string::npos);
    CHECK(compiled.programs[0].source.find("is_grounded() == true") != std::string::npos);
    CHECK(compiled.programs[0].source.find("platformer_jump") != std::string::npos);
}

static void testIsFallingAsEvent() {
    CHECK(isEventEligible(*findDescriptor(kIsFalling)));

    LogicBlockDef event = makeDefaultEventBlock(kIsFalling);
    CHECK(event.typeId == kIsFalling);

    LogicBoardDef board;
    board.id = "logic:FallingEvent";
    LogicRuleDef rule = makeDefaultRule("rule-1");
    rule.trigger = event;
    rule.actions = {makeDefaultBlock(kJump, BlockKind::Action)};
    board.rules.push_back(rule);

    EntityDef owner;
    owner.platformerController = PlatformerControllerComponent{};
    CHECK(validateBoard("Hero", board, &owner).empty());

    LogicCompileResult compiled = compileBoard("Hero", board, &owner);
    CHECK(compiled.ok());
    CHECK(compiled.requiresTick);
    CHECK(compiled.programs[0].source.find("on_update") != std::string::npos);
    CHECK(compiled.programs[0].source.find("is_falling() == true") != std::string::npos);
    const auto& features = compiled.programs[0].requiredFeatures;
    CHECK(std::find(features.begin(), features.end(), "platformer.falling") != features.end());

    // Runtime: falling=false blocks; falling=true runs the action once per tick.
    {
        Host host;
        LogicRuntime runtime(host);
        std::string error;
        CHECK(runtime.loadPrograms(compiled.programs, &error));
        CHECK(runtime.install("Hero", 1, &error).has_value());
        runtime.beginFrame();
        runtime.dispatchTick(1.f / 60.f);
        CHECK(host.calls.empty());
        host.falling.insert(1);
        runtime.beginFrame();
        runtime.dispatchTick(1.f / 60.f);
        CHECK(!host.calls.empty());
    }
}

static void testIsFallingMigratesToPlatformerState() {
    LogicBoardDef board;
    board.id = "logic:FallingMigrate";
    LogicRuleDef rule = makeDefaultRule("rule-1");
    rule.trigger = makeDefaultEventBlock(kIsFalling);
    rule.actions = {makeDefaultBlock(kJump, BlockKind::Action)};
    board.rules.push_back(rule);

    const nlohmann::json json = logicBoardToJson(board);
    LogicBoardDef loaded;
    CHECK(logicBoardFromJson(json, loaded).ok);
    CHECK(loaded.rules.size() == 1);
    CHECK(loaded.rules[0].trigger.typeId == kPlatformerMotionState);
    const LogicPropertyDef* state = findProperty(loaded.rules[0].trigger, "state");
    CHECK(state != nullptr);
    const auto* name = std::get_if<LogicStringValue>(&state->value);
    CHECK(name != nullptr);
    CHECK(name->value == "Falling");

    // expected == false is ambiguous — leave as Is Falling for manual repair.
    LogicBoardDef negated;
    negated.id = "logic:FallingNegated";
    LogicRuleDef negRule = makeDefaultRule("rule-1");
    negRule.trigger = makeDefaultEventBlock(kIsFalling);
    for (LogicPropertyDef& p : negRule.trigger.properties) {
        if (p.key == "expected") p.value = false;
    }
    negRule.actions = {makeDefaultBlock(kJump, BlockKind::Action)};
    negated.rules.push_back(negRule);
    const nlohmann::json negJson = logicBoardToJson(negated);
    LogicBoardDef negLoaded;
    CHECK(logicBoardFromJson(negJson, negLoaded).ok);
    CHECK(negLoaded.rules[0].trigger.typeId == kIsFalling);
}

static void testPlatformerMotionState() {
    CHECK(isEventEligible(*findDescriptor(kPlatformerMotionState)));
    CHECK(findDescriptor(kPlatformerMotionState)->activationKind
          == LogicTriggerActivationKind::Level);

    EntityDef owner;
    owner.platformerController = PlatformerControllerComponent{};

    auto makeMotionTrigger = [](const char* state) {
        LogicBlockDef trigger = makeDefaultEventBlock(kPlatformerMotionState);
        for (LogicPropertyDef& p : trigger.properties) {
            if (p.key == "state") p.value = LogicStringValue{state};
        }
        return trigger;
    };

    {
        LogicBoardDef board;
        board.id = "logic:MotionMoving";
        LogicRuleDef rule = makeDefaultRule("rule-1");
        rule.trigger = makeMotionTrigger("Moving");
        rule.actions = {makeDefaultBlock(kJump, BlockKind::Action)};
        board.rules.push_back(rule);
        CHECK(validateBoard("Hero", board, &owner).empty());
        LogicCompileResult compiled = compileBoard("Hero", board, &owner);
        CHECK(compiled.ok());
        CHECK(compiled.programs[0].source.find("on_update") != std::string::npos);
        CHECK(compiled.programs[0].source.find("platformer_state() == \"Moving\"")
              != std::string::npos);
        CHECK(compiled.programs[0].source.find("platformer_state() == \"Stopped\"")
              == std::string::npos);
        const auto& features = compiled.programs[0].requiredFeatures;
        CHECK(std::find(features.begin(), features.end(), "platformer.motion_state")
              != features.end());
    }
    {
        LogicBoardDef board;
        board.id = "logic:MotionStopped";
        LogicRuleDef rule = makeDefaultRule("rule-1");
        rule.trigger = makeMotionTrigger("Stopped");
        rule.actions = {makeDefaultBlock(kJump, BlockKind::Action)};
        board.rules.push_back(rule);
        LogicCompileResult compiled = compileBoard("Hero", board, &owner);
        CHECK(compiled.ok());
        CHECK(compiled.programs[0].source.find("platformer_state() == \"Stopped\"")
              != std::string::npos);
    }
    {
        LogicBoardDef board;
        board.id = "logic:MotionJumping";
        LogicRuleDef rule = makeDefaultRule("rule-1");
        rule.trigger = makeMotionTrigger("Jumping");
        rule.actions = {makeDefaultBlock(kJump, BlockKind::Action)};
        board.rules.push_back(rule);
        LogicCompileResult compiled = compileBoard("Hero", board, &owner);
        CHECK(compiled.ok());
        CHECK(compiled.programs[0].source.find("platformer_state() == \"Jumping\"")
              != std::string::npos);
    }
    {
        LogicBoardDef board;
        board.id = "logic:MotionFalling";
        LogicRuleDef rule = makeDefaultRule("rule-1");
        rule.trigger = makeMotionTrigger("Falling");
        rule.actions = {makeDefaultBlock(kJump, BlockKind::Action)};
        board.rules.push_back(rule);
        LogicCompileResult compiled = compileBoard("Hero", board, &owner);
        CHECK(compiled.ok());
        CHECK(compiled.programs[0].source.find("platformer_state() == \"Falling\"")
              != std::string::npos);
    }
    {
        LogicBoardDef board;
        board.id = "logic:MotionBad";
        LogicRuleDef rule = makeDefaultRule("rule-1");
        rule.trigger = makeMotionTrigger("Jogging");
        rule.actions = {makeDefaultBlock(kJump, BlockKind::Action)};
        board.rules.push_back(rule);
        const auto diags = validateBoard("Hero", board, &owner);
        CHECK(std::any_of(diags.begin(), diags.end(), [](const LogicDiagnostic& d) {
            return d.code == "LB_PLATFORMER_MOTION_STATE";
        }));
    }
    {
        LogicBoardDef board;
        board.id = "logic:MotionNoPc";
        LogicRuleDef rule = makeDefaultRule("rule-1");
        rule.trigger = makeMotionTrigger("Moving");
        rule.actions = {makeDefaultBlock(kJump, BlockKind::Action)};
        board.rules.push_back(rule);
        EntityDef bare;
        CHECK(!validateBoard("Hero", board, &bare).empty());
    }
    {
        LogicBoardDef board;
        board.id = "logic:MotionOnce";
        LogicRuleDef rule = makeDefaultRule("rule-1");
        rule.trigger = makeMotionTrigger("Moving");
        rule.executionMode = LogicExecutionMode::OncePerActivation;
        rule.actions = {makeDefaultBlock(kJump, BlockKind::Action)};
        board.rules.push_back(rule);

        LogicCompileResult compiled = compileBoard("Hero", board, &owner);
        CHECK(compiled.ok());
        CHECK(compiled.programs[0].source.find("should_execute") != std::string::npos);

        Host host;
        LogicRuntime runtime(host);
        std::string error;
        CHECK(runtime.loadPrograms(compiled.programs, &error));
        CHECK(runtime.install("Hero", 1, &error).has_value());

        auto jumpCount = [&]() {
            return std::count_if(host.calls.begin(), host.calls.end(),
                [](const std::string& c) { return c.rfind("platformer_jump:", 0) == 0; });
        };

        host.platformerStates[1] = PlatformerState::Moving;
        for (int i = 0; i < 100; ++i) {
            runtime.beginFrame();
            runtime.dispatchTick(1.f / 60.f);
        }
        CHECK(jumpCount() == 1);

        host.calls.clear();
        host.platformerStates[1] = PlatformerState::Stopped;
        runtime.beginFrame();
        runtime.dispatchTick(1.f / 60.f);
        CHECK(jumpCount() == 0);

        host.platformerStates[1] = PlatformerState::Moving;
        runtime.beginFrame();
        runtime.dispatchTick(1.f / 60.f);
        CHECK(jumpCount() == 1);
    }
    CHECK(findDescriptor(kPlatformerMotionState)->displayName
          == std::string("Platformer State"));
    CHECK(findDescriptor(kIsFalling)->catalogHidden);
}

static void testIsVisibleAsEvent() {
    CHECK(isEventEligible(*findDescriptor(kIsVisible)));
    const LogicBlockDescriptor* descriptor = findDescriptor(kIsVisible);
    CHECK(descriptor != nullptr);
    CHECK(descriptor->kind == BlockKind::Condition);
    CHECK(descriptor->requiredFeature == std::string("entity.visibility"));

    LogicBlockDef event = makeDefaultEventBlock(kIsVisible);
    CHECK(event.typeId == kIsVisible);

    LogicBoardDef board;
    board.id = "logic:VisibleEvent";
    LogicRuleDef rule = makeDefaultRule("rule-1");
    rule.trigger = event;
    LogicBlockDef moveBy = makeDefaultBlock(kTranslateBy, BlockKind::Action);
    for (LogicPropertyDef& p : moveBy.properties) {
        if (p.key == "offset") p.value = Vec2{5.f, 0.f};
    }
    rule.actions = {moveBy};
    board.rules.push_back(rule);

    CHECK(validateBoard("Hero", board).empty());
    LogicCompileResult compiled = compileBoard("Hero", board);
    CHECK(compiled.ok());
    CHECK(compiled.requiresTick);
    CHECK(compiled.programs[0].source.find("on_update") != std::string::npos);
    CHECK(compiled.programs[0].source.find("is_visible() == true") != std::string::npos);
    CHECK(compiled.programs[0].source.find("translate(5") != std::string::npos);

    Host host;
    host.visible[1] = true;
    LogicRuntime runtime(host);
    std::string error;
    CHECK(runtime.loadPrograms(compiled.programs, &error));
    CHECK(runtime.install("Hero", 1, &error).has_value());
    runtime.beginFrame();
    runtime.dispatchTick(1.f / 60.f);
    CHECK(std::any_of(host.calls.begin(), host.calls.end(),
        [](const std::string& c) { return c == "is_visible:1"; }));
    CHECK(std::any_of(host.calls.begin(), host.calls.end(),
        [](const std::string& c) { return c == "translate:1:5,0"; }));

    host.calls.clear();
    host.visible[1] = false;
    runtime.beginFrame();
    runtime.dispatchTick(1.f / 60.f);
    CHECK(std::any_of(host.calls.begin(), host.calls.end(),
        [](const std::string& c) { return c == "is_visible:1"; }));
    CHECK(std::none_of(host.calls.begin(), host.calls.end(),
        [](const std::string& c) { return c.rfind("translate:", 0) == 0; }));
}

static void testSpriteSetFacingAction() {
    const LogicBlockDescriptor* descriptor = findDescriptor(kSpriteSetFacing);
    CHECK(descriptor != nullptr);
    CHECK(descriptor->kind == BlockKind::Action);
    CHECK(descriptor->displayName == std::string("Flip Horizontal"));
    CHECK(descriptor->properties.size() == 1);
    CHECK(descriptor->properties[0].semantic == LogicPropertySemantic::SpriteFacing);
    CHECK(descriptor->properties[0].options
          == std::vector<std::string>({"Left", "Right"}));

    LogicBoardDef board;
    board.id = "logic:Facing";
    LogicRuleDef rule = makeDefaultRule("rule-1");
    LogicBlockDef facing = makeDefaultBlock(kSpriteSetFacing, BlockKind::Action);
    for (LogicPropertyDef& p : facing.properties) {
        if (p.key == "facing") p.value = LogicStringValue{"Left"};
    }
    rule.actions = {facing};
    board.rules.push_back(rule);

    CHECK(validateBoard("Hero", board).empty());
    LogicCompileResult compiled = compileBoard("Hero", board);
    CHECK(compiled.ok());
    CHECK(compiled.programs[0].source.find("set_flip_x(true)") != std::string::npos);

    LogicBoardDef badBoard = board;
    for (LogicPropertyDef& p : badBoard.rules[0].actions[0].properties) {
        if (p.key == "facing") p.value = LogicStringValue{"Up"};
    }
    CHECK(!compileBoard("Hero", badBoard).ok());

    Host host;
    LogicRuntime runtime(host);
    std::string error;
    CHECK(runtime.loadPrograms(compiled.programs, &error));
    CHECK(runtime.install("Hero", 1, &error).has_value());
    runtime.beginFrame();
    runtime.dispatchStart();
    CHECK(std::find(host.calls.begin(), host.calls.end(), "flip_x:1:1") != host.calls.end());
}

static void testPlatformerMoveHorizontalDirection() {
    const LogicBlockDescriptor* descriptor = findDescriptor(kMoveHorizontal);
    CHECK(descriptor != nullptr);
    CHECK(descriptor->properties.size() == 1);
    CHECK(descriptor->properties[0].key == "direction");
    CHECK(descriptor->properties[0].semantic == LogicPropertySemantic::PlatformerDirection);
    CHECK(descriptor->properties[0].options
          == std::vector<std::string>({"Left", "Right"}));

    LogicBoardDef board;
    board.id = "logic:Move";
    LogicRuleDef rule = makeDefaultRule("rule-1");
    LogicBlockDef move = makeDefaultBlock(kMoveHorizontal, BlockKind::Action);
    for (LogicPropertyDef& p : move.properties) {
        if (p.key == "direction") p.value = LogicStringValue{"Left"};
    }
    rule.actions = {move};
    // Owner needs PlatformerController for availability/validation of required component.
    EntityDef owner;
    owner.platformerController = PlatformerControllerComponent{};
    board.rules.push_back(rule);

    CHECK(validateBoard("Hero", board, &owner).empty());
    LogicCompileResult compiled = compileBoard("Hero", board, &owner);
    CHECK(compiled.ok());
    CHECK(compiled.programs[0].source.find("platformer_move(-1)") != std::string::npos);

    LogicBoardDef rightBoard = board;
    for (LogicPropertyDef& p : rightBoard.rules[0].actions[0].properties) {
        if (p.key == "direction") p.value = LogicStringValue{"Right"};
    }
    LogicCompileResult rightCompiled = compileBoard("Hero", rightBoard, &owner);
    CHECK(rightCompiled.ok());
    CHECK(rightCompiled.programs[0].source.find("platformer_move(1)") != std::string::npos);

    LogicBoardDef badBoard = board;
    for (LogicPropertyDef& p : badBoard.rules[0].actions[0].properties) {
        if (p.key == "direction") p.value = LogicStringValue{"Up"};
    }
    CHECK(!compileBoard("Hero", badBoard, &owner).ok());

    // Legacy numeric axis still compiles.
    LogicBoardDef legacy;
    legacy.id = "logic:LegacyMove";
    LogicRuleDef legacyRule = makeDefaultRule("rule-1");
    legacyRule.actions = {LogicBlockDef{kMoveHorizontal, {{"axis", -1.0}}}};
    legacy.rules.push_back(legacyRule);
    LogicCompileResult legacyCompiled = compileBoard("Hero", legacy, &owner);
    CHECK(legacyCompiled.ok());
    CHECK(legacyCompiled.programs[0].source.find("platformer_move(-1)") != std::string::npos);

    Host host;
    LogicRuntime runtime(host);
    std::string error;
    CHECK(runtime.loadPrograms(compiled.programs, &error));
    CHECK(runtime.install("Hero", 1, &error).has_value());
    runtime.beginFrame();
    runtime.dispatchStart();
    const std::string expectedMove =
        "platformer_move:1:" + std::to_string(static_cast<float>(-1));
    CHECK(std::find(host.calls.begin(), host.calls.end(), expectedMove) != host.calls.end());
}

static LogicBlockDef makeStateCompareCondition(double value) {
    LogicBlockDef condition = makeDefaultBlock(kStateCompare, BlockKind::Condition);
    for (LogicPropertyDef& property : condition.properties) {
        if (property.key == "key") property.value = LogicVariableReference{"score"};
        else if (property.key == "op") property.value = LogicStringValue{">="};
        else if (property.key == "value") property.value = value;
    }
    return condition;
}

static LogicBlockDef makeKeyDownCondition(LogicKey key) {
    LogicBlockDef condition = makeDefaultBlock(kKeyDown, BlockKind::Condition);
    for (LogicPropertyDef& property : condition.properties) {
        if (property.key == "key") property.value = key;
    }
    return condition;
}

static LogicBoardDef makeOperatorBoard(std::vector<LogicConditionClause> conditions) {
    LogicBoardDef board;
    board.id = "logic:Operators";
    LogicRuleDef rule = makeDefaultRule("operator-rule");
    rule.trigger = {kKeyPressed, {{"key", LogicKey::Space}}};
    rule.conditions = std::move(conditions);
    board.rules.push_back(std::move(rule));
    return board;
}

static void testDescriptorSemanticMetadataConsistency() {
    const auto defaultMatchesKind = [](const LogicPropertyDescriptor& property) {
        switch (property.valueKind) {
        case LogicValueKind::Bool:
            return std::holds_alternative<bool>(property.defaultValue);
        case LogicValueKind::Integer:
            return std::holds_alternative<int64_t>(property.defaultValue);
        case LogicValueKind::Number:
            return std::holds_alternative<double>(property.defaultValue);
        case LogicValueKind::String:
            return std::holds_alternative<LogicStringValue>(property.defaultValue);
        case LogicValueKind::Vec2:
            return std::holds_alternative<Vec2>(property.defaultValue);
        case LogicValueKind::Asset:
            return std::holds_alternative<LogicAssetReference>(property.defaultValue);
        case LogicValueKind::Entity:
            return std::holds_alternative<LogicEntityReference>(property.defaultValue);
        case LogicValueKind::Variable:
            return std::holds_alternative<LogicVariableReference>(property.defaultValue);
        case LogicValueKind::Key:
            return std::holds_alternative<LogicKey>(property.defaultValue);
        }
        return false;
    };

    std::unordered_set<LogicBlockTypeId> typeIds;
    for (const LogicBlockDescriptor& block : registry()) {
        CHECK(!block.typeId.empty());
        CHECK(!block.categoryId.empty());
        CHECK(!block.displayName.empty());
        CHECK(!block.description.empty());
        CHECK(typeIds.insert(block.typeId).second);

        std::unordered_set<std::string> propertyKeys;
        for (const LogicPropertyDescriptor& property : block.properties) {
            CHECK(!property.key.empty());
            CHECK(!propertyDisplayName(property).empty());
            CHECK(propertyKeys.insert(property.key).second);
            CHECK(defaultMatchesKind(property));

            if (property.valueKind == LogicValueKind::Key)
                CHECK(property.semantic == LogicPropertySemantic::LogicKey);
            if (property.valueKind == LogicValueKind::Variable)
                CHECK(property.semantic == LogicPropertySemantic::GlobalVariable);
            if (property.valueKind == LogicValueKind::Entity)
                CHECK(property.semantic == LogicPropertySemantic::HiddenSelfTarget);
            if (property.semantic == LogicPropertySemantic::ExpectedBool) {
                CHECK(property.key == "expected");
                CHECK(property.valueKind == LogicValueKind::Bool);
            }
            if (property.semantic == LogicPropertySemantic::ObjectTypeReference)
                CHECK(property.key == "objectTypeId");
            if (property.semantic == LogicPropertySemantic::SpriteAnimationAsset)
                CHECK(property.key == "animationAssetId");
            if (property.semantic == LogicPropertySemantic::AnimationClip)
                CHECK(property.key == "clipId");
            if (property.semantic == LogicPropertySemantic::StaticAudioAsset)
                CHECK(property.key == "audioAssetId");
            if (property.semantic == LogicPropertySemantic::CompareOperator) {
                CHECK(block.typeId == kStateCompare);
                CHECK(property.key == "op");
                CHECK(property.options
                      == std::vector<std::string>({"==", "!=", "<", "<=", ">", ">="}));
            } else if (property.semantic == LogicPropertySemantic::TopDownDirection) {
                CHECK(block.typeId == kTopDownMove);
                CHECK(property.key == "direction");
                CHECK(property.options
                      == std::vector<std::string>({"Left", "Right", "Up", "Down"}));
            } else if (property.semantic == LogicPropertySemantic::SpriteFacing) {
                CHECK(block.typeId == kSpriteSetFacing);
                CHECK(property.key == "facing");
                CHECK(property.options == std::vector<std::string>({"Left", "Right"}));
            } else if (property.semantic == LogicPropertySemantic::PlatformerDirection) {
                CHECK(block.typeId == kMoveHorizontal);
                CHECK(property.key == "direction");
                CHECK(property.options == std::vector<std::string>({"Left", "Right"}));
            } else if (property.semantic == LogicPropertySemantic::PlatformerMotionState) {
                CHECK(block.typeId == kPlatformerMotionState);
                CHECK(property.key == "state");
                CHECK(property.options
                      == std::vector<std::string>(
                          {"Stopped", "Moving", "Jumping", "Falling"}));
            } else {
                CHECK(property.options.empty());
            }
            if (property.allowEmpty) {
                CHECK(property.semantic == LogicPropertySemantic::ObjectTypeReference);
                CHECK(block.typeId == kCollisionEnter || block.typeId == kCollisionExit);
            }

            if (property.valueKind == LogicValueKind::Number)
                CHECK(property.numberConstraint != LogicNumberConstraint::None);
            if (property.key == "seconds" || property.key == "speed")
                CHECK(property.numberConstraint == LogicNumberConstraint::Positive);
            if (property.key == "volume")
                CHECK(property.numberConstraint == LogicNumberConstraint::UnitInterval);
            if (property.key == "axis")
                CHECK(property.numberConstraint == LogicNumberConstraint::NormalizedAxis);
            if (property.key == "scale")
                CHECK(property.numberConstraint == LogicNumberConstraint::PositiveVec2);
        }
    }
}

static void testConditionOperators() {
    const LogicBlockDef grounded = makeDefaultCondition();
    const LogicBlockDef compare = makeStateCompareCondition(5.0);
    const LogicBlockDef keyDownA = makeKeyDownCondition(LogicKey::A);
    const LogicBlockDef keyDownB = makeKeyDownCondition(LogicKey::B);

    const auto compiledSource = [](std::vector<LogicConditionClause> conditions) {
        const LogicCompileResult result = compileBoard(
            "Operators", makeOperatorBoard(std::move(conditions)));
        CHECK(result.ok());
        return result.ok() ? result.programs[0].source : std::string{};
    };
    CHECK(compiledSource({makeClause(grounded)}).find(
        "if (context.self:is_grounded() == true) then") != std::string::npos);
    CHECK(compiledSource({makeClause(grounded, LogicConditionJoin::And, true)}).find(
        "if (not (context.self:is_grounded() == true)) then") != std::string::npos);
    CHECK(compiledSource({makeClause(grounded), makeClause(keyDownA)}).find(
        "if (context.self:is_grounded() == true and context:is_key_down(\"A\")) then")
        != std::string::npos);
    CHECK(compiledSource({makeClause(grounded),
                          makeClause(keyDownA, LogicConditionJoin::Or)}).find(
        "if (context.self:is_grounded() == true) or "
        "(context:is_key_down(\"A\")) then") != std::string::npos);
    CHECK(compiledSource({makeClause(grounded),
                          makeClause(keyDownA, LogicConditionJoin::Or, true)}).find(
        "if (context.self:is_grounded() == true) or "
        "(not (context:is_key_down(\"A\"))) then") != std::string::npos);

    LogicBoardDef grouped = makeOperatorBoard({
        makeClause(grounded),
        makeClause(keyDownA, LogicConditionJoin::And),
        makeClause(compare, LogicConditionJoin::Or),
        makeClause(keyDownB, LogicConditionJoin::And, true),
    });
    const LogicCompileResult groupedCompiled = compileBoard("Operators", grouped);
    CHECK(groupedCompiled.ok());
    CHECK(groupedCompiled.programs[0].source.find(
        "(context.self:is_grounded() == true and context:is_key_down(\"A\")) or "
        "(context:state_compare_number(\"score\", \">=\", 5) and "
        "not (context:is_key_down(\"B\")))") != std::string::npos);

    LogicBoardDef andBeforeOr = makeOperatorBoard({
        makeClause(grounded),
        makeClause(compare, LogicConditionJoin::And),
        makeClause(keyDownA, LogicConditionJoin::Or),
    });
    LogicCompileResult compiled = compileBoard("Operators", andBeforeOr);
    CHECK(compiled.ok());
    {
        Host host;
        host.declareNumber("score", 0.0);
        host.keyDown = true;
        LogicRuntime runtime(host);
        std::string error;
        CHECK(runtime.loadPrograms(compiled.programs, &error));
        CHECK(runtime.install("Operators", 1, &error).has_value());
        runtime.beginFrame();
        runtime.dispatchKeyPressed(LogicKey::Space);
        CHECK(host.calls.size() == 1); // false AND false OR true
    }

    LogicBoardDef orBeforeAnd = makeOperatorBoard({
        makeClause(grounded),
        makeClause(compare, LogicConditionJoin::Or),
        makeClause(keyDownA, LogicConditionJoin::And),
    });
    compiled = compileBoard("Operators", orBeforeAnd);
    CHECK(compiled.ok());
    {
        Host host;
        host.declareNumber("score", 10.0);
        LogicRuntime runtime(host);
        std::string error;
        CHECK(runtime.loadPrograms(compiled.programs, &error));
        CHECK(runtime.install("Operators", 1, &error).has_value());
        runtime.beginFrame();
        runtime.dispatchKeyPressed(LogicKey::Space);
        CHECK(host.calls.empty()); // false OR true AND false
        host.keyDown = true;
        runtime.beginFrame();
        runtime.dispatchKeyPressed(LogicKey::Space);
        CHECK(host.calls.size() == 1); // false OR true AND true
    }

    LogicBoardDef invalid = grouped;
    invalid.rules[0].conditions[0].joinBefore = LogicConditionJoin::Or;
    const auto invalidDiagnostics = validateBoard("Operators", invalid);
    CHECK(std::any_of(invalidDiagnostics.begin(), invalidDiagnostics.end(),
        [](const LogicDiagnostic& diagnostic) {
            return diagnostic.code == "LB_FIRST_CONDITION_JOIN";
        }));
    CHECK(!compileBoard("Operators", invalid).ok());
}

static void testPlaySoundAction() {
    // Registry: Play Sound exists, is an Action, category=audio, volume default=1.
    const LogicBlockDescriptor* descriptor = findDescriptor(kAudioPlaySound);
    CHECK(descriptor != nullptr);
    if (descriptor) {
        CHECK(descriptor->kind == BlockKind::Action);
        CHECK(descriptor->categoryId == "audio");
        CHECK(descriptor->requiredFeature == "audio.play_sound");
        const auto volumeIt = std::find_if(descriptor->properties.begin(), descriptor->properties.end(),
            [](const LogicPropertyDescriptor& p) { return p.key == "volume"; });
        CHECK(volumeIt != descriptor->properties.end());
        CHECK(volumeIt != descriptor->properties.end() && std::get<double>(volumeIt->defaultValue) == 1.0);
    }

    LogicBlockDef action = makeDefaultBlock(kAudioPlaySound, BlockKind::Action);
    CHECK(action.typeId == kAudioPlaySound);

    ProjectDoc project;
    AudioAssetDef staticAsset;
    staticAsset.assetId = "jump.wav";
    staticAsset.sourcePath = "audio/jump.wav";
    staticAsset.loadMode = AudioLoadMode::StaticSound;
    project.audioAssets.push_back(staticAsset);
    AudioAssetDef streamAsset;
    streamAsset.assetId = "theme.ogg";
    streamAsset.sourcePath = "audio/theme.ogg";
    streamAsset.loadMode = AudioLoadMode::Stream;
    project.audioAssets.push_back(streamAsset);

    const auto makeBoardWith = [](const std::string& assetId, double volume) {
        LogicBoardDef board;
        board.id = "logic:Audio";
        LogicRuleDef rule = makeDefaultRule("rule-1");
        LogicBlockDef play = makeDefaultBlock(kAudioPlaySound, BlockKind::Action);
        for (LogicPropertyDef& p : play.properties) {
            if (p.key == "audioAssetId") p.value = LogicAssetReference{assetId};
            else if (p.key == "volume") p.value = volume;
        }
        rule.actions = {play};
        board.rules.push_back(rule);
        return board;
    };
    const auto hasDiagnostic = [](const std::vector<LogicDiagnostic>& diagnostics, const char* code) {
        return std::any_of(diagnostics.begin(), diagnostics.end(),
            [&](const LogicDiagnostic& d) { return d.code == code; });
    };

    // Empty is a first-class authoring draft, not a missing non-empty ID.
    // The same core policy remains strict for compiler/Play/export.
    {
        const LogicBoardDef draft = makeBoardWith("", 1.0);
        const auto authoring = validateBoard(
            "Hero", draft, nullptr, &project, LogicValidationPurpose::AuthoringDiagnostics);
        CHECK(hasDiagnostic(authoring, "LB_AUDIO_ASSET_REFERENCE"));
        CHECK(std::none_of(authoring.begin(), authoring.end(),
            [](const LogicDiagnostic& diagnostic) {
                return diagnostic.severity == DiagnosticSeverity::Error;
            }));
        const auto executable = validateBoard(
            "Hero", draft, nullptr, &project, LogicValidationPurpose::Executable);
        CHECK(std::any_of(executable.begin(), executable.end(),
            [](const LogicDiagnostic& diagnostic) {
                return diagnostic.code == "LB_AUDIO_ASSET_REFERENCE"
                    && diagnostic.severity == DiagnosticSeverity::Error;
            }));
        CHECK(!compileBoard("Hero", draft, nullptr, &project).ok());
    }
    // Valid: existing StaticSound asset, volume in range.
    {
        const LogicBoardDef board = makeBoardWith("jump.wav", 0.8);
        CHECK(validateBoard("Hero", board, nullptr, &project).empty());
        LogicCompileResult compiled = compileBoard("Hero", board, nullptr, &project);
        CHECK(compiled.ok());
        CHECK(compiled.programs[0].source.find("play_sound(\"jump.wav\", 0.8)") != std::string::npos);
        const auto& features = compiled.programs[0].requiredFeatures;
        CHECK(std::find(features.begin(), features.end(), "audio.play_sound") != features.end());
    }

    // Missing asset.
    CHECK(hasDiagnostic(validateBoard("Hero", makeBoardWith("does-not-exist", 1.0), nullptr, &project),
                       "LB_AUDIO_ASSET_REFERENCE"));

    // Stream asset rejected — Play Sound requires StaticSound.
    CHECK(hasDiagnostic(validateBoard("Hero", makeBoardWith("theme.ogg", 1.0), nullptr, &project),
                       "LB_AUDIO_REQUIRES_STATIC"));

    // Volume out of range (both directions) and non-finite.
    CHECK(hasDiagnostic(validateBoard("Hero", makeBoardWith("jump.wav", 1.5), nullptr, &project),
                       "LB_AUDIO_VOLUME_RANGE"));
    CHECK(hasDiagnostic(validateBoard("Hero", makeBoardWith("jump.wav", -0.1), nullptr, &project),
                       "LB_AUDIO_VOLUME_RANGE"));
    CHECK(hasDiagnostic(
        validateBoard("Hero", makeBoardWith("jump.wav", std::numeric_limits<double>::quiet_NaN()),
                     nullptr, &project),
        "LB_NON_FINITE"));

    // Logic-runtime Lua binding: dispatches host.playSound exactly once.
    {
        LogicCompileResult compiled =
            compileBoard("Hero", makeBoardWith("jump.wav", 0.8), nullptr, &project);
        CHECK(compiled.ok());
        Host host;
        LogicRuntime runtime(host);
        std::string error;
        CHECK(runtime.loadPrograms(compiled.programs, &error));
        CHECK(runtime.install("Hero", 1, &error).has_value());
        runtime.beginFrame();
        runtime.dispatchStart();
        const auto playSoundCalls = std::count_if(host.calls.begin(), host.calls.end(),
            [](const std::string& call) { return call.rfind("play_sound:", 0) == 0; });
        CHECK(playSoundCalls == 1);
        CHECK(!host.calls.empty() && host.calls.back().rfind("play_sound:1:jump.wav:", 0) == 0);
    }

    // Compatibility: a runtime that predates audio.play_sound rejects the
    // program up front rather than dispatching to a nonexistent Lua method.
    {
        Host host;
        LogicRuntime runtime(host);
        LogicProgram program = customProgram("Hero", " context:on_start('r', function() end)");
        program.requiredFeatures = {"audio.play_sound_v2_future"};
        std::string error;
        CHECK(!runtime.loadPrograms({program}, &error));
        CHECK(!error.empty());
    }
}

static void testSceneActions() {
    // Registry: both scene actions exist, category=scene, feature-gated.
    const LogicBlockDescriptor* restart = findDescriptor(kSceneRestart);
    const LogicBlockDescriptor* goTo = findDescriptor(kSceneGoTo);
    CHECK(restart != nullptr && restart->kind == BlockKind::Action);
    CHECK(goTo != nullptr && goTo->kind == BlockKind::Action);
    CHECK(restart && restart->categoryId == "scene"
          && restart->requiredFeature == "scene.restart" && restart->properties.empty());
    CHECK(goTo && goTo->categoryId == "scene" && goTo->requiredFeature == "scene.go_to");
    CHECK(goTo && goTo->properties.size() == 1
          && goTo->properties[0].semantic == LogicPropertySemantic::SceneReference);

    ProjectDoc project;
    SceneDef sceneOne; sceneOne.id = "scene-1";
    SceneDef sceneTwo; sceneTwo.id = "scene-2";
    project.scenes.emplace(sceneOne.id, sceneOne);
    project.scenes.emplace(sceneTwo.id, sceneTwo);

    const auto makeGoToBoard = [](const std::string& sceneId) {
        LogicBoardDef board;
        board.id = "logic:Scene";
        LogicRuleDef rule = makeDefaultRule("rule-1");
        LogicBlockDef action = makeDefaultBlock(kSceneGoTo, BlockKind::Action);
        action.properties[0].value = LogicStringValue{sceneId};
        rule.actions = {action};
        board.rules.push_back(rule);
        return board;
    };
    const auto hasDiagnostic = [](const std::vector<LogicDiagnostic>& diagnostics, const char* code) {
        return std::any_of(diagnostics.begin(), diagnostics.end(),
            [&](const LogicDiagnostic& d) { return d.code == code; });
    };

    // Valid reference compiles to the deferred context call + feature gates.
    {
        const LogicBoardDef board = makeGoToBoard("scene-2");
        CHECK(validateBoard("Hero", board, nullptr, &project).empty());
        LogicCompileResult compiled = compileBoard("Hero", board, nullptr, &project);
        CHECK(compiled.ok());
        CHECK(compiled.programs[0].source.find("context:scene_go_to(\"scene-2\")")
              != std::string::npos);
        const auto& features = compiled.programs[0].requiredFeatures;
        CHECK(std::find(features.begin(), features.end(), "scene.go_to") != features.end());
    }
    {
        LogicBoardDef board;
        board.id = "logic:Scene";
        LogicRuleDef rule = makeDefaultRule("rule-1");
        rule.actions = {makeDefaultBlock(kSceneRestart, BlockKind::Action)};
        board.rules.push_back(rule);
        CHECK(validateBoard("Hero", board, nullptr, &project).empty());
        LogicCompileResult compiled = compileBoard("Hero", board, nullptr, &project);
        CHECK(compiled.ok());
        CHECK(compiled.programs[0].source.find("context:scene_restart()") != std::string::npos);
        const auto& features = compiled.programs[0].requiredFeatures;
        CHECK(std::find(features.begin(), features.end(), "scene.restart") != features.end());
    }

    // Unknown / empty scene: semantic LB_SCENE_REFERENCE, compile blocked.
    CHECK(hasDiagnostic(validateBoard("Hero", makeGoToBoard("scene-missing"), nullptr, &project),
                        "LB_SCENE_REFERENCE"));
    CHECK(!compileBoard("Hero", makeGoToBoard("scene-missing"), nullptr, &project).ok());
    CHECK(hasDiagnostic(validateBoard("Hero", makeGoToBoard(""), nullptr, &project),
                        "LB_SCENE_REFERENCE"));

    // Runtime binding: the host receives the queued request exactly once.
    {
        LogicCompileResult compiled =
            compileBoard("Hero", makeGoToBoard("scene-2"), nullptr, &project);
        CHECK(compiled.ok());
        Host host;
        LogicRuntime runtime(host);
        std::string error;
        CHECK(runtime.loadPrograms(compiled.programs, &error));
        CHECK(runtime.install("Hero", 1, &error).has_value());
        runtime.beginFrame();
        runtime.dispatchStart();
        CHECK(std::count(host.calls.begin(), host.calls.end(),
                         std::string("scene_go_to:scene-2")) == 1);
    }
    {
        Host host;
        LogicRuntime runtime(host);
        std::string error;
        CHECK(runtime.loadPrograms(
            {customProgram("Hero",
                           "  context:on_start('r', function()\n"
                           "    context:scene_restart()\n  end)")}, &error));
        CHECK(runtime.install("Hero", 1, &error).has_value());
        runtime.beginFrame();
        runtime.dispatchStart();
        CHECK(std::count(host.calls.begin(), host.calls.end(),
                         std::string("scene_restart")) == 1);
    }

    // Empty scene id at runtime: host rejects, the rule is disabled with a
    // diagnostic instead of corrupting the dispatcher.
    {
        Host host;
        LogicRuntime runtime(host);
        std::string error;
        CHECK(runtime.loadPrograms(
            {customProgram("Hero",
                           "  context:on_start('r', function()\n"
                           "    context:scene_go_to('')\n  end)")}, &error));
        CHECK(runtime.install("Hero", 1, &error).has_value());
        runtime.beginFrame();
        runtime.dispatchStart();
        CHECK(host.calls.empty());
        CHECK(!runtime.diagnostics().empty());
    }

    // Compatibility: an older runtime predating the scene features rejects
    // the program up front (same contract testPlaySoundAction locks in).
}

static void testDestroyOtherAction() {
    // Registry: Action, category=collision, feature-gated, EventOther-scoped,
    // no properties.
    const LogicBlockDescriptor* descriptor = findDescriptor(kDestroyOther);
    CHECK(descriptor != nullptr);
    if (descriptor) {
        CHECK(descriptor->kind == BlockKind::Action);
        CHECK(descriptor->categoryId == "collision");
        CHECK(descriptor->requiredFeature == "collision.destroy_other");
        CHECK(descriptor->properties.empty());
        CHECK(descriptor->requiredContext.size() == 1
              && descriptor->requiredContext[0] == LogicContextCapability::EventOther);
    }

    EntityDef owner;
    owner.name = "Hero";
    owner.className = "Hero";

    // Availability mirrors Other Is Object Type: collision triggers provide
    // EventOther; anything else greys the entry out with a reason.
    const LogicBlockDescriptor* collisionTrigger = findDescriptor(kCollisionEnter);
    const LogicBlockDescriptor* keyTrigger = findDescriptor(kKeyPressed);
    CHECK(blockAvailability(owner, *descriptor, collisionTrigger).compatible);
    const LogicBlockAvailability underKey = blockAvailability(owner, *descriptor, keyTrigger);
    CHECK(!underKey.compatible);
    CHECK(!underKey.reason.empty());

    const auto makeBoardWith = [](const LogicBlockTypeId& triggerId) {
        LogicBoardDef board;
        board.id = "logic:Hero";
        LogicRuleDef rule = makeDefaultRule("rule-1");
        rule.trigger = makeDefaultBlock(triggerId, BlockKind::Trigger);
        rule.actions = {makeDefaultBlock(kDestroyOther, BlockKind::Action)};
        board.rules.push_back(rule);
        return board;
    };

    // Valid placement compiles to the deferred context call + feature gate.
    {
        const LogicBoardDef board = makeBoardWith(kCollisionEnter);
        CHECK(validateBoard("Hero", board, &owner).empty());
        LogicCompileResult compiled = compileBoard("Hero", board, &owner);
        CHECK(compiled.ok());
        CHECK(compiled.programs[0].source.find("context:destroy_other(other)")
              != std::string::npos);
        const auto& features = compiled.programs[0].requiredFeatures;
        CHECK(std::find(features.begin(), features.end(), "collision.destroy_other")
              != features.end());
    }

    // Invalid placement (no EventOther): semantic LB_INCOMPATIBLE_BLOCK,
    // compile blocked; StructuralCommit stays loadable (ADR-0013).
    {
        const LogicBoardDef board = makeBoardWith(kKeyPressed);
        CHECK(validateBoard("Hero", board, &owner,
                            nullptr, LogicValidationPurpose::StructuralCommit).empty());
        const auto diagnostics = validateBoard("Hero", board, &owner);
        CHECK(std::any_of(diagnostics.begin(), diagnostics.end(),
            [](const LogicDiagnostic& d) { return d.code == "LB_INCOMPATIBLE_BLOCK"; }));
        CHECK(!compileBoard("Hero", board, &owner).ok());
    }

    // Runtime binding: the collided entity is destroyed exactly once, and the
    // owner itself is untouched.
    {
        LogicCompileResult compiled = compileBoard("Hero", makeBoardWith(kCollisionEnter), &owner);
        CHECK(compiled.ok());
        Host host;
        LogicRuntime runtime(host);
        std::string error;
        CHECK(runtime.loadPrograms(compiled.programs, &error));
        CHECK(runtime.install("Hero", 1, &error).has_value());
        runtime.beginFrame();
        runtime.dispatchCollisionEnter(1, 42);
        CHECK(std::count(host.calls.begin(), host.calls.end(),
                         std::string("destroy:42")) == 1);
        CHECK(std::count(host.calls.begin(), host.calls.end(),
                         std::string("destroy:1")) == 0);
    }
}

static void testCombinedGameplaySmoke() {
    ProjectDoc project;
    EntityDef hero;
    hero.className = "Hero";
    hero.platformerController = PlatformerControllerComponent{};
    hero.spriteRenderer = SpriteRendererComponent{{}, true};
    hero.spriteAnimator = SpriteAnimatorComponent{"hero-animation", "jump", true, 1.f};

    LogicBoardDef board;
    board.id = "logic:Hero";
    LogicRuleDef rule = makeDefaultRule("jump-feedback");
    rule.trigger = makeDefaultBlock(kKeyPressed, BlockKind::Trigger);
    for (LogicPropertyDef& property : rule.trigger.properties) {
        if (property.key == "key") property.value = LogicKey::Space;
    }
    rule.conditions = {makeClause(makeDefaultBlock(kIsGrounded, BlockKind::Condition))};
    LogicBlockDef playClip = makeDefaultBlock(kAnimationPlayClip, BlockKind::Action);
    for (LogicPropertyDef& property : playClip.properties) {
        if (property.key == "animationAssetId")
            property.value = LogicAssetReference{"hero-animation"};
        else if (property.key == "clipId")
            property.value = LogicStringValue{"jump"};
    }
    LogicBlockDef playSound = makeDefaultBlock(kAudioPlaySound, BlockKind::Action);
    for (LogicPropertyDef& property : playSound.properties) {
        if (property.key == "audioAssetId")
            property.value = LogicAssetReference{"jump-sound"};
        else if (property.key == "volume") property.value = 0.8;
    }
    rule.actions = {
        makeDefaultBlock(kJump, BlockKind::Action),
        std::move(playClip),
        std::move(playSound),
    };
    board.rules = {rule};
    hero.logicBoard = board;
    project.objectTypes.emplace("Hero", hero);

    SpriteAnimationAssetDef animation;
    animation.id = "hero-animation";
    animation.sourceImageAssetId = "hero-sheet";
    animation.frames.push_back(SpriteFrameDef{"frame-0", 0, 0, 16, 16});
    SpriteAnimationClipDef clip;
    clip.id = "jump";
    clip.name = "Jump";
    clip.frameIds = {"frame-0"};
    animation.clips.push_back(clip);
    project.spriteAnimationAssets.push_back(animation);
    project.audioAssets.push_back(
        AudioAssetDef{"jump-sound", "Jump", "audio/jump.wav", AudioLoadMode::StaticSound});

    LogicCompileResult compiled = compileProjectLogic(project);
    CHECK(compiled.ok());
    Host host;
    host.grounded.insert(42);
    LogicRuntime runtime(host);
    std::string error;
    CHECK(runtime.loadPrograms(compiled.programs, &error));
    CHECK(runtime.install("Hero", 42, &error).has_value());
    runtime.beginFrame();
    runtime.dispatchKeyPressed(LogicKey::Space);
    CHECK(host.calls.size() == 3);
    CHECK(host.calls.size() > 0 && host.calls[0] == "platformer_jump:42");
    CHECK(host.calls.size() > 1
        && host.calls[1] == "play_clip:42:hero-animation:jump");
    CHECK(host.calls.size() > 2
        && host.calls[2].rfind("play_sound:42:jump-sound:", 0) == 0);
}

static void testP1EverySecondsAndTick() {
    LogicBoardDef board;
    board.id = "logic:Timer";
    LogicRuleDef rule = makeDefaultRule("every");
    rule.trigger = makeDefaultBlock(kEverySeconds, BlockKind::Trigger);
    for (LogicPropertyDef& p : rule.trigger.properties) {
        if (p.key == "seconds") p.value = 0.5;
    }
    rule.actions[0] = {kSetPosition,
        {{"target", LogicEntityReference{}}, {"position", Vec2{1.f, 2.f}}}};
    board.rules.push_back(rule);

    LogicCompileResult compiled = compileBoard("Timer", board);
    CHECK(compiled.ok());
    CHECK(compiled.programs[0].requiresTick);
    CHECK(compiled.programs[0].source.find("on_every_seconds") != std::string::npos);

    Host host;
    LogicRuntime runtime(host);
    std::string error;
    CHECK(runtime.loadPrograms(compiled.programs, &error));
    CHECK(runtime.requiresTick());
    CHECK(runtime.install("Timer", 7, &error).has_value());
    runtime.beginFrame();
    runtime.dispatchTick(0.25f);
    CHECK(host.calls.empty());
    runtime.dispatchTick(0.30f);
    CHECK(host.calls.size() == 1);
    CHECK(host.calls[0] == "position:7:1,2");
}

static void testP1StateAndWaitAndVelocity() {
    {
        LogicBoardDef board;
        board.id = "logic:State";
        LogicRuleDef rule = makeDefaultRule("state");
        LogicBlockDef set = makeDefaultBlock(kStateSet, BlockKind::Action);
        for (LogicPropertyDef& p : set.properties) {
            if (p.key == "key") p.value = LogicVariableReference{"score"};
            else if (p.key == "value") p.value = 10.0;
        }
        LogicBlockDef add = makeDefaultBlock(kStateAdd, BlockKind::Action);
        for (LogicPropertyDef& p : add.properties) {
            if (p.key == "key") p.value = LogicVariableReference{"score"};
            else if (p.key == "amount") p.value = 3.0;
        }
        LogicBlockDef sub = makeDefaultBlock(kStateSubtract, BlockKind::Action);
        for (LogicPropertyDef& p : sub.properties) {
            if (p.key == "key") p.value = LogicVariableReference{"score"};
            else if (p.key == "amount") p.value = 1.0;
        }
        rule.actions = {set, add, sub};
        board.rules.push_back(rule);

        LogicCompileResult compiled = compileBoard("State", board);
        CHECK(compiled.ok());
        Host host;
        host.declareNumber("score", 0.0);
        LogicRuntime runtime(host);
        std::string error;
        CHECK(runtime.loadPrograms(compiled.programs, &error));
        CHECK(runtime.install("State", 1, &error).has_value());
        runtime.beginFrame();
        runtime.dispatchStart();
        CHECK(host.state["score"] == 12.0);
        CHECK(std::count_if(host.calls.begin(), host.calls.end(),
            [](const std::string& c) { return c.rfind("state_", 0) == 0; }) == 3);
    }
    {
        LogicBoardDef board;
        board.id = "logic:Compare";
        LogicRuleDef rule = makeDefaultRule("cmp");
        rule.trigger = {kKeyPressed, {{"key", LogicKey::Space}}};
        LogicBlockDef cond = makeDefaultBlock(kStateCompare, BlockKind::Condition);
        for (LogicPropertyDef& p : cond.properties) {
            if (p.key == "key") p.value = LogicVariableReference{"score"};
            else if (p.key == "op") p.value = LogicStringValue{">="};
            else if (p.key == "value") p.value = 5.0;
        }
        rule.conditions = {makeClause(cond)};
        rule.actions[0] = {kSetVisible,
            {{"target", LogicEntityReference{}}, {"visible", false}}};
        board.rules.push_back(rule);

        LogicCompileResult compiled = compileBoard("Compare", board);
        CHECK(compiled.ok());
        Host host;
        host.declareNumber("score", 4.0);
        LogicRuntime runtime(host);
        std::string error;
        CHECK(runtime.loadPrograms(compiled.programs, &error));
        CHECK(runtime.install("Compare", 1, &error).has_value());
        runtime.beginFrame();
        runtime.dispatchKeyPressed(LogicKey::Space);
        CHECK(host.calls.empty());
        host.state["score"] = 5.0;
        runtime.beginFrame();
        runtime.dispatchKeyPressed(LogicKey::Space);
        CHECK(host.calls.size() == 1);
    }
    {
        LogicBoardDef board;
        board.id = "logic:Wait";
        LogicRuleDef rule = makeDefaultRule("wait");
        LogicBlockDef wait = makeDefaultBlock(kWait, BlockKind::Action);
        for (LogicPropertyDef& p : wait.properties) {
            if (p.key == "seconds") p.value = 0.4;
        }
        LogicBlockDef pos = {kSetPosition,
            {{"target", LogicEntityReference{}}, {"position", Vec2{9.f, 8.f}}}};
        rule.actions = {wait, pos};
        board.rules.push_back(rule);

        LogicCompileResult compiled = compileBoard("Wait", board);
        CHECK(compiled.ok());
        CHECK(compiled.programs[0].requiresTick);
        Host host;
        LogicRuntime runtime(host);
        std::string error;
        CHECK(runtime.loadPrograms(compiled.programs, &error));
        CHECK(runtime.install("Wait", 3, &error).has_value());
        runtime.beginFrame();
        runtime.dispatchStart();
        CHECK(host.calls.empty());
        runtime.dispatchTick(0.2f);
        CHECK(host.calls.empty());
        runtime.dispatchTick(0.3f);
        CHECK(host.calls.size() == 1);
        CHECK(host.calls[0] == "position:3:9,8");
    }
    {
        LogicBoardDef board;
        board.id = "logic:Vel";
        LogicRuleDef rule = makeDefaultRule("vel");
        LogicBlockDef vel = makeDefaultBlock(kSetVelocity, BlockKind::Action);
        for (LogicPropertyDef& p : vel.properties) {
            if (p.key == "velocity") p.value = Vec2{5.f, -3.f};
        }
        rule.actions = {vel};
        board.rules.push_back(rule);

        LogicCompileResult compiled = compileBoard("Vel", board);
        CHECK(compiled.ok());
        Host host;
        LogicRuntime runtime(host);
        std::string error;
        CHECK(runtime.loadPrograms(compiled.programs, &error));
        CHECK(runtime.install("Vel", 4, &error).has_value());
        runtime.beginFrame();
        runtime.dispatchStart();
        CHECK(host.calls.size() == 1);
        CHECK(host.calls[0] == "velocity:4:5,-3");
    }
}

static void testP1KeyDownCondition() {
    LogicBoardDef board;
    board.id = "logic:KeyDown";
    LogicRuleDef rule = makeDefaultRule("held-gate");
    rule.trigger = {kKeyPressed, {{"key", LogicKey::Space}}};
    LogicBlockDef cond = makeDefaultBlock(kKeyDown, BlockKind::Condition);
    for (LogicPropertyDef& p : cond.properties) {
        if (p.key == "key") p.value = LogicKey::A;
    }
    rule.conditions = {makeClause(cond)};
    rule.actions[0] = {kSetVisible,
        {{"target", LogicEntityReference{}}, {"visible", false}}};
    board.rules.push_back(rule);

    LogicCompileResult compiled = compileBoard("KeyDown", board);
    CHECK(compiled.ok());
    CHECK(compiled.programs[0].source.find("is_key_down(\"A\")") != std::string::npos);

    Host host;
    LogicRuntime runtime(host);
    std::string error;
    CHECK(runtime.loadPrograms(compiled.programs, &error));
    CHECK(runtime.install("KeyDown", 1, &error).has_value());
    runtime.beginFrame();
    runtime.dispatchKeyPressed(LogicKey::Space);
    CHECK(host.calls.empty());
    host.keyDown = true;
    runtime.beginFrame();
    runtime.dispatchKeyPressed(LogicKey::Space);
    CHECK(host.calls.size() == 1);
}

static void testP1SpawnInstallFailure() {
    LogicBoardDef board;
    board.id = "logic:SpawnFail";
    LogicRuleDef rule = makeDefaultRule("spawn");
    LogicBlockDef spawn = makeDefaultBlock(kSpawnObject, BlockKind::Action);
    for (LogicPropertyDef& p : spawn.properties) {
        if (p.key == "objectTypeId") p.value = LogicStringValue{"Coin"};
        else if (p.key == "position") p.value = Vec2{10.f, 20.f};
    }
    rule.actions = {spawn};
    board.rules.push_back(rule);

    ProjectDoc project;
    EntityDef coin;
    coin.name = "Coin";
    project.objectTypes["Coin"] = coin;
    EntityDef hero;
    hero.name = "Hero";
    project.objectTypes["Hero"] = hero;

    LogicCompileResult compiled = compileBoard("Hero", board, &hero, &project);
    CHECK(compiled.ok());
    CHECK(compiled.programs[0].source.find("spawn(\"Coin\"") != std::string::npos);

    Host host;
    host.failSpawn = true;
    host.nextSpawnId = 77;
    LogicRuntime runtime(host);
    std::string error;
    CHECK(runtime.loadPrograms(compiled.programs, &error));
    const auto scope = runtime.install("Hero", 1, &error);
    CHECK(scope.has_value());
    runtime.beginFrame();
    runtime.dispatchStart();
    // Spawn must fail closed: no successful entity id, rollback recorded, rule disabled.
    CHECK(host.destroyedSpawns.size() == 1);
    CHECK(host.destroyedSpawns[0] == 77);
    CHECK(std::any_of(host.calls.begin(), host.calls.end(),
        [](const std::string& c) { return c.rfind("spawn:", 0) == 0; }));
    CHECK(std::any_of(host.calls.begin(), host.calls.end(),
        [](const std::string& c) { return c == "spawn_rollback:77"; }));
    CHECK(!runtime.diagnostics().empty());
}

static void testEntityTransformActions() {
    LogicBoardDef board;
    board.id = "logic:Transform";
    LogicRuleDef rule = makeDefaultRule("xf");
    rule.trigger = makeDefaultTrigger();
    LogicBlockDef moveBy = makeDefaultBlock(kTranslateBy, BlockKind::Action);
    for (LogicPropertyDef& p : moveBy.properties) {
        if (p.key == "offset") p.value = Vec2{3.f, 4.f};
    }
    LogicBlockDef setRot = makeDefaultBlock(kSetRotation, BlockKind::Action);
    for (LogicPropertyDef& p : setRot.properties) {
        if (p.key == "degrees") p.value = 90.0;
    }
    LogicBlockDef rotBy = makeDefaultBlock(kRotateBy, BlockKind::Action);
    for (LogicPropertyDef& p : rotBy.properties) {
        if (p.key == "degrees") p.value = -45.0;
    }
    LogicBlockDef setScale = makeDefaultBlock(kSetScale, BlockKind::Action);
    for (LogicPropertyDef& p : setScale.properties) {
        if (p.key == "scale") p.value = Vec2{2.f, 2.f};
    }
    rule.actions = {moveBy, setRot, rotBy, setScale};
    board.rules.push_back(rule);

    // Negative / zero scale rejected.
    LogicBoardDef badBoard = board;
    badBoard.rules[0].actions[3].properties[0].value = Vec2{-1.f, 1.f};
    LogicCompileResult bad = compileBoard("Hero", badBoard);
    CHECK(!bad.ok());

    LogicCompileResult compiled = compileBoard("Hero", board);
    CHECK(compiled.ok());
    CHECK(compiled.programs[0].source.find("translate(3") != std::string::npos);
    CHECK(compiled.programs[0].source.find("set_rotation(") != std::string::npos);
    CHECK(compiled.programs[0].source.find("rotate_by(") != std::string::npos);
    CHECK(compiled.programs[0].source.find("set_scale(2") != std::string::npos);

    Host host;
    LogicRuntime runtime(host);
    std::string error;
    CHECK(runtime.loadPrograms(compiled.programs, &error));
    CHECK(runtime.install("Hero", 9, &error).has_value());
    runtime.beginFrame();
    runtime.dispatchStart();
    CHECK(host.calls.size() == 4);
    CHECK(host.calls[0] == "translate:9:3,4");
    CHECK(host.calls[1].rfind("rotation:9:", 0) == 0);
    CHECK(host.calls[2].rfind("rotate_by:9:", 0) == 0);
    CHECK(host.rotations.size() == 1);
    CHECK(host.rotations[0].first == 9);
    CHECK(std::abs(host.rotations[0].second - std::acos(-1.f) / 2.f) < 0.0001f);
    CHECK(host.rotationDeltas.size() == 1);
    CHECK(host.rotationDeltas[0].first == 9);
    CHECK(std::abs(host.rotationDeltas[0].second + std::acos(-1.f) / 4.f) < 0.0001f);
    CHECK(host.scales.size() == 1);
    CHECK(host.scales[0].first == 9);
    CHECK(host.scales[0].second.x == 2.f);
    CHECK(host.scales[0].second.y == 2.f);
}

static void testManualTransformActions() {
    using namespace ArtCade::Modules;

    LuaHost lua({LuaSandboxProfile::ManualScriptStrict, 1024u * 1024u});
    CHECK(lua.init());
    CHECK(lua.loadManualProgramSource(
        "artcade.require_api_version(2)\n"
        "return {\n"
        "  on_start = function(ctx)\n"
        "    ctx.self:set_rotation(1.25)\n"
        "    ctx.self:rotate_by(-0.5)\n"
        "    ctx.self:set_scale(2, 3)\n"
        "  end\n"
        "}\n",
        "manual-transform.lua", 2, 1000, 64));

    Host host;
    CHECK(lua.callManualOnStart(&host, 17, 1000, 64));
    CHECK(host.rotations.size() == 1);
    CHECK(host.rotations[0].first == 17);
    CHECK(std::abs(host.rotations[0].second - 1.25f) < 0.0001f);
    CHECK(host.rotationDeltas.size() == 1);
    CHECK(host.rotationDeltas[0].first == 17);
    CHECK(std::abs(host.rotationDeltas[0].second + 0.5f) < 0.0001f);
    CHECK(host.scales.size() == 1);
    CHECK(host.scales[0].first == 17);
    CHECK(host.scales[0].second.x == 2.f);
    CHECK(host.scales[0].second.y == 3.f);
    lua.shutdown();
}

static void testStateVariableAndToggle() {
    {
        ProjectDoc project;
        project.globalVariables.push_back(
            {"doorOpen", GameVariableDefinition::Type::Boolean, false, {}});
        LogicBoardDef board;
        board.id = "logic:Toggle";
        LogicRuleDef rule = makeDefaultRule("toggle");
        LogicBlockDef toggle = makeDefaultBlock(kStateToggle, BlockKind::Action);
        applyDeterministicVariableDefault(project, toggle);
        const LogicPropertyDef* keyProp = findProperty(toggle, "key");
        const auto* ref = keyProp ? std::get_if<LogicVariableReference>(&keyProp->value) : nullptr;
        CHECK(ref && ref->id == "doorOpen");
        rule.actions = {toggle};
        board.rules.push_back(rule);
        const LogicCompileResult compiled = compileBoard("Toggle", board, nullptr, &project);
        CHECK(compiled.ok());
        CHECK(compiled.programs[0].source.find("state_toggle_boolean") != std::string::npos);
        Host host;
        host.declareBoolean("doorOpen", false);
        LogicRuntime runtime(host);
        std::string error;
        CHECK(runtime.loadPrograms(compiled.programs, &error));
        CHECK(runtime.install("Toggle", 1, &error).has_value());
        runtime.beginFrame();
        runtime.dispatchStart();
        CHECK(host.boolState["doorOpen"] == true);
    }
    {
        Host host;
        CHECK(!host.setStateNumber("missing", 1.0));
        CHECK(!host.addStateNumber("missing", 1.0));
        CHECK(!host.toggleStateBoolean("missing"));
    }
    {
        ProjectDoc project;
        project.globalVariables.push_back(
            {"score", GameVariableDefinition::Type::Number, 0.0, {}});
        LogicBoardDef board;
        board.id = "logic:Mismatch";
        LogicRuleDef rule = makeDefaultRule("bad");
        LogicBlockDef toggle = makeDefaultBlock(kStateToggle, BlockKind::Action);
        for (LogicPropertyDef& p : toggle.properties) {
            if (p.key == "key") p.value = LogicVariableReference{"score"};
        }
        rule.actions = {toggle};
        board.rules.push_back(rule);
        const auto diags = validateBoard("Mismatch", board, nullptr, &project,
                                         LogicValidationPurpose::AuthoringDiagnostics);
        CHECK(std::any_of(diags.begin(), diags.end(), [](const LogicDiagnostic& d) {
            return d.code == "LB_VARIABLE_TYPE_MISMATCH"
                && d.severity == DiagnosticSeverity::Error;
        }));
    }
}

static void testOncePerActivationExecutionMode() {
    CHECK(findDescriptor(kIsFalling)->activationKind == LogicTriggerActivationKind::Level);
    CHECK(findDescriptor(kEveryFrame)->activationKind == LogicTriggerActivationKind::Level);
    CHECK(findDescriptor(kKeyHeld)->activationKind == LogicTriggerActivationKind::Level);
    CHECK(findDescriptor(kKeyPressed)->activationKind == LogicTriggerActivationKind::Pulse);
    CHECK(findDescriptor(kOnStart)->activationKind == LogicTriggerActivationKind::Pulse);

    EntityDef owner;
    owner.platformerController = PlatformerControllerComponent{};

    // Persistence: omit → EveryOccurrence; explicit once_per_activation round-trips.
    {
        LogicBoardDef board;
        board.id = "logic:ExecModeJson";
        LogicRuleDef rule = makeDefaultRule("rule-1");
        rule.trigger = makeDefaultEventBlock(kIsFalling);
        rule.executionMode = LogicExecutionMode::OncePerActivation;
        rule.actions = {makeDefaultBlock(kJump, BlockKind::Action)};
        board.rules.push_back(rule);
        const nlohmann::json json = logicBoardToJson(board);
        CHECK(json["rules"][0]["executionMode"] == "once_per_activation");
        LogicBoardDef loaded;
        CHECK(logicBoardFromJson(json, loaded).ok);
        CHECK(loaded.rules[0].executionMode == LogicExecutionMode::OncePerActivation);

        nlohmann::json withoutMode = json;
        withoutMode["rules"][0].erase("executionMode");
        LogicBoardDef defaulted;
        CHECK(logicBoardFromJson(withoutMode, defaulted).ok);
        CHECK(defaulted.rules[0].executionMode == LogicExecutionMode::EveryOccurrence);
    }

    // Continuous Level trigger: rising edge once, latch while true, rearm on false.
    {
        LogicBoardDef board;
        board.id = "logic:FallOnce";
        LogicRuleDef rule = makeDefaultRule("rule-1");
        rule.trigger = makeDefaultEventBlock(kIsFalling);
        rule.executionMode = LogicExecutionMode::OncePerActivation;
        rule.actions = {makeDefaultBlock(kJump, BlockKind::Action)};
        board.rules.push_back(rule);

        LogicCompileResult compiled = compileBoard("Hero", board, &owner);
        CHECK(compiled.ok());
        CHECK(compiled.programs[0].source.find("should_execute") != std::string::npos);
        CHECK(compiled.programs[0].source.find("once_per_activation") != std::string::npos);
        const auto& features = compiled.programs[0].requiredFeatures;
        CHECK(std::find(features.begin(), features.end(),
                        "logic.execution.once_per_activation") != features.end());

        Host host;
        LogicRuntime runtime(host);
        std::string error;
        CHECK(runtime.loadPrograms(compiled.programs, &error));
        CHECK(runtime.install("Hero", 1, &error).has_value());

        auto jumpCount = [&]() {
            return std::count_if(host.calls.begin(), host.calls.end(),
                [](const std::string& c) { return c.rfind("platformer_jump:", 0) == 0; });
        };

        // Already falling at first evaluation → one execution (initial false→true).
        host.falling.insert(1);
        for (int i = 0; i < 120; ++i) {
            runtime.beginFrame();
            runtime.dispatchTick(1.f / 60.f);
        }
        CHECK(jumpCount() == 1);

        // true → false → true → second execution.
        host.calls.clear();
        host.falling.clear();
        runtime.beginFrame();
        runtime.dispatchTick(1.f / 60.f);
        CHECK(jumpCount() == 0);
        host.falling.insert(1);
        runtime.beginFrame();
        runtime.dispatchTick(1.f / 60.f);
        CHECK(jumpCount() == 1);
    }

    // Complete WHEN: condition false then true while trigger stays true.
    {
        ProjectDoc project;
        project.globalVariables.push_back(
            {"points", GameVariableDefinition::Type::Number, 0.0, {}});
        LogicBoardDef board;
        board.id = "logic:WhenGate";
        LogicRuleDef rule = makeDefaultRule("rule-1");
        rule.trigger = makeDefaultEventBlock(kIsFalling);
        rule.executionMode = LogicExecutionMode::OncePerActivation;
        LogicBlockDef compare = makeDefaultBlock(kStateCompare, BlockKind::Condition);
        for (LogicPropertyDef& p : compare.properties) {
            if (p.key == "key") p.value = LogicVariableReference{"points"};
            if (p.key == "op") p.value = LogicStringValue{">"};
            if (p.key == "value") p.value = 0.0;
        }
        rule.conditions = {makeClause(compare)};
        rule.actions = {makeDefaultBlock(kJump, BlockKind::Action)};
        board.rules.push_back(rule);

        LogicCompileResult compiled = compileBoard("Hero", board, &owner, &project);
        CHECK(compiled.ok());
        Host host;
        host.declareNumber("points", 0.0);
        host.falling.insert(1);
        LogicRuntime runtime(host);
        std::string error;
        CHECK(runtime.loadPrograms(compiled.programs, &error));
        CHECK(runtime.install("Hero", 1, &error).has_value());

        auto jumpCount = [&]() {
            return std::count_if(host.calls.begin(), host.calls.end(),
                [](const std::string& c) { return c.rfind("platformer_jump:", 0) == 0; });
        };

        runtime.beginFrame();
        runtime.dispatchTick(1.f / 60.f);
        CHECK(jumpCount() == 0);

        host.state["points"] = 100.0;
        runtime.beginFrame();
        runtime.dispatchTick(1.f / 60.f);
        CHECK(jumpCount() == 1);

        host.calls.clear();
        runtime.beginFrame();
        runtime.dispatchTick(1.f / 60.f);
        CHECK(jumpCount() == 0);

        host.state["points"] = 0.0;
        runtime.beginFrame();
        runtime.dispatchTick(1.f / 60.f);
        host.state["points"] = 100.0;
        runtime.beginFrame();
        runtime.dispatchTick(1.f / 60.f);
        CHECK(jumpCount() == 1);
    }

    // Independent latches per entity instance.
    {
        LogicBoardDef board;
        board.id = "logic:MultiInstance";
        LogicRuleDef rule = makeDefaultRule("rule-1");
        rule.trigger = makeDefaultEventBlock(kIsFalling);
        rule.executionMode = LogicExecutionMode::OncePerActivation;
        rule.actions = {makeDefaultBlock(kJump, BlockKind::Action)};
        board.rules.push_back(rule);
        LogicCompileResult compiled = compileBoard("Hero", board, &owner);
        CHECK(compiled.ok());

        Host host;
        LogicRuntime runtime(host);
        std::string error;
        CHECK(runtime.loadPrograms(compiled.programs, &error));
        CHECK(runtime.install("Hero", 1, &error).has_value());
        CHECK(runtime.install("Hero", 2, &error).has_value());

        host.falling.insert(1);
        runtime.beginFrame();
        runtime.dispatchTick(1.f / 60.f);
        CHECK(std::count(host.calls.begin(), host.calls.end(), "platformer_jump:1") == 1);
        CHECK(std::count(host.calls.begin(), host.calls.end(), "platformer_jump:2") == 0);

        host.calls.clear();
        host.falling.insert(2);
        runtime.beginFrame();
        runtime.dispatchTick(1.f / 60.f);
        CHECK(std::count(host.calls.begin(), host.calls.end(), "platformer_jump:1") == 0);
        CHECK(std::count(host.calls.begin(), host.calls.end(), "platformer_jump:2") == 1);
    }

    // Pulse trigger: OncePerActivation does not suppress a second key press.
    {
        LogicBoardDef board;
        board.id = "logic:PulseOnce";
        LogicRuleDef rule = makeDefaultRule("rule-1");
        rule.trigger = {kKeyPressed, {{"key", LogicKey::Space}}};
        rule.executionMode = LogicExecutionMode::OncePerActivation;
        rule.actions = {makeDefaultBlock(kJump, BlockKind::Action)};
        board.rules.push_back(rule);
        LogicCompileResult compiled = compileBoard("Hero", board, &owner);
        CHECK(compiled.ok());
        CHECK(std::any_of(compiled.diagnostics.begin(), compiled.diagnostics.end(),
            [](const LogicDiagnostic& d) {
                return d.code == "LB_EXECUTION_MODE_PULSE_REDUNDANT"
                    && d.severity == DiagnosticSeverity::Warning;
            }));

        Host host;
        LogicRuntime runtime(host);
        std::string error;
        CHECK(runtime.loadPrograms(compiled.programs, &error));
        CHECK(runtime.install("Hero", 1, &error).has_value());
        runtime.beginFrame();
        runtime.dispatchKeyPressed(LogicKey::Space);
        runtime.beginFrame();
        runtime.dispatchKeyPressed(LogicKey::Space);
        CHECK(std::count(host.calls.begin(), host.calls.end(), "platformer_jump:1") == 2);
    }

    // Every Frame + OncePerActivation: one run per Play session.
    {
        LogicBoardDef board;
        board.id = "logic:EveryFrameOnce";
        LogicRuleDef rule = makeDefaultRule("rule-1");
        rule.trigger = makeDefaultBlock(kEveryFrame, BlockKind::Trigger);
        rule.executionMode = LogicExecutionMode::OncePerActivation;
        rule.actions = {makeDefaultBlock(kJump, BlockKind::Action)};
        board.rules.push_back(rule);
        LogicCompileResult compiled = compileBoard("Hero", board, &owner);
        CHECK(compiled.ok());

        Host host;
        LogicRuntime runtime(host);
        std::string error;
        CHECK(runtime.loadPrograms(compiled.programs, &error));
        CHECK(runtime.install("Hero", 1, &error).has_value());
        for (int i = 0; i < 10; ++i) {
            runtime.beginFrame();
            runtime.dispatchTick(1.f / 60.f);
        }
        CHECK(std::count(host.calls.begin(), host.calls.end(), "platformer_jump:1") == 1);
    }

    // Default EveryOccurrence keeps while-true every-tick semantics.
    {
        LogicBoardDef board;
        board.id = "logic:FallEvery";
        LogicRuleDef rule = makeDefaultRule("rule-1");
        rule.trigger = makeDefaultEventBlock(kIsFalling);
        CHECK(rule.executionMode == LogicExecutionMode::EveryOccurrence);
        rule.actions = {makeDefaultBlock(kJump, BlockKind::Action)};
        board.rules.push_back(rule);
        LogicCompileResult compiled = compileBoard("Hero", board, &owner);
        CHECK(compiled.ok());
        CHECK(compiled.programs[0].source.find("should_execute") == std::string::npos);

        Host host;
        host.falling.insert(1);
        LogicRuntime runtime(host);
        std::string error;
        CHECK(runtime.loadPrograms(compiled.programs, &error));
        CHECK(runtime.install("Hero", 1, &error).has_value());
        for (int i = 0; i < 5; ++i) {
            runtime.beginFrame();
            runtime.dispatchTick(1.f / 60.f);
        }
        CHECK(std::count(host.calls.begin(), host.calls.end(), "platformer_jump:1") == 5);
    }
}

static void testValidationPurposesRecovery() {
    EntityDef owner;
    // No TopDownController — Top Down Move is incompatible.

    LogicBoardDef board;
    board.id = "logic:Recovery";
    for (int i = 0; i < 8; ++i) {
        LogicRuleDef rule = makeDefaultRule("rule-" + std::to_string(i + 1));
        rule.actions = {makeDefaultBlock(kTopDownMove, BlockKind::Action)};
        board.rules.push_back(std::move(rule));
    }

    const auto structural = validateBoard(
        "Hero", board, &owner, nullptr, LogicValidationPurpose::StructuralCommit);
    CHECK(!hasLogicErrors(structural));

    const auto authoring = validateBoard(
        "Hero", board, &owner, nullptr, LogicValidationPurpose::AuthoringDiagnostics);
    const std::size_t incompatible = static_cast<std::size_t>(std::count_if(
        authoring.begin(), authoring.end(), [](const LogicDiagnostic& d) {
            return d.code == "LB_INCOMPATIBLE_BLOCK"
                && d.severity == DiagnosticSeverity::Error;
        }));
    CHECK(incompatible == 8);
    CHECK(hasLogicErrors(authoring));

    CHECK(!compileBoard("Hero", board, &owner, nullptr).ok());

    // Drop one incompatible rule — board remains structurally valid with 7 errors.
    board.rules.erase(board.rules.begin());
    const auto afterDelete = validateBoard(
        "Hero", board, &owner, nullptr, LogicValidationPurpose::AuthoringDiagnostics);
    CHECK(std::count_if(afterDelete.begin(), afterDelete.end(),
                        [](const LogicDiagnostic& d) {
                            return d.code == "LB_INCOMPATIBLE_BLOCK";
                        })
          == 7);
    CHECK(!hasLogicErrors(validateBoard(
        "Hero", board, &owner, nullptr, LogicValidationPurpose::StructuralCommit)));

    // Replace one action with a compatible Set Visible — 6 incompatible remain.
    board.rules[0].actions[0] = makeDefaultBlock(kSetVisible, BlockKind::Action);
    const auto afterReplace = validateBoard(
        "Hero", board, &owner, nullptr, LogicValidationPurpose::AuthoringDiagnostics);
    CHECK(std::count_if(afterReplace.begin(), afterReplace.end(),
                        [](const LogicDiagnostic& d) {
                            return d.code == "LB_INCOMPATIBLE_BLOCK";
                        })
          == 6);

    // Disabled incompatible rule does not block Executable with Errors.
    LogicBoardDef disabledOnly;
    disabledOnly.id = "logic:Disabled";
    LogicRuleDef disabled = makeDefaultRule("rule-1");
    disabled.enabled = false;
    disabled.actions = {makeDefaultBlock(kTopDownMove, BlockKind::Action)};
    disabledOnly.rules.push_back(std::move(disabled));
    const auto disabledDiags = validateBoard(
        "Hero", disabledOnly, &owner, nullptr, LogicValidationPurpose::Executable);
    CHECK(!hasLogicErrors(disabledDiags));
    CHECK(std::any_of(disabledDiags.begin(), disabledDiags.end(),
                      [](const LogicDiagnostic& d) {
                          return d.code == "LB_INCOMPATIBLE_BLOCK"
                              && d.severity == DiagnosticSeverity::Warning;
                      }));
    CHECK(compileBoard("Hero", disabledOnly, &owner, nullptr).ok());

    // Empty actions: structural ok; Executable error when enabled.
    LogicBoardDef emptyActions;
    emptyActions.id = "logic:Empty";
    LogicRuleDef emptyRule = makeDefaultRule("rule-1");
    emptyRule.actions.clear();
    emptyActions.rules.push_back(emptyRule);
    CHECK(!hasLogicErrors(validateBoard(
        "Hero", emptyActions, &owner, nullptr,
        LogicValidationPurpose::StructuralCommit)));
    CHECK(hasLogicErrors(validateBoard(
        "Hero", emptyActions, &owner, nullptr, LogicValidationPurpose::Executable)));

    // Unknown block type is structural-ok and authoring-visible.
    LogicBoardDef unknown;
    unknown.id = "logic:Unknown";
    LogicRuleDef unknownRule = makeDefaultRule("rule-1");
    unknownRule.trigger = {"future.unknown_trigger", {}};
    unknown.rules.push_back(unknownRule);
    CHECK(!hasLogicErrors(validateBoard(
        "Hero", unknown, &owner, nullptr, LogicValidationPurpose::StructuralCommit)));
    CHECK(std::any_of(
        validateBoard("Hero", unknown, &owner, nullptr,
                      LogicValidationPurpose::AuthoringDiagnostics)
            .begin(),
        validateBoard("Hero", unknown, &owner, nullptr,
                      LogicValidationPurpose::AuthoringDiagnostics)
            .end(),
        [](const LogicDiagnostic& d) { return d.code == "LB_UNKNOWN_BLOCK"; }));
}

int main() {
    testCompilerAndJson();
    testDescriptorSemanticMetadataConsistency();
    testRuntime();
    testStrictSandboxAndBudget();
    testLimitsSnapshotAndIsolation();
    testIsGroundedCondition();
    testIsGroundedAsEvent();
    testIsFallingAsEvent();
    testIsFallingMigratesToPlatformerState();
    testPlatformerMotionState();
    testIsVisibleAsEvent();
    testSpriteSetFacingAction();
    testPlatformerMoveHorizontalDirection();
    testConditionOperators();
    testPlaySoundAction();
    testSceneActions();
    testDestroyOtherAction();
    testCombinedGameplaySmoke();
    testP1EverySecondsAndTick();
    testP1StateAndWaitAndVelocity();
    testStateVariableAndToggle();
    testP1KeyDownCondition();
    testP1SpawnInstallFailure();
    testEntityTransformActions();
    testManualTransformActions();
    testOncePerActivationExecutionMode();
    testValidationPurposesRecovery();
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
