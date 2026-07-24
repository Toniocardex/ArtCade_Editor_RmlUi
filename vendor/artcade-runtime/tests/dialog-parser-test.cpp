#include "../src/modules/dialog/include/dialog-parser.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static int failures = 0;

static void expect(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "  [fail] " << msg << "\n";
        ++failures;
    } else {
        std::cout << "  [ok] " << msg << "\n";
    }
}

int main() {
    fs::path golden = fs::path(ARTCADE_REPO_ROOT) / "docs/examples/dialogs/innkeeper.json";
    if (!fs::exists(golden)) {
        golden = fs::path("docs/examples/dialogs/innkeeper.json");
    }

    auto result = ArtCade::Modules::DialogParser::parseFile(golden.string());
    expect(result.ok(), "parse innkeeper golden JSON");
    expect(result.graph.dialogId == "innkeeper", "dialogId");
    expect(result.graph.startNode == "n1", "startNode");
    const auto n3 = result.graph.nodes.find("n3");
    expect(n3 != result.graph.nodes.end(), "choice node n3");
    expect(n3 != result.graph.nodes.end() && n3->second.options.size() == 2,
           "two choices");

    const std::string bad = R"({"dialogId":"x","startNode":"missing","nodes":{}})";
    auto badResult = ArtCade::Modules::DialogParser::parseJsonString(bad);
    expect(!badResult.ok(), "reject orphan startNode");

    std::cout << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}
