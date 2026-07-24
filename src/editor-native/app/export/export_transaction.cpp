#include "editor-native/app/export/export_transaction.h"

#include <chrono>
#include <fstream>
#include <random>
#include <sstream>

namespace ArtCade::EditorNative {
namespace {

constexpr const char* kMarkerName = ".artcade-export-transaction.json";

std::string makeNonce() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::mt19937_64 rng{static_cast<std::uint64_t>(now)
                        ^ static_cast<std::uint64_t>(std::random_device{}())};
    std::ostringstream out;
    out << std::hex << rng();
    return out.str();
}

bool isOwnedStaging(const std::filesystem::path& staging) {
    std::error_code ec;
    return std::filesystem::is_regular_file(staging / kMarkerName, ec);
}

} // namespace

ExportTransaction ExportTransaction::begin(const std::filesystem::path& finalDirectory) {
    ExportTransaction tx;
    tx.final_ = finalDirectory;
    tx.nonce_ = makeNonce();
    const auto parent = finalDirectory.parent_path();
    const auto stem = finalDirectory.filename().u8string();
    tx.staging_ = parent / ("." + stem + ".export-" + tx.nonce_);
    tx.backup_ = parent / ("." + stem + ".backup-" + tx.nonce_);

    std::error_code ec;
    std::filesystem::remove_all(tx.staging_, ec);
    std::filesystem::create_directories(tx.staging_, ec);
    if (ec) {
        tx.active_ = false;
        return tx;
    }
    tx.active_ = tx.writeMarker();
    if (!tx.active_) {
        std::filesystem::remove_all(tx.staging_, ec);
    }
    return tx;
}

ExportTransaction::ExportTransaction(ExportTransaction&& other) noexcept {
    *this = std::move(other);
}

ExportTransaction& ExportTransaction::operator=(ExportTransaction&& other) noexcept {
    if (this == &other) return *this;
    rollback();
    final_ = std::move(other.final_);
    staging_ = std::move(other.staging_);
    backup_ = std::move(other.backup_);
    nonce_ = std::move(other.nonce_);
    committed_ = other.committed_;
    active_ = other.active_;
    other.active_ = false;
    other.committed_ = true;
    return *this;
}

ExportTransaction::~ExportTransaction() {
    rollback();
}

bool ExportTransaction::writeMarker() const {
    std::ofstream out(staging_ / kMarkerName, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << "{\"format\":\"artcade-export-transaction\",\"nonce\":\"" << nonce_ << "\"}\n";
    return static_cast<bool>(out);
}

ExportResult ExportTransaction::commit(bool replaceExisting) {
    ExportResult result;
    result.stage = ExportStage::Committing;
    if (!active_ || staging_.empty()) {
        result.diagnostics.push_back(makeExportError(
            ExportDiagnosticCode::DestinationUnavailable, "export staging is inactive"));
        return result;
    }

    std::error_code ec;
    const bool finalExists = std::filesystem::exists(final_, ec);
    if (finalExists && !replaceExisting) {
        result.diagnostics.push_back(makeExportError(
            ExportDiagnosticCode::DestinationUnavailable,
            "destination already exists", final_));
        return result;
    }

    // Remove marker before publishing.
    std::filesystem::remove(staging_ / kMarkerName, ec);

    if (!finalExists) {
        std::filesystem::rename(staging_, final_, ec);
        if (ec) {
            result.diagnostics.push_back(makeExportError(
                ExportDiagnosticCode::DestinationUnavailable,
                "failed to promote staging: " + ec.message(), final_));
            writeMarker();
            return result;
        }
        committed_ = true;
        active_ = false;
        result.ok = true;
        result.stage = ExportStage::Completed;
        result.outputDirectory = final_;
        return result;
    }

    std::filesystem::remove_all(backup_, ec);
    std::filesystem::rename(final_, backup_, ec);
    if (ec) {
        writeMarker();
        result.diagnostics.push_back(makeExportError(
            ExportDiagnosticCode::DestinationUnavailable,
            "failed to backup existing export: " + ec.message(), final_));
        return result;
    }

    std::filesystem::rename(staging_, final_, ec);
    if (ec) {
        std::error_code restoreEc;
        std::filesystem::rename(backup_, final_, restoreEc);
        writeMarker();
        result.diagnostics.push_back(makeExportError(
            ExportDiagnosticCode::DestinationUnavailable,
            "failed to promote staging; previous export restored: " + ec.message(),
            final_));
        return result;
    }

    std::filesystem::remove_all(backup_, ec);
    committed_ = true;
    active_ = false;
    result.ok = true;
    result.stage = ExportStage::Completed;
    result.outputDirectory = final_;
    return result;
}

void ExportTransaction::rollback() noexcept {
    if (!active_ || committed_) return;
    std::error_code ec;
    if (isOwnedStaging(staging_)) {
        std::filesystem::remove_all(staging_, ec);
    }
    active_ = false;
}

} // namespace ArtCade::EditorNative
