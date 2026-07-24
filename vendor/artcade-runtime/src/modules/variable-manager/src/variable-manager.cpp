#include "../include/variable-manager.h"

#include <cmath>

namespace ArtCade::Modules {

bool VariableManager::init() {
    clear();
    observers_.clear();
    nextToken_ = 1;
    return true;
}

void VariableManager::shutdown() {
    clear();
    observers_.clear();
}

bool VariableManager::valueMatchesType(
    const Value& value, GameVariableDefinition::Type type) {
    switch (type) {
        case GameVariableDefinition::Type::Number:  return std::holds_alternative<double>(value);
        case GameVariableDefinition::Type::Boolean: return std::holds_alternative<bool>(value);
        case GameVariableDefinition::Type::String:  return std::holds_alternative<std::string>(value);
    }
    return false;
}

bool VariableManager::valuesEqual(const Value& a, const Value& b) {
    if (a.index() != b.index()) return false;
    if (const auto* da = std::get_if<double>(&a)) return *da == std::get<double>(b);
    if (const auto* ba = std::get_if<bool>(&a)) return *ba == std::get<bool>(b);
    if (const auto* sa = std::get_if<std::string>(&a)) return *sa == std::get<std::string>(b);
    return false;
}

bool VariableManager::isFiniteNumber(const Value& value) {
    const auto* number = std::get_if<double>(&value);
    return number && std::isfinite(*number);
}

void VariableManager::configureGlobals(
    const std::vector<GameVariableDefinition>& definitions) {
    vars_.clear();
    globalTypes_.clear();
    for (const auto& def : definitions) {
        if (def.key.empty() || globalTypes_.count(def.key)
            || !valueMatchesType(def.initialValue, def.type)) continue;
        if (def.type == GameVariableDefinition::Type::Number
            && !isFiniteNumber(def.initialValue)) continue;
        globalTypes_[def.key] = def.type;
        vars_[def.key] = def.initialValue;
    }
}

void VariableManager::createEntity(
    EntityId id,
    const std::vector<GameVariableDefinition>& definitions,
    const Snapshot& overrides) {
    Snapshot values;
    std::unordered_map<std::string, GameVariableDefinition::Type> types;
    for (const auto& def : definitions) {
        if (def.key.empty() || types.count(def.key)
            || !valueMatchesType(def.initialValue, def.type)) continue;
        if (def.type == GameVariableDefinition::Type::Number
            && !isFiniteNumber(def.initialValue)) continue;
        types[def.key] = def.type;
        auto overrideIt = overrides.find(def.key);
        Value chosen = def.initialValue;
        if (overrideIt != overrides.end()
            && valueMatchesType(overrideIt->second, def.type)
            && (def.type != GameVariableDefinition::Type::Number
                || isFiniteNumber(overrideIt->second))) {
            chosen = overrideIt->second;
        }
        values[def.key] = chosen;
    }
    entityTypes_[id] = std::move(types);
    entityVars_[id] = std::move(values);
}

void VariableManager::destroyEntity(EntityId id) {
    entityVars_.erase(id);
    entityTypes_.erase(id);
}

VariableManager::Value VariableManager::get(
    const GameVariableId& key, const Value& defaultVal) const {
    auto it = vars_.find(key);
    return it != vars_.end() ? it->second : defaultVal;
}

int32_t VariableManager::getInt(const GameVariableId& key, int32_t def) const {
    auto it = vars_.find(key);
    return it != vars_.end() && std::holds_alternative<double>(it->second)
        ? static_cast<int32_t>(std::get<double>(it->second)) : def;
}

float VariableManager::getFloat(const GameVariableId& key, float def) const {
    auto it = vars_.find(key);
    return it != vars_.end() && std::holds_alternative<double>(it->second)
        ? static_cast<float>(std::get<double>(it->second)) : def;
}

bool VariableManager::getBool(const GameVariableId& key, bool def) const {
    auto it = vars_.find(key);
    return it != vars_.end() && std::holds_alternative<bool>(it->second)
        ? std::get<bool>(it->second) : def;
}

std::string VariableManager::getString(const GameVariableId& key, std::string def) const {
    auto it = vars_.find(key);
    return it != vars_.end() && std::holds_alternative<std::string>(it->second)
        ? std::get<std::string>(it->second) : def;
}

bool VariableManager::exists(const GameVariableId& key) const {
    return vars_.count(key) != 0;
}

bool VariableManager::entityExists(EntityId id, const GameVariableId& key) const {
    auto it = entityVars_.find(id);
    return it != entityVars_.end() && it->second.count(key) != 0;
}

VariableManager::Value VariableManager::getEntity(
    EntityId id, const GameVariableId& key) const {
    auto entityIt = entityVars_.find(id);
    if (entityIt == entityVars_.end()) return 0.0;
    auto valueIt = entityIt->second.find(key);
    return valueIt != entityIt->second.end() ? valueIt->second : Value{0.0};
}

std::optional<GameVariableDefinition::Type> VariableManager::globalType(
    const GameVariableId& id) const {
    auto it = globalTypes_.find(id);
    if (it == globalTypes_.end()) return std::nullopt;
    return it->second;
}

std::optional<double> VariableManager::tryGetNumber(const GameVariableId& id) const {
    auto typeIt = globalTypes_.find(id);
    if (typeIt == globalTypes_.end()
        || typeIt->second != GameVariableDefinition::Type::Number) {
        return std::nullopt;
    }
    auto valueIt = vars_.find(id);
    if (valueIt == vars_.end() || !std::holds_alternative<double>(valueIt->second)) {
        return std::nullopt;
    }
    return std::get<double>(valueIt->second);
}

VariableMutationResult VariableManager::setGlobal(
    const GameVariableId& id, const Value& value) {
    VariableMutationResult result;
    auto typeIt = globalTypes_.find(id);
    if (typeIt == globalTypes_.end() || vars_.count(id) == 0) {
        result.status = VariableMutationStatus::MissingVariable;
        return result;
    }
    result.before = vars_[id];
    if (!valueMatchesType(value, typeIt->second)) {
        result.status = VariableMutationStatus::TypeMismatch;
        result.after = result.before;
        return result;
    }
    if (typeIt->second == GameVariableDefinition::Type::Number
        && !isFiniteNumber(value)) {
        result.status = VariableMutationStatus::NonFiniteValue;
        result.after = result.before;
        return result;
    }
    if (valuesEqual(result.before, value)) {
        result.status = VariableMutationStatus::Unchanged;
        result.after = result.before;
        return result;
    }
    vars_[id] = value;
    result.after = value;
    result.status = VariableMutationStatus::Changed;
    notifyObservers(id, value);
    return result;
}

VariableMutationResult VariableManager::addNumber(
    const GameVariableId& id, double delta) {
    VariableMutationResult result;
    auto typeIt = globalTypes_.find(id);
    if (typeIt == globalTypes_.end() || vars_.count(id) == 0) {
        result.status = VariableMutationStatus::MissingVariable;
        return result;
    }
    result.before = vars_[id];
    if (typeIt->second != GameVariableDefinition::Type::Number
        || !std::holds_alternative<double>(result.before)) {
        result.status = VariableMutationStatus::TypeMismatch;
        result.after = result.before;
        return result;
    }
    if (!std::isfinite(delta)) {
        result.status = VariableMutationStatus::NonFiniteValue;
        result.after = result.before;
        return result;
    }
    const double next = std::get<double>(result.before) + delta;
    if (!std::isfinite(next)) {
        result.status = VariableMutationStatus::NonFiniteValue;
        result.after = result.before;
        return result;
    }
    return setGlobal(id, next);
}

VariableMutationResult VariableManager::toggleBoolean(const GameVariableId& id) {
    VariableMutationResult result;
    auto typeIt = globalTypes_.find(id);
    if (typeIt == globalTypes_.end() || vars_.count(id) == 0) {
        result.status = VariableMutationStatus::MissingVariable;
        return result;
    }
    result.before = vars_[id];
    if (typeIt->second != GameVariableDefinition::Type::Boolean
        || !std::holds_alternative<bool>(result.before)) {
        result.status = VariableMutationStatus::TypeMismatch;
        result.after = result.before;
        return result;
    }
    return setGlobal(id, !std::get<bool>(result.before));
}

bool VariableManager::setEntity(
    EntityId id, const GameVariableId& key, const Value& value) {
    auto entityTypeIt = entityTypes_.find(id);
    if (entityTypeIt == entityTypes_.end()) return false;
    auto typeIt = entityTypeIt->second.find(key);
    if (typeIt == entityTypeIt->second.end() || !valueMatchesType(value, typeIt->second))
        return false;
    if (typeIt->second == GameVariableDefinition::Type::Number && !isFiniteNumber(value))
        return false;
    entityVars_[id][key] = value;
    return true;
}

std::optional<double> VariableManager::addEntity(
    EntityId id, const GameVariableId& key, double delta) {
    if (!entityExists(id, key)) return std::nullopt;
    if (!std::isfinite(delta)) return std::nullopt;
    const Value current = getEntity(id, key);
    if (!std::holds_alternative<double>(current)) return std::nullopt;
    const double next = std::get<double>(current) + delta;
    if (!std::isfinite(next)) return std::nullopt;
    return setEntity(id, key, next) ? std::optional<double>{next} : std::nullopt;
}

void VariableManager::remove(const GameVariableId& key) {
    vars_.erase(key);
    globalTypes_.erase(key);
}

void VariableManager::clear() {
    vars_.clear();
    globalTypes_.clear();
    entityVars_.clear();
    entityTypes_.clear();
}

VariableManager::ObsToken VariableManager::observe(
    const GameVariableId& key, Observer cb) {
    const ObsToken token = nextToken_++;
    observers_.push_back({token, key, std::move(cb)});
    return token;
}

void VariableManager::stopObserving(ObsToken token) {
    observers_.erase(std::remove_if(observers_.begin(), observers_.end(),
        [token](const ObsEntry& entry) { return entry.token == token; }), observers_.end());
}

void VariableManager::notifyObservers(const GameVariableId& key, const Value& value) {
    for (auto& entry : observers_) if (entry.key == key) entry.cb(key, value);
}

VariableManager::Snapshot VariableManager::takeSnapshot() const { return vars_; }

VariableManager::Snapshot VariableManager::takeEntitySnapshot(EntityId id) const {
    auto it = entityVars_.find(id);
    return it != entityVars_.end() ? it->second : Snapshot{};
}

void VariableManager::restoreSnapshot(const Snapshot& snapshot) {
    for (const auto& [key, value] : snapshot) {
        (void)setGlobal(key, value);
    }
}

VariableManager::GameSnapshot VariableManager::takeGameSnapshot(
    const std::vector<EntityId>& persistentIds) const {
    GameSnapshot result{vars_, {}};
    for (EntityId id : persistentIds) {
        auto it = entityVars_.find(id);
        if (it != entityVars_.end()) result.entities[id] = it->second;
    }
    return result;
}

bool VariableManager::restoreGameSnapshot(
    const GameSnapshot& snapshot, const std::vector<EntityId>& persistentIds) {
    // Only catalog keys are accepted — no implicit Number registration.
    if (snapshot.globals.size() != globalTypes_.size()) return false;
    for (const auto& [key, _] : globalTypes_) {
        if (snapshot.globals.count(key) == 0) return false;
    }
    for (const auto& [key, value] : snapshot.globals) {
        auto typeIt = globalTypes_.find(key);
        if (typeIt == globalTypes_.end()) return false;
        if (!valueMatchesType(value, typeIt->second)) return false;
        if (typeIt->second == GameVariableDefinition::Type::Number
            && !isFiniteNumber(value)) return false;
    }
    for (EntityId id : persistentIds) {
        auto savedIt = snapshot.entities.find(id);
        auto typesIt = entityTypes_.find(id);
        if (typesIt == entityTypes_.end()) return false;
        if (savedIt == snapshot.entities.end()
            || savedIt->second.size() != typesIt->second.size()) return false;
        for (const auto& [key, value] : savedIt->second) {
            auto typeIt = typesIt->second.find(key);
            if (typeIt == typesIt->second.end() || !valueMatchesType(value, typeIt->second))
                return false;
            if (typeIt->second == GameVariableDefinition::Type::Number
                && !isFiniteNumber(value)) return false;
        }
    }
    for (const auto& [key, value] : snapshot.globals) {
        vars_[key] = value;
    }
    for (EntityId id : persistentIds) {
        auto savedIt = snapshot.entities.find(id);
        if (savedIt == snapshot.entities.end()) continue;
        for (const auto& [key, value] : savedIt->second) {
            entityVars_[id][key] = value;
        }
    }
    return true;
}

} // namespace ArtCade::Modules
