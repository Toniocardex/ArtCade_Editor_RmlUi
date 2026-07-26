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

bool RecentProjectsStore::entriesEqual(const std::vector<RecentProjectEntry>& a,
                                       const std::vector<RecentProjectEntry>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].path != b[i].path
            || a[i].displayName != b[i].displayName
            || a[i].lastOpenedUtc != b[i].lastOpenedUtc) {
            return false;
        }
    }
    return true;
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
    ++contentRevision_;
}

bool RecentProjectsStore::remove(const std::string& path) {
    const std::string key = normalizePathKey(path);
    if (key.empty()) return false;
    const std::size_t previousSize = entries_.size();
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [&](const RecentProjectEntry& e) {
                                      return normalizePathKey(e.path) == key;
                                  }),
                   entries_.end());
    if (entries_.size() == previousSize) return false;
    ++contentRevision_;
    return true;
}

void RecentProjectsStore::clear() {
    if (entries_.empty()) return;
    entries_.clear();
    ++contentRevision_;
}

bool RecentProjectsStore::fromJson(const std::string& text) {
    std::vector<RecentProjectEntry> next;
    if (!text.empty()) {
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
        // Valid object without projects → empty MRU (next stays empty).

        if (projects) {
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
                    entry.lastOpenedUtc =
                        static_cast<std::int64_t>(item["lastOpenedUtc"].get<double>());

                const std::string key = normalizePathKey(entry.path);
                const bool dup = std::any_of(next.begin(), next.end(),
                                             [&](const RecentProjectEntry& e) {
                                                 return normalizePathKey(e.path) == key;
                                             });
                if (dup) continue;
                next.push_back(std::move(entry));
                if (next.size() >= kMaxEntries) break;
            }

            std::stable_sort(next.begin(), next.end(),
                             [](const RecentProjectEntry& a, const RecentProjectEntry& b) {
                                 return a.lastOpenedUtc > b.lastOpenedUtc;
                             });
            if (next.size() > kMaxEntries) next.resize(kMaxEntries);
        }
    }

    if (entriesEqual(entries_, next)) return true;
    entries_ = std::move(next);
    ++contentRevision_;
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
