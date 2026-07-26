#include "editor-native/app/recent_projects_store.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <utility>

namespace ArtCade::EditorNative {

std::string RecentProjectsStore::normalizePathKey(std::string path) {
    for (char& c : path) {
        if (c == '\\') c = '/';
    }
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    return path;
}

std::string RecentProjectsStore::displayNameFromPath(const std::string& path) {
    if (path.empty()) return {};
    const std::filesystem::path p{path};
    const std::string stem = p.stem().string();
    if (!stem.empty()) return stem;
    const std::string name = p.filename().string();
    return name.empty() ? path : name;
}

void RecentProjectsStore::touch(std::string path, std::int64_t utc) {
    if (path.empty()) return;
    const std::string key = normalizePathKey(path);
    if (key.empty()) return;

    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [&](const RecentProjectEntry& e) {
                                      return normalizePathKey(e.path) == key;
                                  }),
                   entries_.end());

    RecentProjectEntry entry;
    entry.path = std::move(path);
    entry.displayName = displayNameFromPath(entry.path);
    entry.lastOpenedUtc = utc;
    entries_.insert(entries_.begin(), std::move(entry));

    if (entries_.size() > kMaxEntries) entries_.resize(kMaxEntries);
}

void RecentProjectsStore::remove(const std::string& path) {
    const std::string key = normalizePathKey(path);
    if (key.empty()) return;
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [&](const RecentProjectEntry& e) {
                                      return normalizePathKey(e.path) == key;
                                  }),
                   entries_.end());
}

void RecentProjectsStore::clear() { entries_.clear(); }

bool RecentProjectsStore::fromJson(const std::string& text) {
    entries_.clear();
    if (text.empty()) return true;
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(text);
    } catch (const nlohmann::json::exception&) {
        return false;
    }
    if (!root.is_object()) return false;

    const nlohmann::json* projects = nullptr;
    if (root.contains("projects") && root["projects"].is_array())
        projects = &root["projects"];
    else
        return true; // valid object without projects → empty MRU

    for (const nlohmann::json& item : *projects) {
        if (!item.is_object()) continue;
        if (!item.contains("path") || !item["path"].is_string()) continue;
        std::string path = item["path"].get<std::string>();
        if (path.empty()) continue;

        RecentProjectEntry entry;
        entry.path = std::move(path);
        if (item.contains("displayName") && item["displayName"].is_string()) {
            entry.displayName = item["displayName"].get<std::string>();
        }
        if (entry.displayName.empty())
            entry.displayName = displayNameFromPath(entry.path);
        if (item.contains("lastOpenedUtc") && item["lastOpenedUtc"].is_number_integer())
            entry.lastOpenedUtc = item["lastOpenedUtc"].get<std::int64_t>();
        else if (item.contains("lastOpenedUtc") && item["lastOpenedUtc"].is_number())
            entry.lastOpenedUtc = static_cast<std::int64_t>(item["lastOpenedUtc"].get<double>());

        const std::string key = normalizePathKey(entry.path);
        const bool dup = std::any_of(entries_.begin(), entries_.end(),
                                     [&](const RecentProjectEntry& e) {
                                         return normalizePathKey(e.path) == key;
                                     });
        if (dup) continue;
        entries_.push_back(std::move(entry));
        if (entries_.size() >= kMaxEntries) break;
    }

    std::stable_sort(entries_.begin(), entries_.end(),
                     [](const RecentProjectEntry& a, const RecentProjectEntry& b) {
                         return a.lastOpenedUtc > b.lastOpenedUtc;
                     });
    if (entries_.size() > kMaxEntries) entries_.resize(kMaxEntries);
    return true;
}

std::string RecentProjectsStore::toJson() const {
    nlohmann::json projects = nlohmann::json::array();
    for (const RecentProjectEntry& entry : entries_) {
        projects.push_back({
            {"path", entry.path},
            {"displayName", entry.displayName},
            {"lastOpenedUtc", entry.lastOpenedUtc},
        });
    }
    const nlohmann::json root = {
        {"version", 1},
        {"projects", std::move(projects)},
    };
    return root.dump(2);
}

} // namespace ArtCade::EditorNative
