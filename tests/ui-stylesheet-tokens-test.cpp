// ADR-0033 design-system source gates.

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

int passed = 0;
int failed = 0;

#define CHECK(x) do { if (x) ++passed; else { ++failed; \
    std::cerr << "FAIL " #x " line " << __LINE__ << "\n"; } } while (0)

void fail(const std::filesystem::path& file, std::size_t line, const std::string& message) {
    ++failed;
    std::cerr << "FAIL " << file.generic_string() << ":" << line << ": " << message << "\n";
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        fail(path, 0, "file is unreadable");
        return {};
    }
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string trim(const std::string& value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}

std::size_t lineAt(const std::string& source, std::size_t offset) {
    return 1 + static_cast<std::size_t>(
        std::count(source.begin(), source.begin() + static_cast<std::ptrdiff_t>(offset), '\n'));
}

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
        out.declarations.append(close + 2 - open, ' ');
        i = close + 2;
    }
    return out;
}

bool isHexDigit(char c) {
    return std::isxdigit(static_cast<unsigned char>(c)) != 0;
}

std::vector<std::string> colourLiterals(const std::string& text) {
    std::vector<std::string> found;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '#') continue;
        if (i > 0 && text[i - 1] == '&') continue; // HTML character reference.
        std::size_t j = i + 1;
        while (j < text.size() && isHexDigit(text[j])) ++j;
        const std::size_t digits = j - i - 1;
        const bool identifierFollows =
            j < text.size() && (std::isalnum(static_cast<unsigned char>(text[j])) != 0
                                || text[j] == '-' || text[j] == '_');
        if (identifierFollows || (digits != 3 && digits != 6 && digits != 8)) continue;
        found.push_back(lower(text.substr(i, digits + 1)));
        i = j - 1;
    }
    return found;
}

bool containsPuaReference(const std::string& source) {
    static const std::regex reference(R"(&#(x[0-9a-f]+|[0-9]+);)", std::regex::icase);
    for (std::sregex_iterator it(source.begin(), source.end(), reference), end; it != end; ++it) {
        const std::string digits = lower((*it)[1].str());
        const unsigned long value = digits.front() == 'x'
            ? std::stoul(digits.substr(1), nullptr, 16)
            : std::stoul(digits, nullptr, 10);
        if (value >= 0xe000ul && value <= 0xf8fful) return true;
    }
    return false;
}

std::vector<std::string> namedColourAssignments(const std::string& source) {
    static const std::regex assignment(
        R"(\b(?:color|background|background-color|border(?:-(?:top|right|bottom|left))?-color)\s*:\s*(white|black|red|green|blue|yellow|orange|purple|gray|grey|silver|maroon|olive|lime|aqua|teal|navy|fuchsia)\b)",
        std::regex::icase);
    std::vector<std::string> hits;
    for (std::sregex_iterator it(source.begin(), source.end(), assignment), end; it != end; ++it)
        hits.push_back(lower((*it)[1].str()));
    return hits;
}

bool hasColourFunction(const std::string& source) {
    static const std::regex function(R"(\b(?:rgb|rgba|hsl|hsla)\s*\()", std::regex::icase);
    return std::regex_search(source, function);
}

const std::set<std::string>& roles() {
    static const std::set<std::string> values{
        "surface-base", "surface-chrome", "surface-raised", "surface-sunken",
        "surface-hover", "surface-selected", "surface-disabled",
        "border-subtle", "border-strong", "border-focus",
        "text-primary", "text-secondary", "text-tertiary", "text-disabled",
        "text-strong", "action", "action-hover", "on-action", "brand",
        "danger-surface", "danger-border", "danger-text", "danger-text-hover",
        "warning-surface", "warning-border", "warning-text", "success-text",
        "code-text", "code-comment", "code-keyword", "code-string", "code-number",
        "code-operator", "code-error-bg", "code-error-rule", "code-caret-layer",
        "code-bracket", "code-bracket-on",
    };
    return values;
}

