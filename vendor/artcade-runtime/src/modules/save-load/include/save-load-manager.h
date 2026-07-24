#pragma once

#include "../../../core/module.h"
#include "../../variable-manager/include/variable-manager.h"
#include <string>
#include <unordered_map>
#include <optional>
#include <cstdint>

namespace ArtCade::Modules {

/**
 * SaveLoadManager — file-based save/load with named slots.
 *
 * Uses VariableManager::Snapshot as the in-memory save format.
 * Persists to disk as a simple key=value text file (no external JSON lib needed).
 *
 * API:
 *   save(slot, snapshot)  — serialize and write to <saveDir>/<slot>.sav
 *   load(slot)            — read file and return Snapshot (or nullopt on error)
 *   hasSave(slot)         — check if file exists
 *   deleteSave(slot)      — erase the file
 *   listSlots()           — names of all existing save files
 *
 * Format (text, one key/value per line):
 *   i:score=42          (int)
 *   f:speed=3.14        (float)
 *   b:dead=1            (bool, 1/0)
 *   s:name=hello world  (string)
 */
class SaveLoadManager final : public IModule {
public:
    SaveLoadManager() = default;

    bool init()     override;
    void shutdown() override;

    using Snapshot = VariableManager::GameSnapshot;

    // Directory where .sav files are stored (default: "saves/")
    void setSaveDirectory(const std::string& dir);
    const std::string& saveDirectory() const;

    // ------------------------------------------------------------------ save / load

    bool save(const std::string& slot, const Snapshot& snapshot);
    std::optional<Snapshot> load(const std::string& slot) const;

    // ------------------------------------------------------------------ query

    bool hasSave(const std::string& slot) const;
    void deleteSave(const std::string& slot);
    std::vector<std::string> listSlots() const;

private:
    std::string saveDir_ = "saves/";

    std::string slotPath(const std::string& slot) const;
    bool ensureSaveDir() const;
    static bool isValidSlotName(const std::string& slot);

    static std::string serializeSnapshot(const Snapshot& snap);
    static std::optional<Snapshot> deserializeSnapshot(const std::string& content);
};

} // namespace ArtCade::Modules
