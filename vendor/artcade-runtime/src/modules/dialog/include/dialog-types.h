#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace ArtCade::Modules {

enum class DialogNodeType {
    Say,
    Choice,
    Condition,
    SetVariable,
    EmitEvent,
    End,
    Unknown,
};

struct DialogChoiceOption {
    std::string text;
    std::string next;
};

struct DialogNode {
    std::string id;
    DialogNodeType type = DialogNodeType::Unknown;

    // say
    std::string character;
    std::string text;
    std::string textKey;
    std::string portrait;
    std::string next;

    // choice
    std::vector<DialogChoiceOption> options;

    // condition
    std::string variable;
    std::string op;
    float       value = 0.f;
    std::string ifTrue;
    std::string ifFalse;

    // setVariable
    std::string operation;

    // emitEvent
    std::string event;
};

struct DialogGraph {
    std::string dialogId;
    std::string startNode;
    std::unordered_map<std::string, DialogNode> nodes;
};

struct TypewriterState {
    std::string fullText;
    std::string visibleText;
    float       timer    = 0.f;
    bool        complete = false;
};

enum class DialogWaitPhase {
    None,
    Text,
    Choice,
};

} // namespace ArtCade::Modules