void scanThemeRoles(const std::filesystem::path& file, const std::string& source,
                    std::map<std::string, std::string>& resolvedRoles) {
    static const std::regex marker(
        R"(/\* ds-role\(([a-z0-9-]+)\): ([a-z-]+) \*/\s*([^{}]+)\{([^{}]*)\})");
    std::size_t parsed = 0;
    for (std::sregex_iterator it(source.begin(), source.end(), marker), end; it != end; ++it) {
        ++parsed;
        const std::string role = (*it)[1].str();
        const std::string property = (*it)[2].str();
        const std::string body = (*it)[4].str();
        if (!roles().count(role)) {
            fail(file, lineAt(source, static_cast<std::size_t>((*it).position())),
                 "unknown ds-role '" + role + "'");
            continue;
        }
        const std::regex declaration("\\b" + property + R"(\s*:\s*([^;]+);)",
                                     std::regex::icase);
        std::smatch valueMatch;
        if (!std::regex_search(body, valueMatch, declaration)) {
            fail(file, lineAt(source, static_cast<std::size_t>((*it).position())),
                 "ds-role marker does not match its rule property");
            continue;
        }
        const std::vector<std::string> values = colourLiterals(valueMatch[1].str());
        if (values.size() != 1) {
            fail(file, lineAt(source, static_cast<std::size_t>((*it).position())),
                 "marked declaration must contain exactly one colour literal");
            continue;
        }
        const auto existing = resolvedRoles.find(role);
        if (existing != resolvedRoles.end() && existing->second != values.front()) {
            fail(file, lineAt(source, static_cast<std::size_t>((*it).position())),
                 "role resolves to both " + existing->second + " and " + values.front());
        } else {
            resolvedRoles[role] = values.front();
            ++passed;
        }
    }
    static const std::regex anyMarker(R"(/\* ds-role\()");
    const std::size_t markers = static_cast<std::size_t>(
        std::distance(std::sregex_iterator(source.begin(), source.end(), anyMarker),
                      std::sregex_iterator()));
    const std::size_t declared = colourLiterals(splitComments(source).declarations).size();
    CHECK(parsed == markers);
    CHECK(parsed == declared);
}

std::string metricCategory(const std::string& property) {
    if (property.rfind("margin", 0) == 0 || property.rfind("padding", 0) == 0
        || property == "gap" || property == "row-gap" || property == "column-gap")
        return "spacing";
    if (property == "border-radius") return "radius";
    if (property == "font-size") return "type-size";
    if (property == "font-weight") return "type-weight";
    if (property.rfind("transition", 0) == 0) return "motion";
    return {};
}

bool metricApproved(const std::string& category, const std::string& property,
                    const std::string& rawValue) {
    const std::string value = lower(trim(rawValue));
    std::smatch token;
    if (category == "type-size") {
        static const std::set<std::string> allowed{
            "10dp", "11dp", "12dp", "13dp", "15dp", "18dp", "28dp"};
        return allowed.count(value) != 0;
    }
    if (category == "type-weight") {
        static const std::set<std::string> allowed{"400", "500", "600", "700"};
        return allowed.count(value) != 0;
    }
    if (category == "motion") {
        static const std::regex duration(R"((?:^|[^a-z0-9.])([0-9]*\.?[0-9]+(?:ms|s))\b)",
                                         std::regex::icase);
        bool sawDuration = false;
        for (std::sregex_iterator it(value.begin(), value.end(), duration), end; it != end; ++it) {
            sawDuration = true;
            if (lower((*it)[1].str()) != "0.1s") return false;
        }
        return sawDuration;
    }

    static const std::regex component(R"((-?[0-9]*\.?[0-9]+(?:dp|px|em|%)?|[a-z-]+))",
                                      std::regex::icase);
    std::vector<std::string> tokens;
    for (std::sregex_iterator it(value.begin(), value.end(), component), end; it != end; ++it)
        tokens.push_back(lower((*it)[1].str()));
    if (tokens.empty() || tokens.size() > 4) return false;
    const std::set<std::string> allowed = category == "spacing"
        ? std::set<std::string>{"0", "0dp", "2dp", "4dp", "6dp", "8dp", "10dp",
                                "12dp", "16dp", "24dp"}
        : std::set<std::string>{"0", "0dp", "2dp", "4dp", "8dp"};
    for (const std::string& item : tokens) {
        if (allowed.count(item)) continue;
        if (category == "spacing" && item == "auto" && property.rfind("margin", 0) == 0)
            continue;
        return false;
    }
    return true;
}

