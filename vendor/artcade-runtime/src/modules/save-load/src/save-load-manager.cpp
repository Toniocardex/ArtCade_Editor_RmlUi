#include "../include/save-load-manager.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace ArtCade::Modules {

bool SaveLoadManager::init() { return true; }
void SaveLoadManager::shutdown() {}

void SaveLoadManager::setSaveDirectory(const std::string& dir) {
    saveDir_ = dir;
    if (!saveDir_.empty() && saveDir_.back() != '/' && saveDir_.back() != '\\')
        saveDir_ += '/';
}

const std::string& SaveLoadManager::saveDirectory() const { return saveDir_; }
std::string SaveLoadManager::slotPath(const std::string& slot) const {
    return saveDir_ + slot + ".sav";
}

bool SaveLoadManager::isValidSlotName(const std::string& slot) {
    if (slot.empty() || slot.size() > 64) return false;
    for (unsigned char ch : slot) {
        if (!std::isalnum(ch) && ch != '_' && ch != '-' && ch != ' ') return false;
    }
    return true;
}

bool SaveLoadManager::ensureSaveDir() const {
    if (saveDir_.empty()) return false;
    std::error_code error;
    fs::create_directories(saveDir_, error);
    return !error;
}

std::string SaveLoadManager::serializeSnapshot(const Snapshot& snapshot) {
    const auto encode = [](const VariableManager::Value& value) {
        return std::visit([](const auto& item) -> nlohmann::json { return item; }, value);
    };
    nlohmann::json root = {
        {"version", 1},
        {"globals", nlohmann::json::object()},
        {"entities", nlohmann::json::object()},
    };
    for (const auto& [key, value] : snapshot.globals)
        root["globals"][key] = encode(value);
    for (const auto& [id, variables] : snapshot.entities) {
        auto& entity = root["entities"][std::to_string(id)];
        entity = nlohmann::json::object();
        for (const auto& [key, value] : variables) entity[key] = encode(value);
    }
    return root.dump(2);
}

std::optional<SaveLoadManager::Snapshot>
SaveLoadManager::deserializeSnapshot(const std::string& content) {
    const auto decode = [](const nlohmann::json& value)
        -> std::optional<VariableManager::Value> {
        if (value.is_boolean()) return value.get<bool>();
        if (value.is_number()) return value.get<double>();
        if (value.is_string()) return value.get<std::string>();
        return std::nullopt;
    };
    try {
        const auto root = nlohmann::json::parse(content);
        if (root.value("version", 0) != 1 || !root.contains("globals")
            || !root["globals"].is_object() || !root.contains("entities")
            || !root["entities"].is_object()) return std::nullopt;

        Snapshot snapshot;
        for (const auto& [key, raw] : root["globals"].items()) {
            auto value = decode(raw);
            if (!value) return std::nullopt;
            snapshot.globals[key] = std::move(*value);
        }
        for (const auto& [rawId, rawVariables] : root["entities"].items()) {
            if (!rawVariables.is_object()) return std::nullopt;
            auto& variables = snapshot.entities[static_cast<EntityId>(std::stoul(rawId))];
            for (const auto& [key, raw] : rawVariables.items()) {
                auto value = decode(raw);
                if (!value) return std::nullopt;
                variables[key] = std::move(*value);
            }
        }
        return snapshot;
    } catch (...) {
        return std::nullopt;
    }
}

bool SaveLoadManager::save(const std::string& slot, const Snapshot& snapshot) {
    if (!isValidSlotName(slot) || !ensureSaveDir()) return false;
    std::ofstream file(slotPath(slot));
    if (!file.is_open()) return false;
    file << serializeSnapshot(snapshot);
    return file.good();
}

std::optional<SaveLoadManager::Snapshot>
SaveLoadManager::load(const std::string& slot) const {
    if (!isValidSlotName(slot)) return std::nullopt;
    std::ifstream file(slotPath(slot));
    if (!file.is_open()) return std::nullopt;
    const std::string content((std::istreambuf_iterator<char>(file)), {});
    return deserializeSnapshot(content);
}

bool SaveLoadManager::hasSave(const std::string& slot) const {
    return isValidSlotName(slot) && fs::exists(slotPath(slot));
}

void SaveLoadManager::deleteSave(const std::string& slot) {
    if (!isValidSlotName(slot)) return;
    std::error_code error;
    fs::remove(slotPath(slot), error);
}

std::vector<std::string> SaveLoadManager::listSlots() const {
    std::vector<std::string> result;
    std::error_code error;
    if (!fs::exists(saveDir_, error)) return result;
    for (const auto& entry : fs::directory_iterator(saveDir_, error)) {
        if (entry.path().extension() == ".sav")
            result.push_back(entry.path().stem().string());
    }
    std::sort(result.begin(), result.end());
    return result;
}

} // namespace ArtCade::Modules
