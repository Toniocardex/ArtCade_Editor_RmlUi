#pragma once

#include "editor-native/app/export/export_types.h"

#include <filesystem>
#include <string>

namespace ArtCade::EditorNative {

class ExportTransaction {
public:
    static ExportTransaction begin(const std::filesystem::path& finalDirectory);

    ExportTransaction(ExportTransaction&& other) noexcept;
    ExportTransaction& operator=(ExportTransaction&& other) noexcept;
    ExportTransaction(const ExportTransaction&) = delete;
    ExportTransaction& operator=(const ExportTransaction&) = delete;
    ~ExportTransaction();

    const std::filesystem::path& stagingDirectory() const { return staging_; }
    const std::filesystem::path& finalDirectory() const { return final_; }

    bool writeMarker() const;
    ExportResult commit(bool replaceExisting);
    void rollback() noexcept;

private:
    ExportTransaction() = default;

    std::filesystem::path final_;
    std::filesystem::path staging_;
    std::filesystem::path backup_;
    std::string nonce_;
    bool committed_ = false;
    bool active_ = false;
};

} // namespace ArtCade::EditorNative
