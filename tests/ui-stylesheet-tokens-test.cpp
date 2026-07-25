// ADR-0027 phase 3 — the enforcement that keeps the design system real.
//
// Two invariants, both checked against the shipped .rcss sources:
//   1. theme.rcss is the ONLY stylesheet allowed to contain a colour.
//   2. every colour theme.rcss declares must also be documented in one of its
//      comment blocks — so adding a value costs a line of documentation and a
//      reviewer sees it, instead of the palette quietly growing back.
//
// Without this the phase-1/2 migration is a one-off cleanup that re-rots at
// the next panel: that is exactly how the 163 distinct values this replaced
// accumulated in the first place.
//
// Spacing is deliberately NOT enforced here. The scale ADR-0027 proposed
// (2/4/6/8/12/16/24) does not describe the UI as built — 10dp alone is used 52
// times — so 152 declarations sit off it. Bringing them onto a scale changes
// the density of every panel and needs its own phase with its own visual
// verification, not a test bolted on here.

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

int passed = 0;
int failed = 0;

#define CHECK(x) do { if (x) ++passed; else { ++failed; \
    std::cerr << "FAIL " #x " line " << __LINE__ << "\n"; } } while (0)

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

bool isHexDigit(char c) {
    return std::isxdigit(static_cast<unsigned char>(c)) != 0;
}

/** Every `#rrggbb`/`#rgb`/`#rrggbbaa` token in @p text, lowercased. */
std::vector<std::string> colourLiterals(const std::string& text) {
    std::vector<std::string> found;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '#') continue;
        std::size_t j = i + 1;
        while (j < text.size() && isHexDigit(text[j])) ++j;
        const std::size_t digits = j - i - 1;
        // An RCSS id selector (#btn-play) stops at a non-hex character that is
        // still part of an identifier; a colour is followed by a delimiter.
        const bool identifierFollows =
            j < text.size() && (std::isalnum(static_cast<unsigned char>(text[j])) != 0
                                || text[j] == '-' || text[j] == '_');
        if (identifierFollows) continue;
        if (digits != 3 && digits != 6 && digits != 8) continue;
        std::string literal = text.substr(i, digits + 1);
        std::transform(literal.begin(), literal.end(), literal.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        found.push_back(literal);
        i = j - 1;
    }
    return found;
}

/** Splits @p text into its comment and non-comment halves. */
struct SplitSource {
    std::string comments;
    std::string declarations;
};

SplitSource splitComments(const std::string& text) {
    SplitSource out;
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t open = text.find("/*", i);
        if (open == std::string::npos) {
            out.declarations += text.substr(i);
            break;
        }
        out.declarations += text.substr(i, open - i);
        const std::size_t close = text.find("*/", open + 2);
        if (close == std::string::npos) {
            out.comments += text.substr(open);
            break;
        }
        out.comments += text.substr(open, close + 2 - open);
        i = close + 2;
    }
    return out;
}

/** Named colours RCSS understands that would bypass a hex-only scan. */
const std::vector<std::string>& namedColours() {
    static const std::vector<std::string> names{
        "white", "black", "red", "green", "blue", "yellow",
        "orange", "purple", "gray", "grey", "silver", "maroon",
        "olive", "lime", "aqua", "teal", "navy", "fuchsia",
    };
    return names;
}

/** True when @p text assigns one of the named colours to a colour property. */
std::vector<std::string> namedColourAssignments(const std::string& text) {
    std::vector<std::string> hits;
    for (const std::string& name : namedColours()) {
        std::size_t pos = 0;
        while ((pos = text.find(name, pos)) != std::string::npos) {
            // Must look like `: name;` / `: name }` — not part of an identifier.
            const bool boundaryBefore = pos > 0 && !std::isalnum(
                static_cast<unsigned char>(text[pos - 1])) && text[pos - 1] != '-';
            const std::size_t after = pos + name.size();
            const bool boundaryAfter = after >= text.size() || !std::isalnum(
                static_cast<unsigned char>(text[after]));
            if (boundaryBefore && boundaryAfter) {
                std::size_t back = pos;
                while (back > 0 && (text[back - 1] == ' ' || text[back - 1] == '\t')) --back;
                if (back > 0 && text[back - 1] == ':') hits.push_back(name);
            }
            pos = after;
        }
    }
    return hits;
}

} // namespace

int main() {
    const std::filesystem::path uiDir{ARTCADE_UI_RESOURCE_DIR};
    CHECK(std::filesystem::is_directory(uiDir));

    std::vector<std::filesystem::path> sheets;
    for (const auto& entry : std::filesystem::directory_iterator(uiDir)) {
        if (entry.path().extension() == ".rcss") sheets.push_back(entry.path());
    }
    std::sort(sheets.begin(), sheets.end());
    // The suite is worthless if it silently scans nothing.
    CHECK(sheets.size() >= 8);

    bool sawTheme = false;
    for (const std::filesystem::path& sheet : sheets) {
        const std::string name = sheet.filename().string();
        const std::string text = readFile(sheet);
        const SplitSource split = splitComments(text);

        if (name == "theme.rcss") {
            sawTheme = true;
            // Invariant 2: declared colours must be documented in a comment.
            const std::vector<std::string> documented = colourLiterals(split.comments);
            const std::set<std::string> documentedSet(documented.begin(), documented.end());
            for (const std::string& declared : colourLiterals(split.declarations)) {
                if (documentedSet.count(declared) == 0) {
                    ++failed;
                    std::cerr << "FAIL theme.rcss declares an undocumented colour: "
                              << declared
                              << " — add it to the palette comment or reuse a role\n";
                } else {
                    ++passed;
                }
            }
            continue;
        }

        // Invariant 1: no colour of any kind outside theme.rcss.
        for (const std::string& stray : colourLiterals(split.declarations)) {
            ++failed;
            std::cerr << "FAIL " << name << " declares colour " << stray
                      << " — colours belong in theme.rcss (ADR-0027)\n";
        }
        for (const std::string& stray : namedColourAssignments(split.declarations)) {
            ++failed;
            std::cerr << "FAIL " << name << " assigns named colour '" << stray
                      << "' — colours belong in theme.rcss (ADR-0027)\n";
        }
        ++passed;  // this sheet is clean
    }
    CHECK(sawTheme);

    // The scanner must actually recognise a colour: a test that cannot fail is
    // not enforcement. Verified on the shapes real sheets use, and on the
    // id-selector shape that must NOT be mistaken for one.
    CHECK(colourLiterals("color: #1f1f23;").size() == 1);
    CHECK(colourLiterals("border: 1px #3b82f655;").size() == 1);
    CHECK(colourLiterals("background: #abc;").size() == 1);
    CHECK(colourLiterals("#btn-play-project { display: block; }").empty());
    CHECK(colourLiterals("#btn-grid-size.disabled { }").empty());
    CHECK(namedColourAssignments("color: white;").size() == 1);
    CHECK(namedColourAssignments(".white-label { display: block; }").empty());

    std::cout << "ui-stylesheet-tokens-test: " << passed << " passed, "
              << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