void scanMetrics(const std::filesystem::path& file, const std::string& source) {
    static const std::regex declaration(
        R"(\b((?:margin|padding)(?:-(?:top|right|bottom|left))?|gap|row-gap|column-gap|border-radius|font-size|font-weight|transition(?:-[a-z-]+)?)\s*:\s*([^;]+);)",
        std::regex::icase);
    static const std::regex exception(
        R"(^/\* ds-exception\((spacing|radius|type-size|type-weight|motion)\): ([^;]+); ref=(gallery|scene|logic|expression|anim|tileset)(?:/([a-z0-9-]+))? \*/$)");
    std::istringstream lines(source);
    std::string line;
    std::string pendingCategory;
    std::size_t pendingLine = 0;
    bool inComment = false;
    std::string selector;
    std::size_t lineNumber = 0;
    while (std::getline(lines, line)) {
        ++lineNumber;
        const std::string clean = trim(line);
        std::smatch exceptionMatch;
        if (std::regex_match(clean, exceptionMatch, exception)) {
            if (!pendingCategory.empty()) fail(file, pendingLine, "orphaned ds-exception");
            pendingCategory = exceptionMatch[1].str();
            pendingLine = lineNumber;
            continue;
        }
        if (clean.rfind("/*", 0) == 0) inComment = clean.find("*/") == std::string::npos;
        if (inComment) {
            if (clean.find("*/") != std::string::npos) inComment = false;
            continue;
        }
        if (clean.empty()) continue;
        const std::size_t openBrace = clean.find('{');
        if (openBrace != std::string::npos) selector = trim(clean.substr(0, openBrace));
        bool sawDeclaration = false;
        for (std::sregex_iterator it(line.begin(), line.end(), declaration), end; it != end; ++it) {
            sawDeclaration = true;
            const std::string property = lower((*it)[1].str());
            const std::string category = metricCategory(property);
            const std::string value = lower(trim((*it)[2].str()));
            const bool relativeIconSize =
                category == "type-size"
                && std::regex_match(value, std::regex(R"([0-9]*\.?[0-9]+em)"))
                && (selector.find(".icon") != std::string::npos
                    || selector.find(".row-icon") != std::string::npos);
            const bool approved =
                metricApproved(category, property, (*it)[2].str()) || relativeIconSize;
            if (!pendingCategory.empty()) {
                if (pendingCategory != category)
                    fail(file, pendingLine, "ds-exception category does not match declaration");
                else if (approved)
                    fail(file, pendingLine, "ds-exception attached to an approved value");
                else
                    ++passed;
                pendingCategory.clear();
            } else if (!approved) {
                fail(file, lineNumber, "off-scale " + category + " value in " + property
                     + ": " + trim((*it)[2].str()));
            } else {
                ++passed;
            }
        }
        if (!pendingCategory.empty() && !sawDeclaration) {
            fail(file, pendingLine, "ds-exception is not adjacent to a declaration");
            pendingCategory.clear();
        }
        if (clean.find('}') != std::string::npos) selector.clear();
    }
    if (!pendingCategory.empty()) fail(file, pendingLine, "orphaned ds-exception");
}

bool forbiddenInlineColour(const std::string& source) {
    static const std::regex styleColour(
        R"(style\s*=\s*["'][^"']*\b(?:color|background|background-color|border(?:-(?:top|right|bottom|left))?(?:-color)?)\s*:)",
        std::regex::icase);
    static const std::regex styleBlock(R"(<style\b[^>]*>[\s\S]*?(?:#[0-9a-f]{3,8}|\b(?:rgb|rgba|hsl|hsla)\s*\())",
                                       std::regex::icase);
    return std::regex_search(source, styleColour) || std::regex_search(source, styleBlock);
}

