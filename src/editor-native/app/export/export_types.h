#pragma once

#include "core/types.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ArtCade::EditorNative {

enum class ExportTarget {
    WindowsX64,
};

enum class ExportStage {
    Idle,
    Preflight,
    PackingProject,
    PreparingPlayer,
    Verifying,
    Committing,
    Completed,
    Failed,
    Cancelled,
};

enum class ExportDiagnosticSeverity {
    Info,
    Warning,
    Error,
};

enum class ExportDiagnosticCode {
    Playing,
    ProjectUnsaved,
    ProjectDirty,
    InvalidStartScene,
    RuntimeValidationFailed,
    InvalidScript,
    InvalidAsset,
    ProjectWriteInProgress,
    InvalidProductName,
    MissingTemplate,
    IncompatibleTemplate,
    InvalidDestination,
    DestinationUnavailable,
};

inline const char* exportDiagnosticCodeString(ExportDiagnosticCode code) {
    switch (code) {
    case ExportDiagnosticCode::Playing: return "EXP001";
    case ExportDiagnosticCode::ProjectUnsaved: return "EXP002";
    case ExportDiagnosticCode::ProjectDirty: return "EXP003";
    case ExportDiagnosticCode::InvalidStartScene: return "EXP004";
    case ExportDiagnosticCode::RuntimeValidationFailed: return "EXP005";
    case ExportDiagnosticCode::InvalidScript: return "EXP006";
    case ExportDiagnosticCode::InvalidAsset: return "EXP007";
    case ExportDiagnosticCode::ProjectWriteInProgress: return "EXP008";
    case ExportDiagnosticCode::InvalidProductName: return "EXP009";
    case ExportDiagnosticCode::MissingTemplate: return "EXP010";
    case ExportDiagnosticCode::IncompatibleTemplate: return "EXP011";
    case ExportDiagnosticCode::InvalidDestination: return "EXP012";
    case ExportDiagnosticCode::DestinationUnavailable: return "EXP013";
    }
    return "EXP000";
}

struct ExportDiagnostic {
    ExportDiagnosticSeverity severity = ExportDiagnosticSeverity::Error;
    ExportDiagnosticCode code = ExportDiagnosticCode::RuntimeValidationFailed;
    std::string message;
    std::filesystem::path relatedPath;
};

struct ExportRequest {
    ExportTarget target = ExportTarget::WindowsX64;
    std::filesystem::path destinationDirectory;
    std::string productName;
    bool replaceExisting = false;
};

struct ExportArtifact {
    std::filesystem::path path;
    std::uintmax_t sizeBytes = 0;
    std::string sha256;
};

struct ExportResult {
    bool ok = false;
    ExportStage stage = ExportStage::Idle;
    std::filesystem::path outputDirectory;
    std::vector<ExportArtifact> artifacts;
    std::vector<ExportDiagnostic> diagnostics;
};

struct ExportContext {
    std::filesystem::path projectFile;
    std::filesystem::path projectRoot;
    std::string projectId;
    std::string projectName;
    std::uint64_t projectRevision = 0;
    int projectFormatVersion = 0;
};

struct ExportProjectSnapshot {
    std::string projectJson;
    std::string projectName;
    SceneId startSceneId;
    std::uint64_t documentRevision = 0;
    int projectFormatVersion = 0;
};

struct ArchiveEntry {
    std::string archivePath;
    std::variant<std::filesystem::path, std::vector<std::uint8_t>> source;
};

struct ProjectPackPlan {
    ExportProjectSnapshot project;
    std::vector<ArchiveEntry> files;
};

struct ExportDomainEligibility {
    bool allowed = false;
    std::vector<ExportDiagnostic> diagnostics;
};

inline ExportDiagnostic makeExportError(ExportDiagnosticCode code, std::string message,
                                        std::filesystem::path path = {}) {
    ExportDiagnostic d;
    d.severity = ExportDiagnosticSeverity::Error;
    d.code = code;
    d.message = std::move(message);
    d.relatedPath = std::move(path);
    return d;
}

inline ExportDiagnostic makeExportWarning(ExportDiagnosticCode code, std::string message) {
    ExportDiagnostic d;
    d.severity = ExportDiagnosticSeverity::Warning;
    d.code = code;
    d.message = std::move(message);
    return d;
}

bool destinationIsInsideProjectRoot(const std::filesystem::path& destination,
                                    const std::filesystem::path& projectRoot);

std::string normalizeProductFileName(std::string productName, bool* changed);

} // namespace ArtCade::EditorNative
