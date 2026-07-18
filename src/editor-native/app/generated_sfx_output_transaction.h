#pragma once

#include "editor-native/model/generated_sfx_policy.h"
#include "editor-native/commands/editor_operation_result.h"

#include "artcade/sfx/types.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ArtCade::EditorNative {

class EditorCoordinator;
struct RemoveGeneratedSfxIntent;

struct GeneratedSfxFileInspection {
    bool ok = false;
    bool exists = false;
    bool readable = false;
    std::string error;
};

enum class GeneratedSfxOutputCommitFailure {
    None,
    Failed,
    Collision,
};

struct GeneratedSfxOutputCommitResult {
    bool ok = false;
    GeneratedSfxOutputCommitFailure failure = GeneratedSfxOutputCommitFailure::Failed;
    std::string error;

    static GeneratedSfxOutputCommitResult success() {
        return {true, GeneratedSfxOutputCommitFailure::None, {}};
    }
    static GeneratedSfxOutputCommitResult failed(
        std::string message,
        GeneratedSfxOutputCommitFailure kind = GeneratedSfxOutputCommitFailure::Failed) {
        return {false, kind, std::move(message)};
    }
};

struct GeneratedSfxFileOperationResult {
    bool ok = false;
    std::string error;

    static GeneratedSfxFileOperationResult success() { return {true, {}}; }
    static GeneratedSfxFileOperationResult failure(std::string message) {
        return {false, std::move(message)};
    }
};

struct GeneratedSfxFileReadResult {
    bool ok = false;
    bool exists = false;
    std::vector<std::uint8_t> bytes;
    std::string error;

    static GeneratedSfxFileReadResult missing() {
        return {true, false, {}, {}};
    }
    static GeneratedSfxFileReadResult success(std::vector<std::uint8_t> value) {
        return {true, true, std::move(value), {}};
    }
    static GeneratedSfxFileReadResult failure(std::string message) {
        return {false, false, {}, std::move(message)};
    }
};

// Platform port for the small set of atomic filesystem operations required by
// the Generated SFX transaction. Ownership remains in ProjectDocument.
class GeneratedSfxOutputRepository {
public:
    virtual ~GeneratedSfxOutputRepository() = default;
    virtual GeneratedSfxFileInspection inspect(
        const std::filesystem::path& path) const = 0;
    virtual GeneratedSfxFileOperationResult moveNoReplace(
        const std::filesystem::path& source,
        const std::filesystem::path& destination) = 0;
    virtual GeneratedSfxFileOperationResult removeIfExists(
        const std::filesystem::path& path) = 0;
    virtual GeneratedSfxFileReadResult readIfExists(
        const std::filesystem::path& path,
        std::uintmax_t maximumBytes) const {
        (void)path;
        (void)maximumBytes;
        return GeneratedSfxFileReadResult::failure(
            "Generated SFX repository cannot preserve file bytes");
    }
    virtual GeneratedSfxFileOperationResult writeNoReplace(
        const std::filesystem::path& path,
        const std::vector<std::uint8_t>& bytes) {
        (void)path;
        (void)bytes;
        return GeneratedSfxFileOperationResult::failure(
            "Generated SFX repository cannot restore file bytes");
    }
};

class FilesystemGeneratedSfxOutputRepository final
    : public GeneratedSfxOutputRepository {
public:
    GeneratedSfxFileInspection inspect(
        const std::filesystem::path& path) const override;
    GeneratedSfxFileOperationResult moveNoReplace(
        const std::filesystem::path& source,
        const std::filesystem::path& destination) override;
    GeneratedSfxFileOperationResult removeIfExists(
        const std::filesystem::path& path) override;
    GeneratedSfxFileReadResult readIfExists(
        const std::filesystem::path& path,
        std::uintmax_t maximumBytes) const override;
    GeneratedSfxFileOperationResult writeNoReplace(
        const std::filesystem::path& path,
        const std::vector<std::uint8_t>& bytes) override;
};

// One application transaction joining an already encoded staging WAV to the
// single persistent registration Command. It owns no UI and creates no second
// authoring path.
class GeneratedSfxOutputTransaction {
public:
    GeneratedSfxOutputTransaction(
        EditorCoordinator& coordinator,
        std::shared_ptr<GeneratedSfxOutputRepository> repository);

    GeneratedSfxOutputCommitResult commit(
        const std::string& generatedSfxId,
        const artcade::sfx::SfxRecipe& renderedRecipe,
        const GeneratedSfxOutputIdentity& output,
        bool regenerating,
        std::uint64_t generationToken);

    /** Destructive delete use case. Removes recipe, linked AudioAssetDef,
     *  structured project references and the canonical WAV in one Undo entry. */
    EditorOperationResult remove(
        const RemoveGeneratedSfxIntent& intent,
        const std::filesystem::path& projectRoot);

private:
    EditorCoordinator& coordinator_;
    std::shared_ptr<GeneratedSfxOutputRepository> repository_;
};

} // namespace ArtCade::EditorNative