void scanRml(const std::filesystem::path& file, const std::string& source) {
    if (forbiddenInlineColour(source)) fail(file, 1, "inline or embedded chrome colour");
    const std::vector<std::string> colours = colourLiterals(source);
    if (!colours.empty()) fail(file, 1, "literal chrome colour outside theme.rcss");

    static const std::set<std::string> tablerAllowlist{
        "ea03","ea3b","ea47","ea5f","ea61","ea6c","ea7a","eaad","eae0","eae3",
        "eb0a","eb0b","eb13","eb1c","eb2d","eb2e","eb41","eb56","eb57","eb5d",
        "eb77","eb8b","ebb8","ebdc","ebe6","ec2e","ed30","ed37","ed46","ed4a",
        "edef","ee9e","f22f","f265",
    };
    static const std::regex pua(R"(&#x([e-f][0-9a-f]{3});)", std::regex::icase);
    for (std::sregex_iterator it(source.begin(), source.end(), pua), end; it != end; ++it) {
        if (!tablerAllowlist.count(lower((*it)[1].str())))
            fail(file, lineAt(source, static_cast<std::size_t>((*it).position())),
                 "PUA glyph is not in the verified Tabler allowlist");
        else {
            const std::size_t position = static_cast<std::size_t>((*it).position());
            const std::size_t tagStart = source.rfind('<', position);
            const std::size_t tagEnd = source.find('>', tagStart);
            const std::string tag = tagStart == std::string::npos || tagEnd == std::string::npos
                ? std::string{} : source.substr(tagStart, tagEnd - tagStart + 1);
            if (tag.find("class=") == std::string::npos
                || (tag.find("icon") == std::string::npos
                    && tag.find("caret") == std::string::npos
                    && tag.find("chevron") == std::string::npos)) {
                fail(file, lineAt(source, position),
                     "static Tabler glyph must use .icon or a semantic icon class");
            }
            ++passed;
        }
    }

    static const std::regex button(R"(<button\b([^>]*)>([\s\S]*?)</button>)",
                                   std::regex::icase);
    static const std::regex tags(R"(<[^>]+>)");
    static const std::regex entities(R"(&#[xX]?[0-9a-fA-F]+;|&nbsp;)");
    for (std::sregex_iterator it(source.begin(), source.end(), button), end; it != end; ++it) {
        const std::string attributes = (*it)[1].str();
        const std::string inner = (*it)[2].str();
        if (!std::regex_search(inner, pua)) continue;
        const std::string visible = trim(std::regex_replace(
            std::regex_replace(inner, tags, ""), entities, ""));
        if (visible.empty() && lower(attributes).find("title=") == std::string::npos)
            fail(file, lineAt(source, static_cast<std::size_t>((*it).position())),
                 "icon-only static control requires title text");
    }
}

bool forbiddenGeneratedColour(const std::string& source) {
    static const std::regex setProperty(
        R"rx(SetProperty\s*\(\s*"(?:color|background|background-color|border(?:-(?:top|right|bottom|left))?(?:-color)?)")rx",
        std::regex::icase);
    static const std::regex setStyle(R"(SetAttribute\s*\(\s*"style"\s*,[\s\S]{0,240}\b(?:color|background|border)\s*:)",
                                     std::regex::icase);
    return forbiddenInlineColour(source) || std::regex_search(source, setProperty)
        || std::regex_search(source, setStyle) || !colourLiterals(source).empty()
        || !namedColourAssignments(source).empty();
}

void scanGeneratedMarkup(const std::filesystem::path& file, const std::string& source) {
    if (forbiddenGeneratedColour(source)) fail(file, 1, "generated markup contains chrome colour");
    if (file.filename() != "ui_icons.h") {
        if (containsPuaReference(source))
            fail(file, 1, "raw Tabler PUA reference bypasses ui_icons.h");
    }
}

void scanPlatformMirrors(const std::filesystem::path& file, const std::string& source,
                         const std::map<std::string, std::string>& rolesByName) {
    static const std::regex mirror(
        R"(// ds-role-mirror\(([a-z0-9-]+)\)\s*\n\s*inline constexpr unsigned int [A-Za-z0-9_]+ = 0x([0-9a-fA-F]{6});)");
    std::size_t found = 0;
    for (std::sregex_iterator it(source.begin(), source.end(), mirror), end; it != end; ++it) {
        ++found;
        const std::string role = (*it)[1].str();
        const auto expected = rolesByName.find(role);
        if (expected == rolesByName.end() || expected->second != "#" + lower((*it)[2].str()))
            fail(file, lineAt(source, static_cast<std::size_t>((*it).position())),
                 "platform mirror differs from canonical theme role");
        else
            ++passed;
    }
    CHECK(found == 2);
}

} // namespace

