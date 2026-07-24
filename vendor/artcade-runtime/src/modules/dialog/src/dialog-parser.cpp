#include "../include/dialog-parser.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>

using json = nlohmann::json;

namespace ArtCade::Modules {

namespace {

DialogNodeType parseNodeType(const std::string& s) {
    if (s == "say")          return DialogNodeType::Say;
    if (s == "choice")       return DialogNodeType::Choice;
    if (s == "condition")    return DialogNodeType::Condition;
    if (s == "setVariable")  return DialogNodeType::SetVariable;
    if (s == "emitEvent")    return DialogNodeType::EmitEvent;
    if (s == "end")          return DialogNodeType::End;
    return DialogNodeType::Unknown;
}

std::string readNext(const json& j) {
    if (j.contains("next") && !j["next"].is_null())
        return j["next"].get<std::string>();
    return {};
}

DialogNode parseNode(const std::string& id, const json& j) {
    DialogNode n;
    n.id   = id;
    n.type = parseNodeType(j.value("type", ""));
    n.next = readNext(j);

    switch (n.type) {
    case DialogNodeType::Say:
        n.character = j.value("character", "");
        n.text      = j.value("text", "");
        n.textKey   = j.value("textKey", "");
        n.portrait  = j.value("portrait", "");
        break;
    case DialogNodeType::Choice:
        if (j.contains("options") && j["options"].is_array()) {
            for (const auto& opt : j["options"]) {
                DialogChoiceOption o;
                o.text = opt.value("text", "");
                o.next = opt.value("next", "");
                n.options.push_back(std::move(o));
            }
        }
        break;
    case DialogNodeType::Condition:
        n.variable = j.value("variable", "");
        n.op       = j.value("operator", j.value("op", "=="));
        n.value    = j.value("value", 0.f);
        n.ifTrue   = j.value("ifTrue", "");
        n.ifFalse  = j.value("ifFalse", "");
        break;
    case DialogNodeType::SetVariable:
        n.variable   = j.value("variable", "");
        n.operation  = j.value("operation", "=");
        n.value      = j.value("value", 0.f);
        break;
    case DialogNodeType::EmitEvent:
        n.event = j.value("event", "");
        break;
    case DialogNodeType::End:
        break;
    default:
        break;
    }
    return n;
}

} // namespace

DialogParseResult DialogParser::parseJsonString(const std::string& jsonText) {
    DialogParseResult out;
    try {
        json root = json::parse(jsonText);
        out.graph.dialogId  = root.value("dialogId", "");
        out.graph.startNode = root.value("startNode", "");

        if (!root.contains("nodes") || !root["nodes"].is_object()) {
            out.error = "missing or invalid 'nodes' object";
            return out;
        }

        for (auto it = root["nodes"].begin(); it != root["nodes"].end(); ++it) {
            const std::string nodeId = it.key();
            if (out.graph.nodes.count(nodeId)) {
                out.error = "duplicate node id: " + nodeId;
                return out;
            }
            out.graph.nodes[nodeId] = parseNode(nodeId, it.value());
        }

        if (out.graph.dialogId.empty())
            out.graph.dialogId = root.value("id", "");

        out.error = validate(out.graph);
    } catch (const std::exception& ex) {
        out.error = std::string("JSON parse error: ") + ex.what();
    }
    return out;
}

DialogParseResult DialogParser::parseFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        DialogParseResult out;
        out.error = "cannot open file: " + path;
        return out;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return parseJsonString(ss.str());
}

std::string DialogParser::validate(const DialogGraph& graph) {
    if (graph.dialogId.empty())
        return "dialogId is required";
    if (graph.startNode.empty())
        return "startNode is required";
    if (!graph.nodes.count(graph.startNode))
        return "startNode not found: " + graph.startNode;

    bool hasEnd = false;
    for (const auto& [id, node] : graph.nodes) {
        if (node.type == DialogNodeType::Unknown)
            return "unknown node type at id: " + id;
        if (node.type == DialogNodeType::End)
            hasEnd = true;

        auto checkRef = [&](const std::string& ref) {
            if (!ref.empty() && !graph.nodes.count(ref))
                return "orphan reference from " + id + " to " + ref;
            return std::string{};
        };

        if (auto err = checkRef(node.next); !err.empty()) return err;
        if (auto err = checkRef(node.ifTrue); !err.empty()) return err;
        if (auto err = checkRef(node.ifFalse); !err.empty()) return err;
        for (const auto& opt : node.options) {
            if (auto err = checkRef(opt.next); !err.empty()) return err;
        }
        if (node.type == DialogNodeType::Choice && node.options.empty())
            return "choice node has no options: " + id;
    }
    if (!hasEnd)
        return "graph has no end node";
    return {};
}

} // namespace ArtCade::Modules
