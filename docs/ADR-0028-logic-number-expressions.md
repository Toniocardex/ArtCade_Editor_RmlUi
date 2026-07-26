# ADR-0028 — Typed Number Expressions (Logic Board)

**Status:** Accepted  
**Date:** 2026-07-25  
**Scope:** Shared `NumberExpression` AST, `LogicVec2Value`, Logic Board schema 4,
project format 11, Set Position component expressions, editor Expression UI  
**Related:** [`LOGIC_BOARD_RULES_ROADMAP.md`](LOGIC_BOARD_RULES_ROADMAP.md),
ADR-0025 (scene actions — unrelated), ADR-0026 (Destroy Other), ADR-0027
(UI design tokens), Constitution §21

## Context

Numeric Logic Board action parameters are static literals. Authors need
structured, typed number expressions (Random Range, Clamp, Lerp, Self/Scene
properties, variables, arithmetic) without proliferating specialised Actions
and without arbitrary Lua formula strings.

## Decision

### Ownership (acyclic)

| Layer | Owns |
|---|---|
| `artcade-core` | `NumberExpression`, `LogicVec2Value`, deep copy / equality |
| `artcade-logic-core` | validation, `LiteralOnly` enforcement, expression JSON codec, board schema 3→4, formatter, catalog policy, Lua expression compiler |
| `artcade-logic-runtime` | `logic.number.*`, board-local RNG, finite guards, rate-limited diagnostics |
| `editor-native` | Commands, property editor, Expression modal, Coordinator diagnostics projection |

No editor-native AST copy. No parallel C++ evaluator. UI does not call the Lua
code generator.

`artcade-core` is a STATIC library with PUBLIC includes. `logic-number-expression.h`
must not include logic-core, JSON, Lua, RmlUi, Raylib, or editor-native.

### Catalog shape

Set Position keeps one Vec2 parameter `position`. Internally:

```text
LogicVec2Value { NumberExpression x; NumberExpression y; }
```

`LogicValueKind::Vec2` is unchanged; the `LogicValue` variant arm is
`LogicVec2Value` only (never both `Vec2` and `LogicVec2Value`).

`NumericExpressionPolicy` is appended at the end of `LogicPropertyDescriptor`
(default `LiteralOnly`). At ADR-0028 acceptance only `entity.set_position` /
`position` used `PerComponentNumberExpression`. Later flips (ADR-0029 / catalog
updates) also enable Move By `offset` and Set Velocity `velocity`; Set Scale and
Spawn remain `LiteralOnly` until those policies change. Enforcement is shared
across StructuralCommit, AuthoringDiagnostics, and Executable.

### Versions

| Authority | Value |
|---|---|
| Editor / runtime project format | 11 |
| Export template min / max | 11 / 11 |
| Logic Board schema | 4 |
| Logic API | 2 |

Board reader: schema 3 → numeric vec2 → Literal normalize → in-memory 4;
schema 4 → number or expression object. Writer always emits 4.
Project migrate 10→11 is editor-owned; exported runtime is current-only.

### Runtime

Expressions compile to Lua. Random uses board-local deterministic RNG (never
`math.random`). Set Position evaluates X then Y and applies only if both are
finite.

### UI / theme

Position remains one visual group with per-component literal or fx summary.
Expression modal drafts a single component. Colours only via ADR-0027 role
groups in `theme.rcss`. Feature RCSS is structure-only. Flex parents wrap all
labels in escaped `<span>` elements.

Expression Context (`NumberExpressionContext`) supplies availability flags and
Number variable catalogs (local / global). The Expression picker is categorized
(VALUE / CONTEXT / MATH / RANDOM); Local/Global Variable entries appear only when
compatible Number variables exist; Delta Seconds appears only when the trigger
provides it.

## Consequences

Number parameters become expression-capable without splitting catalog Vec2
parameters. First vertical slice enables Set Position only; other Vec2 Actions
reject dynamic expressions until later slices flip their policy.
