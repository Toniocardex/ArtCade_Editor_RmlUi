// The invariant behind three separate regressions (805d596, 80139e4, and the
// Assets row menu that shipped with icon-only rows): RmlUi's flexbox does NOT
// generate anonymous flex items, so a bare text node inside a `display: flex`
// element is laid out nowhere and the label is simply invisible.
//
// Reviewing markup by eye does not catch this — the RML looks correct, and the
// only symptom is a menu of unlabelled icons. So derive the rule from the
// shipped sources instead: every class/id the stylesheets make flex is a
// container whose text must live in an element child.
//
// The flex set is read from the .rcss rather than hard-coded: a panel that
// becomes flex tomorrow is covered without anyone remembering to update a list.
//
// Scope: the authored .rml documents. Markup that C++ stamps in with
// SetInnerRML is not visible here; the component gallery
// (--shot-gallery / component_gallery.cpp) is where those are eyeballed.

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

bool isSpace(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

bool isBlank(const std::string& text) {
    return std::all_of(text.begin(), text.end(), [](char c) { return isSpace(c); });
}

std::string stripRcssComments(const std::string& text) {
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t open = text.find("/*", i);
        if (open == std::string::npos) { out += text.substr(i); break; }
        out += text.substr(i, open - i);
        const std::size_t close = text.find("*/", open + 2);
        if (close == std::string::npos) break;
        i = close + 2;
    }
    return out;
}

std::string trim(const std::string& text) {
    std::size_t b = 0, e = text.size();
    while (b < e && isSpace(text[b])) ++b;
    while (e > b && isSpace(text[e - 1])) --e;
    return text.substr(b, e - b);
}

std::vector<std::string> split(const std::string& text, char sep) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : text) {
        if (c == sep) { parts.push_back(current); current.clear(); }
        else current += c;
    }
    parts.push_back(current);
    return parts;
}

bool isSelectorChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_';
}

/** True when @p body declares `display: flex`. */
bool declaresFlex(const std::string& body) {
    const std::size_t at = body.find("display");
    if (at == std::string::npos) return false;
    std::size_t i = at + 7;
    while (i < body.size() && isSpace(body[i])) ++i;
    if (i >= body.size() || body[i] != ':') return false;
    ++i;
    while (i < body.size() && isSpace(body[i])) ++i;
    return body.compare(i, 4, "flex") == 0;
}

/** Flex selector sets from one stylesheet. Only whole-selector `.class` /
 *  `#id` forms are taken: a descendant or compound selector applies to some
 *  elements of that class and would produce false alarms on the others. */
struct FlexSelectors {
    std::set<std::string> classes;
    std::set<std::string> ids;
};

void collectFlexSelectors(const std::string& sheet, FlexSelectors& out) {
    const std::string text = stripRcssComments(sheet);
    std::size_t i = 0;
    while (true) {
        const std::size_t open = text.find('{', i);
        if (open == std::string::npos) break;
        const std::size_t close = text.find('}', open + 1);
        if (close == std::string::npos) break;
        const std::size_t selectorStart = text.find_last_of('}', open) == std::string::npos
            ? 0 : text.find_last_of('}', open) + 1;
        const std::string selector = text.substr(selectorStart, open - selectorStart);
        const std::string body = text.substr(open + 1, close - open - 1);
        i = close + 1;
        if (!declaresFlex(body)) continue;
        for (const std::string& part : split(selector, ',')) {
            const std::string one = trim(part);
            if (one.size() < 2) continue;
            const bool simple = std::all_of(one.begin() + 1, one.end(), isSelectorChar);
            if (!simple) continue;
            if (one[0] == '.') out.classes.insert(one.substr(1));
            else if (one[0] == '#') out.ids.insert(one.substr(1));
        }
    }
}

struct Violation {
    int line = 0;
    std::string owner;  // the flex element the text landed in
    std::string text;
};

/** Elements that never open a scope, so their content is not a text run. */
bool isVoidTag(const std::string& name) {
    return name == "br" || name == "img" || name == "input"
        || name == "link" || name == "meta" || name == "hr";
}

