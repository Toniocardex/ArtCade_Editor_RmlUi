#pragma once

#include <cstdint>
#include <limits>

namespace ArtCade::EditorNative {

using ProjectSessionId = std::uint64_t;

// Workspace identity for one loaded/replaced project lifetime. The path is not
// an identity: reopening the same file must invalidate work from the previous
// document instance.
class ProjectSessionIdentity {
public:
    ProjectSessionId current() const { return current_; }

    ProjectSessionId advance() {
        if (current_ == std::numeric_limits<ProjectSessionId>::max()) current_ = 1;
        else ++current_;
        return current_;
    }

private:
    ProjectSessionId current_ = 1;
};

} // namespace ArtCade::EditorNative
