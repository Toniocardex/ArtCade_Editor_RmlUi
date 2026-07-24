#pragma once

#include "../../../core/module.h"
#include "../../../core/types.h"
#include <string>
#include <variant>
#include <unordered_map>
#include <optional>
#include <functional>
#include <vector>
#include <cstdint>

namespace ArtCade::Modules {

/**
 * Outcome of a global variable mutation through VariableManager.
 * Callers must not treat computed values as applied unless accepted().
 */
enum class VariableMutationStatus {
    Changed,
    Unchanged,
    MissingVariable,
    TypeMismatch,
    NonFiniteValue
};

struct VariableMutationResult {
    VariableMutationStatus status = VariableMutationStatus::MissingVariable;
    GameVariableValue before = 0.0;
    GameVariableValue after = 0.0;

    [[nodiscard]] bool accepted() const {
        return status == VariableMutationStatus::Changed
            || status == VariableMutationStatus::Unchanged;
    }
    [[nodiscard]] bool changed() const {
        return status == VariableMutationStatus::Changed;
    }
};

/**
 * VariableManager — typed global variable store.
 *
 * Materializes only definitions from ProjectDoc.globalVariables (via
 * configureGlobals). Does not create keys implicitly. Mutations return
 * VariableMutationResult; observers fire only on Changed.
 */
class VariableManager final : public IModule {
public:
    VariableManager() = default;

    bool init()     override;
    void shutdown() override;

    using Value    = GameVariableValue;
    using Observer = std::function<void(const GameVariableId& key, const Value& newVal)>;
    using ObsToken = uint32_t;
    using Snapshot = std::unordered_map<std::string, Value>;
    using EntitySnapshot = std::unordered_map<EntityId, Snapshot>;

    struct GameSnapshot {
        Snapshot globals;
        EntitySnapshot entities;
    };

    /** Replaces materialized globals with validated ProjectDoc definitions. */
    void configureGlobals(const std::vector<GameVariableDefinition>& definitions);
    /** Creates typed local storage for @p id from object-type definitions and overrides. */
    void createEntity(EntityId id,
                      const std::vector<GameVariableDefinition>& definitions,
                      const Snapshot& overrides = {});
    /** Destroys every local variable belonging to @p id. */
    void destroyEntity(EntityId id);

    // ------------------------------------------------------------------ get

    /** Returns a global value, or @p defaultVal when the key is absent. */
    Value get(const GameVariableId& key, const Value& defaultVal = {}) const;
    /** Converts a Number global to int, or returns @p def when unavailable. */
    int32_t getInt(const GameVariableId& key, int32_t def = 0) const;
    /** Converts a Number global to float, or returns @p def when unavailable. */
    float getFloat(const GameVariableId& key, float def = 0.f) const;
    /** Returns a Boolean global, or @p def when unavailable. */
    bool getBool(const GameVariableId& key, bool def = false) const;
    /** Returns a String global, or @p def when unavailable. */
    std::string getString(const GameVariableId& key, std::string def = "") const;

    /** Returns whether a catalog global key is materialized. */
    bool exists(const GameVariableId& key) const;
    /** Returns whether @p id owns a local variable @p key. */
    bool entityExists(EntityId id, const GameVariableId& key) const;
    /** Returns a local value, or Number zero when the entity/key is unavailable. */
    Value getEntity(EntityId id, const GameVariableId& key) const;

    /**
     * Declared type of a materialized global, or nullopt if missing.
     */
    std::optional<GameVariableDefinition::Type> globalType(const GameVariableId& id) const;

    /**
     * Number value if the key exists and is Number; otherwise nullopt.
     */
    std::optional<double> tryGetNumber(const GameVariableId& id) const;

    // ------------------------------------------------------------------ set (strict)

    /** Strictly sets a declared global and reports whether it was accepted. */
    VariableMutationResult setGlobal(const GameVariableId& id, const Value& value);
    /** Strictly adds a finite delta to a declared Number global. */
    VariableMutationResult addNumber(const GameVariableId& id, double delta);
    /** Strictly toggles a declared Boolean global. */
    VariableMutationResult toggleBoolean(const GameVariableId& id);

    /** Strictly changes a typed local variable. */
    bool setEntity(EntityId id, const GameVariableId& key, const Value& value);
    /** Adds a finite delta to a local Number, or returns nullopt on rejection. */
    std::optional<double> addEntity(EntityId id, const GameVariableId& key, double delta);

    /** Removes one global key and its catalog type. */
    void remove(const GameVariableId& key);
    /** Clears all global and local variable state. */
    void clear();

    // ------------------------------------------------------------------ observe

    /** Subscribes to changed values for @p key and returns a cancellation token. */
    ObsToken observe(const GameVariableId& key, Observer cb);
    /** Cancels a previously returned observation token. */
    void     stopObserving(ObsToken token);

    // ------------------------------------------------------------------ snapshot

    /** Captures all current globals. */
    Snapshot takeSnapshot() const;
    /** Captures local values for one entity. */
    Snapshot takeEntitySnapshot(EntityId id) const;
    /** Restores matching globals through strict mutation checks. */
    void     restoreSnapshot(const Snapshot& snap);
    /** Captures globals and local values for persistent entities. */
    GameSnapshot takeGameSnapshot(const std::vector<EntityId>& persistentIds) const;
    /** Restores an exact catalog-compatible gameplay snapshot. */
    bool restoreGameSnapshot(const GameSnapshot& snapshot,
                             const std::vector<EntityId>& persistentIds);

private:
    std::unordered_map<std::string, Value> vars_;
    std::unordered_map<std::string, GameVariableDefinition::Type> globalTypes_;
    std::unordered_map<EntityId, Snapshot> entityVars_;
    std::unordered_map<EntityId,
        std::unordered_map<std::string, GameVariableDefinition::Type>> entityTypes_;

    struct ObsEntry {
        ObsToken token;
        GameVariableId key;
        Observer    cb;
    };
    std::vector<ObsEntry> observers_;
    ObsToken nextToken_ = 1;

    void notifyObservers(const GameVariableId& key, const Value& val);
    static bool valueMatchesType(const Value& value, GameVariableDefinition::Type type);
    static bool valuesEqual(const Value& a, const Value& b);
    static bool isFiniteNumber(const Value& value);
};

} // namespace ArtCade::Modules