int main() {
    const std::filesystem::path uiDir{ARTCADE_UI_RESOURCE_DIR};
    const std::filesystem::path sourceDir{ARTCADE_UI_SOURCE_DIR};
    CHECK(std::filesystem::is_directory(uiDir));
    CHECK(std::filesystem::is_directory(sourceDir));

    std::vector<std::filesystem::path> sheets;
    std::vector<std::filesystem::path> documents;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(uiDir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() == ".rcss") sheets.push_back(entry.path());
        if (entry.path().extension() == ".rml") documents.push_back(entry.path());
    }
    std::sort(sheets.begin(), sheets.end());
    CHECK(sheets.size() >= 12);
    CHECK(std::any_of(sheets.begin(), sheets.end(), [](const auto& path) {
        return path.generic_string().find("sprite-animation/sprite_animation_editor.rcss")
            != std::string::npos;
    }));
    CHECK(std::any_of(sheets.begin(), sheets.end(), [](const auto& path) {
        return path.generic_string().find("tileset/tileset_editor.rcss")
            != std::string::npos;
    }));

    std::map<std::string, std::string> resolvedRoles;
    for (const auto& sheet : sheets) {
        const std::string source = readFile(sheet);
        const SplitSource split = splitComments(source);
        if (sheet.filename() == "theme.rcss") {
            scanThemeRoles(sheet, source, resolvedRoles);
        } else {
            for (const std::string& colour : colourLiterals(split.declarations))
                fail(sheet, 1, "colour " + colour + " belongs in theme.rcss");
            for (const std::string& colour : namedColourAssignments(split.declarations))
                fail(sheet, 1, "named colour " + colour + " belongs in theme.rcss");
            if (hasColourFunction(split.declarations))
                fail(sheet, 1, "functional colour belongs in theme.rcss");
        }
        scanMetrics(sheet, source);
    }
    CHECK(resolvedRoles.size() == roles().size());

    for (const auto& document : documents) scanRml(document, readFile(document));

    for (const auto& entry : std::filesystem::directory_iterator(sourceDir / "ui")) {
        if (!entry.is_regular_file()) continue;
        const std::string extension = lower(entry.path().extension().string());
        if (extension == ".cpp" || extension == ".h")
            scanGeneratedMarkup(entry.path(), readFile(entry.path()));
    }
    scanPlatformMirrors(sourceDir / "ui" / "window_chrome_theme.h",
                        readFile(sourceDir / "ui" / "window_chrome_theme.h"), resolvedRoles);
    CHECK(std::filesystem::is_regular_file(
        uiDir.parent_path() / "fonts" / "tabler" / "tabler-icons.ttf"));

    // Negative fixtures: each logical scanner must be capable of failing.
    CHECK(colourLiterals("color: #1f1f23;").size() == 1);
    CHECK(colourLiterals("#btn-play-project { display:block; }").empty());
    CHECK(colourLiterals("&#xeb41;").empty());
    CHECK(containsPuaReference("&#xeb41;"));
    CHECK(containsPuaReference("&#60225;"));
    CHECK(!containsPuaReference("&#8230;"));
    CHECK(forbiddenInlineColour("<div style=\"background:#fff\"></div>"));
    CHECK(forbiddenGeneratedColour("el->SetProperty(\"border-color\", value);"));
    CHECK(!forbiddenGeneratedColour("html += \"<span>&#xeb41;</span>\";"));
    CHECK(metricApproved("spacing", "padding", "0 4dp 12dp"));
    CHECK(!metricApproved("spacing", "padding", "5dp"));
    CHECK(!metricApproved("radius", "border-radius", "50%"));
    CHECK(!metricApproved("type-size", "font-size", "9dp"));
    CHECK(!metricApproved("type-weight", "font-weight", "bold"));
    CHECK(!metricApproved("motion", "transition", "color 0.2s"));

    std::cout << "ui-stylesheet-tokens-test: " << passed << " passed, "
              << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
