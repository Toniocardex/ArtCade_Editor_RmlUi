#pragma once

#include "dialog-types.h"
#include <optional>
#include <string>

namespace ArtCade::Modules {

/** Result of parsing a dialog JSON file. */
struct DialogParseResult {
    DialogGraph graph;
    std::string error;
    bool ok() const { return error.empty(); }
};

class DialogParser {
public:
    static DialogParseResult parseFile(const std::string& path);
    static DialogParseResult parseJsonString(const std::string& jsonText);

    /** Validates graph integrity (start node, references, at least one end). */
    static std::string validate(const DialogGraph& graph);
};

} // namespace ArtCade::Modules