std::vector<Violation> scanDocument(const std::string& doc, const FlexSelectors& flex) {
    struct Open {
        std::vector<std::string> classes;
        std::string id;
        std::string name;
    };
    std::vector<Violation> hits;
    std::vector<Open> stack;
    std::size_t i = 0;
    std::size_t textStart = 0;
    while (i < doc.size()) {
        const std::size_t open = doc.find('<', i);
        if (open == std::string::npos) break;
        const std::string run = doc.substr(textStart, open - textStart);
        if (!isBlank(run) && !stack.empty()) {
            const Open& parent = stack.back();
            const bool flexParent =
                flex.ids.count(parent.id) > 0
                || std::any_of(parent.classes.begin(), parent.classes.end(),
                               [&](const std::string& c) { return flex.classes.count(c) > 0; });
            if (flexParent) {
                hits.push_back(Violation{
                    static_cast<int>(std::count(doc.begin(), doc.begin() + open, '\n')) + 1,
                    parent.id.empty() ? "." + parent.classes.front() : "#" + parent.id,
                    trim(run)});
            }
        }

        if (doc.compare(open, 4, "<!--") == 0) {
            const std::size_t end = doc.find("-->", open + 4);
            if (end == std::string::npos) break;
            i = textStart = end + 3;
            continue;
        }

        // Attribute values may hold '>', so close the tag outside quotes only.
        std::size_t j = open + 1;
        bool inQuote = false;
        while (j < doc.size() && (inQuote || doc[j] != '>')) {
            if (doc[j] == '"') inQuote = !inQuote;
            ++j;
        }
        if (j >= doc.size()) break;
        const std::string tag = doc.substr(open + 1, j - open - 1);
        i = textStart = j + 1;
        if (tag.empty() || tag[0] == '!' || tag[0] == '?') continue;

        if (tag[0] == '/') {
            if (!stack.empty()) stack.pop_back();
            continue;
        }
        std::size_t nameEnd = 0;
        while (nameEnd < tag.size() && isSelectorChar(tag[nameEnd])) ++nameEnd;
        const std::string name = tag.substr(0, nameEnd);
        if (!tag.empty() && tag.back() == '/') continue;  // self-closing
        if (isVoidTag(name)) continue;

        Open element;
        element.name = name;
        const auto attribute = [&](const char* key) {
            const std::string needle = std::string(key) + "=\"";
            const std::size_t at = tag.find(needle);
            if (at == std::string::npos) return std::string{};
            const std::size_t start = at + needle.size();
            const std::size_t end = tag.find('"', start);
            if (end == std::string::npos) return std::string{};
            return tag.substr(start, end - start);
        };
        element.id = attribute("id");
        std::istringstream classes(attribute("class"));
        std::string one;
        while (classes >> one) element.classes.push_back(one);
        if (element.classes.empty()) element.classes.push_back(name);
        stack.push_back(std::move(element));
    }
    return hits;
}

} // namespace

int main() {
    const std::filesystem::path uiDir{ARTCADE_UI_RESOURCE_DIR};
    CHECK(std::filesystem::is_directory(uiDir));

    FlexSelectors flex;
    int sheets = 0;
    for (const auto& entry : std::filesystem::directory_iterator(uiDir)) {
        if (entry.path().extension() != ".rcss") continue;
        collectFlexSelectors(readFile(entry.path()), flex);
        ++sheets;
    }
    CHECK(sheets >= 8);
    // The suite is worthless if it derived an empty rule set; .context-entry is
    // the row this invariant keeps breaking on, so name it explicitly.
    CHECK(flex.classes.count("context-entry") == 1);
    CHECK(flex.classes.size() >= 40);

    int documents = 0;
    for (const auto& entry : std::filesystem::directory_iterator(uiDir)) {
        if (entry.path().extension() != ".rml") continue;
        ++documents;
        const std::string name = entry.path().filename().string();
        for (const Violation& hit : scanDocument(readFile(entry.path()), flex)) {
            ++failed;
            std::cerr << "FAIL " << name << ":" << hit.line << " bare text \""
                      << hit.text << "\" inside flex " << hit.owner
                      << " — RmlUi lays out no anonymous flex item, so this "
                         "label is invisible; wrap it in a <span>\n";
        }
        ++passed;
    }
    CHECK(documents >= 3);

    // A scanner that cannot fail enforces nothing: exercise both verdicts on
    // the exact shapes the real regressions took.
    FlexSelectors fixture;
    fixture.classes = {"context-entry"};
    fixture.ids = {"floating-row"};
    CHECK(scanDocument(
        "<div class=\"context-entry\"><span class=\"icon\">x</span>Delete</div>",
        fixture).size() == 1);
    CHECK(scanDocument(
        "<div class=\"context-entry\"><span class=\"icon\">x</span><span>Delete</span></div>",
        fixture).empty());
    CHECK(scanDocument("<div class=\"menu-entry\">Delete</div>", fixture).empty());
    CHECK(scanDocument("<div id=\"floating-row\">Delete</div>", fixture).size() == 1);
    CHECK(scanDocument("<div class=\"context-entry\">\n  <span>Ok</span>\n</div>",
                       fixture).empty());
    // Nesting must not leak: text in a plain child of a flex row is fine.
    CHECK(scanDocument(
        "<div class=\"context-entry\"><div class=\"sub\">Move to Layer</div></div>",
        fixture).empty());
    // An attribute holding '>' must not be mistaken for the end of the tag.
    CHECK(scanDocument("<div class=\"context-entry\" title=\"a > b\"><span>Ok</span></div>",
                       fixture).empty());

    std::cout << "ui-markup-flex-text-test: " << passed << " passed, "
              << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
