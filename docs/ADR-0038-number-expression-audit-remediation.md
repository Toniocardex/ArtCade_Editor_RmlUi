# ADR-0038 — Number Expression Audit: Three Correctness Holes

**Status:** Implemented — see "Implementation status" below
**Date:** 2026-07-28
**Scope:** `artcade-logic-core` — the Lua codegen literal writer
(`logic-number-expression-compiler.cpp`, scalar emitters in `logic-core.cpp`),
the recursive-descent parser (`logic-number-expression-parse.cpp`), and
`NumericExpressionPolicy` enforcement in `validateBoard`; plus **one new
`logic.number.round` binding in `artcade-logic-runtime`** (Finding 4). No
schema, JSON, project format, or AST change. One editor-side input guard.
**Related:** ADR-0028 (typed number expressions, AST + ownership),
ADR-0029 (text authoring, `Code` syntax, per-descriptor policy),
Constitution §21 / AC-LUA-001, §23 / AC-AI-001 (no silent fallbacks)

## Context

An audit of the expression system was run over the whole pipeline — AST,
parser, formatter, Lua compiler, JSON codec, validation, runtime bindings,
and the editor field. Findings below were **reproduced by execution**, not
inferred from reading, using three probes compiled against the shipped
`artcade-logic-core` / `artcade-core` libraries.

### What the audit found healthy

Recorded so the remediation does not disturb it, and so a future audit does
not re-derive it:

- **Precedence and minimal parenthesisation round-trip exactly.** 16 cases,
  zero drift: `1-(2*3)` → `1 - 2 * 3`, `1-(2-3)` keeps its parentheses,
  `1-2-3` stays left-associative, `-self.x` normalises to `-(self.x)`.
- **Malformed input is handled cleanly.** 25 cases (unterminated calls,
  stray operators, `1..2`, `1e`, `()`, wrong arity) each produced a
  specific message, no crash and no accidental parse.
- **Variable quoting/escaping round-trips**, including `'`, `\`, spaces,
  dots, parentheses, leading digits, and the literal name `global.x`, in
  both Local and Global scope.
- **All 14 completion entries parse.**
- **Division by zero is defended in three layers**: validator
  (`NE_DIVIDE_ZERO` on literal denominators), codegen
  (`logic.number.divide`), runtime (NaN + `is_finite` guard + rate-limited
  diagnostic).
- **The ADR-0029 "Known gap"** — an uncommitted draft not resolved before
  Save/Open/New/Play — **is already fixed**;
  [`editor_ui.cpp:1102`](../src/editor-native/ui/editor_ui.cpp) calls
  `resolvePendingExpressionEdit()` first. That ADR's text is stale.
- Suites green: 57/57 ctest, 155 syntax assertions, 1228 logic-board
  assertions.

## Finding 1 — Generated Lua silently rounds every literal to 6 digits

`compileNumberExpressionToLua` writes doubles with a bare
`std::ostringstream <<` ([`logic-number-expression-compiler.cpp:22`](../vendor/artcade-runtime/src/modules/logic-core/src/logic-number-expression-compiler.cpp)),
which uses the stream default of **6 significant digits**. The scalar
emitters in `logic-core.cpp` do the same (`lua << radians` at line 550,
`lua << literalNumberOf(p).value_or(...)` at 626 and elsewhere).

`std::setprecision` appears **nowhere in any codegen path** — only in the
formatter, whose `codeLiteral()` deliberately widens precision (6 → 15 → 16
→ 17) until the text reads back as the same double
([`logic-number-expression-format.cpp:21-31`](../vendor/artcade-runtime/src/modules/logic-core/src/logic-number-expression-format.cpp)).

So the display path is lossless and the execution path is not. Measured:

| author types | field shows | generated Lua | game runs |
|---|---|---|---|
| `100000.5` | `100000.5` | `100000` | 100000 |
| `1234567` | `1234567` | `1.23457e+06` | 1234570 |
| `1.0000001` | `1.0000001` | `1` | 1 |
| `123456789.25` | `123456789.25` | `1.23457e+08` | 123457000 |
| `3.14159265358979` | `3.14159265358979` | `3.14159` | 3.14159 |

The emitted text is always *valid* Lua, so nothing fails — it just computes
a different number than the one the author is looking at. This is precisely
the hazard ADR-0029 named when it said the formatter had become
load-bearing, except it landed on the side that ADR did not cover.

**The Lua compiler is the odd one out, not a general oversight.** A literal
travels three paths — to disk, to the field, to the interpreter — and the
other two were measured and are lossless. The JSON codec round-trips every
value exactly, including denormals, `-0.0`, `1e308` and
`0.49999999999999994` (nlohmann serialises shortest-round-trip); the
formatter round-trips by construction. So the value an author typed is
stored correctly and displayed correctly, and is wrong only in the artifact
that decides what happens. That is also why the bug is invisible in review:
every place a human looks agrees.

### Decision

One shortest-round-trip `double` → string function, owned by
`artcade-logic-core`, used by **both** the formatter and every codegen
site. The existing `codeLiteral` body is the implementation; it moves out of
the format translation unit's anonymous namespace into the module's public
surface.

The function carries its invariant in its contract, not in a comment on one
caller: `strtod(f(x)) == x` for every finite `x`. The display path uses it
*because* display must also round-trip — not the other way round — so a
future cosmetic change to how numbers are shown cannot silently alter what
is executed.

Non-finite input must not reach it (validation rejects non-finite literals
first), but `<<` on an infinity produces `inf`, which is **not** valid Lua
and would turn a data error into a syntax error in generated code. The
shared function therefore asserts/guards finiteness explicitly rather than
relying on every caller having been validated.

## Finding 2 — The parser can be made to overflow the C++ stack

The depth budget is enforced **only inside `make()`**
([`logic-number-expression-parse.cpp:88-94`](../vendor/artcade-runtime/src/modules/logic-core/src/logic-number-expression-parse.cpp)),
which runs when an AST node is *constructed* — at the bottom of the
descent. Every recursive path (`parsePrimary` → `parseExpression` on `(`,
`parseCall` → `parseExpression` per argument, `parseFactor` → itself on
unary `-`) descends first and checks later, so the descent itself is
unbounded. A `(` also constructs no node, so the node budget (128) never
fires either.

Measured (Release `/O2 /MT`, same flags as the shipped editor):

| input | 500 | 2000 |
|---|---|---|
| `((((1))))` nested | parsed ok | **process killed** |
| `abs(abs(…1…))` nested | rejected: "nested too deeply" | **process killed** |
| `((((` unterminated | rejected: "incomplete" | **process killed** |
| `-----self.x` | rejected: "nested too deeply" | rejected |

The paren case crashes between 700 and 1000 levels — about **2 KB of
input**. Unary happens to survive 2000 because its frames are small, but it
is the same defect, not a different one.

This is reachable from the shipped editor: `commitExpressionText` calls the
parser on the raw field text with no length or nesting guard
([`logic_board_editor_controller.cpp:168`](../src/editor-native/ui/logic_board_editor_controller.cpp)).
Pasting ~2 KB of `(` into an expression field terminates ArtCade Studio and
takes unsaved work with it. It is not a security boundary — it is the
author's own editor — but a paste that kills the app is a defect regardless
of intent.

### Decision

**Bound the descent, at the point of descent.** A recursion counter is
incremented on entry to `parseExpression` and `parseFactor` (which between
them dominate every recursive path) and released on exit; exceeding the
limit fails with the existing "Expression is nested too deeply" message.

The limit is a **new constant, separate from `kMaximumNumberExpressionDepth`**.
Reusing the AST depth budget (16) would be wrong twice over: grammar
nesting and tree depth are not the same count — `random(0, clamp(…))` costs
call parentheses *and* nodes — so folding them together would start
rejecting expressions that are valid today. A separate, comfortably larger
grammar-recursion cap (order of 64) only ever fires on input that the AST
depth budget would have rejected anyway, so **no currently-accepted
expression changes behaviour**. That property is the point of separating
them, and is worth asserting in a test rather than assuming.

Additionally, the editor caps the length of text it hands the parser. That
is defence in depth, not the fix — the parser must be safe on any input it
is given, because it is also reachable from `--shot-expression`, tests, and
any future importer.

## Finding 3 — `LiteralOnly` is not enforced on scalar number properties

The policy check sits **inside** the `LogicVec2Value` branch
([`logic-core.cpp:238-249`](../vendor/artcade-runtime/src/modules/logic-core/src/logic-core.cpp)).
A scalar `NumberExpression` property is covered only by a hardcoded Camera
Shake special case (line 277). Every other scalar has **no policy
enforcement at all**.

Codegen then reads those properties through
`literalNumberOf(p).value_or(<default>)`, which returns `nullopt` for a
dynamic expression exactly as it does for an absent property — so the
default is substituted and the expression is discarded without a word.

Measured end-to-end (validate → compile → inspect emitted Lua):

| board | validation | compile | emitted |
|---|---|---|---|
| `set_rotation.degrees = random(0,360)` | **no diagnostics** | OK | `set_rotation(0)` |
| `rotate_by.degrees = random(0,360)` | **no diagnostics** | OK | `rotate_by(0)` |
| `set_playback_speed.speed = random(1,2)` | **no diagnostics** | OK | `…playback_speed(1)` |
| `flow.wait.seconds = random(1,5)` | **no diagnostics** | OK | expression dropped |
| *control:* `set_scale.scale = random(1,2)` (Vec2) | `NE_LITERAL_ONLY` | blocked | — |

The JSON codec accepts and preserves such a value — `logic-board-test.cpp`'s
`testScalarExpressionValueSurvivesJson` deliberately writes one and asserts
it round-trips — so the format supports a state that validation waves
through and codegen silently mistranslates. A file carrying it is displayed
correctly, saved correctly, and executed as a constant.

Through today's editor UI an author cannot *create* this: the expression
field is only offered on the three Vec2 parameters whose policy is
`PerComponentNumberExpression`. The exposure is loaded files, and the next
slice: ADR-0029's remaining work is exactly to make nine scalars
expression-capable, and whoever flips those policies will get silent zeros
with no failing test.

`.value_or(default)` on a value the compiler could not translate is a silent
fallback, which Constitution §23 lists among the things that must not be
accepted.

### The same fallback exists on the Vec2 path

`emitGuardedVec2` substitutes a literal `0` whenever compilation of a
component fails ([`logic-core.cpp:78-79`](../vendor/artcade-runtime/src/modules/logic-core/src/logic-core.cpp)):

```cpp
lua << "        local _x = " << (compiledX.ok ? compiledX.luaSource : "0") << "\n";
```

Reachability is narrower than the scalar case — `compileNumberExpressionToLua`
only fails on a null child box, which validation rejects as `NE_INCOMPLETE`
— so this is defence-in-depth rather than a live defect. It is called out
because it is the *same* pattern on the path that **is** reachable from the
editor today (Set Position, Move By, Set Velocity), and because a fix
written only against the scalar sites would walk straight past it. Both
sites are one decision, not two.

An untranslatable component must fail the compile with the compiler's own
error, which `CompiledNumberExpression::error` already carries and this
caller currently discards.

### Decision

**Enforce policy on the property, not on the value kind.** The
`numericExpressionPolicy` check moves out of the Vec2 branch and applies to
any property holding a `NumberExpression`:

- `LiteralOnly` + non-literal expression → `NE_LITERAL_ONLY`, same code and
  message the Vec2 path already emits.
- `PerComponentNumberExpression` → full `validateNumberExpression` against
  the Expression Context.

The Camera Shake special case is then redundant and is deleted rather than
left as a second, divergent copy of the same rule.

**Codegen stops conflating "absent" with "present but not a literal."**
`literalNumberOf` returns `nullopt` for both today, which is what lets the
default paper over a real translation failure. The two are separated so a
missing property may still take its documented default, while a property
holding something the emitter cannot translate is a compile error, not a
zero.

This ADR **does not** make scalars expression-capable — that remains
ADR-0029's slice. It makes the current `LiteralOnly` state actually hold,
which that slice needs as its floor: today the slice could be declared
"done" with codegen untouched and every test would still pass.

## Finding 4 — `round()` is not idempotent on integers, and mis-rounds one double

The compiler emits additive rounding
([`logic-number-expression-compiler.cpp:56-61`](../vendor/artcade-runtime/src/modules/logic-core/src/logic-number-expression-compiler.cpp)):

```
round(self.x)  ->  math.floor((context.self:get_position_x())+0.5)
```

Lua 5.4 numbers are IEEE-754 doubles and the arithmetic is identical, so the
emitted expression was evaluated under `/fp:strict`. Measured:

| input | emitted | correct half-up | |
|---|---|---|---|
| `4503599627370497` (2^52+1) | `4503599627370498` | `4503599627370497` | **wrong** |
| `0.49999999999999994` | `1` | `0` | **wrong** |
| `2.5` / `3.5` | `3` / `4` | same | ok |
| `-2.5` / `-1.5` / `-0.5` | `-2` / `-1` / `0` | same | ok |

Two defects and one unspecified behaviour:

- **Not idempotent on whole numbers.** At and above 2^52 the gap between
  doubles exceeds 0.5, so `x + 0.5` rounds up to the next representable
  integer and `round()` *changes a value that was already integral*. Rounding
  a whole number must be identity.
- **`0.49999999999999994` → 1.** The largest double below 0.5, plus 0.5,
  rounds to exactly 1.0 before `floor` ever runs — the same defect Java's
  `Math.round` carried for years.
- **The tie rule is undocumented.** It is half-up, so it is asymmetric about
  zero (`-2.5` → -2, not -3). Nothing is broken — the vocabulary summary
  only promises "Round to the nearest whole number", which does not
  disambiguate ties — but an unspecified rule in a system whose determinism
  is a stated invariant (AC-LUA-001) is worth pinning rather than leaving to
  be rediscovered.

Practical reach is small: game coordinates do not approach 2^52, and the
denormal-adjacent case needs that exact double. It is included because
`round(` is in the completion list and Set Position / Move By / Set Velocity
accept expressions today, so this is live behaviour, not a hypothetical.

### Decision

**Move the operation into a `logic.number.round` runtime binding**, beside
`divide`, `clamp` and `lerp`, which are C++ bindings for exactly this reason.

The obvious in-Lua fix — `floor(x)` then compare the fractional part — is
**wrong here**, and the reason is the durable part of this finding: it needs
the operand **twice**, and the operand is an arbitrary expression that may
contain `random(...)` or a variable read. `round(random(0, 10))` would draw
from the board RNG twice and round a number it never returned. No emitter
may duplicate an operand; a binding evaluates it once by construction.

The tie rule stays **half-up** — changing it would silently alter existing
boards' behaviour on negative values — and gets written down in the
completion summary so the field documents it where authors look.

## Minor findings

| # | Finding | Decision |
|---|---|---|
| 5 | Parser and validator depth budgets disagree: `1+1+…` with 16 operators parses, then fails validation with `NE_DEPTH_LIMIT`. The parser's `depth` parameter does not track real tree depth for left-associative chains. | Accept the inconsistency for now; the input is rejected either way and the message is accurate. Revisit only if the diagnostic's *location* (board-level rather than inline in the field) proves confusing in use. Recorded so it is not re-discovered as new. |
| 6 | `parse(format(e, Code)) == e` has a counterexample: an empty variable name formats to `$''`, which does not parse. | Fix the round-trip invariant at its stated strength — either make `$''` parse or make the formatter refuse to emit it. Validation rejects empty `variableId` separately, so this is very likely unreachable; the reason to fix it is that ADR-0029 states the invariant holds for *every* expression, and an invariant with a known exception stops being usable as a guard. |
| 7 | Completion inside a quoted variable name inserts broken text: `numberExpressionTokenPrefix("$'my ")` returns `""` because the space ends the token scan, so applying a completion yields `$'my scene.width`. | Extend the prefix scan to recognise an unterminated quoted variable token. Low severity, self-inflicted only. |
| 8 | ADR-0029's "Known gap" section describes a draft-loss bug that has since been fixed. | Correct that ADR's text in the same change, marking the gap closed and naming the code that closes it. Stale "known bug" notes cost more than they document. |

## Rejected alternatives

- **Set `setprecision(17)` on the codegen stream instead of sharing a
  helper.** Fixes the value but makes every literal unreadable
  (`0.1` → `0.10000000000000001`) in the Generated Lua tab, which authors
  read. It also leaves two independent notions of "how a number is written"
  one edit apart from diverging again.
- **Cap only the editor field's input length.** Would hide Finding 2 in the
  one place it was observed while leaving the parser unsafe for every other
  caller. The guard is worth having, but not as the fix.
- **Reuse `kMaximumNumberExpressionDepth` for the parse-recursion cap.**
  Conflates grammar nesting with tree depth and would newly reject valid
  expressions — see Finding 2.
- **Make the nine scalar parameters expression-capable now, so the
  unenforced policy stops mattering.** That is a feature slice with a
  schema/format migration behind it (ADR-0029); shipping it to fix a
  validation hole would bundle a correctness fix with a format change. The
  hole is closed first, on its own.
- **Leave Finding 3 alone because the editor cannot produce the state.**
  The JSON codec can, an existing test writes one deliberately, and the next
  planned slice walks straight into it.

## Test plan

The three probes written for this audit become real tests; a finding
reproduced only by a throwaway program is a finding that regresses.

- **Literal fidelity** (`number-expression-syntax-test`): over a corpus
  covering the table in Finding 1 plus boundaries (`0`, `-0`, denormals,
  `1e308`, `0.49999999999999994`, 17-significant-digit values), assert
  `strtod(emitted) == original` for **all three** paths — formatter, Lua
  compiler, and JSON codec — and assert they agree with each other. JSON is
  lossless today, so its rows are a regression guard rather than a fix:
  the behaviour belongs to nlohmann, which is a dependency this repo does
  not control, and Finding 1 is what an unpinned literal path costs.
- **Parse recursion** (`number-expression-syntax-test`): each of the four
  shapes measured above at a size well past the cap returns a parse error
  and does not crash; and — the regression guard that matters — a corpus of
  legitimately nested expressions that parse today still parses.
- **`round()` semantics** (`logic-board-test`, through a running
  `LogicRuntime` so the real Lua runs, not a C++ model of it): the two
  measured wrong cases, idempotence over a set of integral doubles including
  2^52 boundaries, the half-up tie rule on both signs, and — the one that
  guards the design decision — `round(random(0, 10))` draws from the board
  RNG **once**, asserted by comparing against the RNG's next value.
- **Policy enforcement** (`logic-board-test`): for every catalog property
  whose policy is `LiteralOnly`, a board holding a dynamic expression there
  produces `NE_LITERAL_ONLY` and fails to compile. Table-driven over the
  catalog rather than a hand-listed set, so a property added later is
  covered without anyone remembering to add it.
- **Codegen refuses what it cannot translate**: a `LiteralOnly` property
  holding a dynamic expression fails compilation instead of emitting a
  default — asserted on the compile result, not by grepping the Lua. The
  same assertion covers a Vec2 component whose expression fails to compile,
  so `emitGuardedVec2` cannot quietly fall back to `0`.

## Consequences

- Values shown in the field and values executed by the game become the same
  number. Existing saved projects are unaffected on disk; boards whose
  literals exceeded 6 significant digits will *change behaviour* on the next
  Play — toward what the author actually typed. Worth stating plainly:
  this is a behaviour change, and the pre-fix behaviour was the bug.
- Pathological expression text is rejected instead of terminating the
  editor.
- Boards that today hold a dynamic expression on a `LiteralOnly` scalar
  start reporting `NE_LITERAL_ONLY` instead of silently running a default.
  Such a board is currently only producible by hand-editing or an external
  tool; if any exist, they were already not doing what they appear to do.
- `round()` starts returning the value it always should have for integral
  and near-tie inputs. Boards using `round` on ordinary game-scale numbers
  are unaffected — the wrong cases need |x| ≥ 2^52 or one specific double.
- No schema, JSON, project format, or AST change. The runtime gains one
  binding (`logic.number.round`), which is the only reason a Logic API
  version question arises at all — see open question 4.

## Definition of Done

- One shared round-trip literal writer; no bare `<<` of a `double` remains
  in any codegen path (checked by inspection of both emitters).
- Formatter and compiler produce the same text for the same literal.
- Non-finite reaching the literal writer is impossible or explicit, never
  `inf` in generated Lua.
- Parse recursion is bounded at the descent; all four measured shapes
  return an error rather than crashing.
- No expression that parses today fails to parse after the change.
- `numericExpressionPolicy` is enforced for scalar and Vec2 properties by
  one code path; the Camera Shake special case is gone.
- Codegen distinguishes an absent property from an untranslatable one and
  fails on the latter.
- No silent numeric fallback remains in codegen: neither
  `literalNumberOf(...).value_or(...)` on a present-but-dynamic property nor
  `emitGuardedVec2`'s `"0"`; the compiler's own error is surfaced instead of
  discarded.
- `round()` is idempotent on every integral double, `round(0.49999999999999994)`
  is 0, the tie rule is half-up and documented, and the operand is evaluated
  exactly once (asserted with a `random` operand, not by reading the Lua).
- Minor findings 6, 7 and 8 addressed; ADR-0029's stale gap note corrected.
- `scripts\build_runtime_tests.bat` green; `scripts\build.bat` builds the
  editor.
- WASM target still builds.

## Open questions (for review before implementation)

1. **Does any shipped board legitimately omit a number property and rely on
   the codegen default?** Validation emits `LB_MISSING_PROPERTY` and blocks
   compile in the cases probed, which suggests no — but that was not proven
   for every property, and it decides whether "absent" may keep its default
   or should also become an error. Needs a pass over the catalog during
   implementation.
2. **Exact value for the parse-recursion cap.** 64 is proposed as
   comfortably above anything the AST depth budget (16) would admit. If a
   test shows a legitimate expression approaching it, the constant is wrong,
   not the test.
3. **Should the literal-fidelity fix be backported into the export
   pipeline's template range?** Exported games regenerate Lua from the same
   compiler, so they inherit the fix on rebuild — but an already-exported
   bundle keeps the rounded literals. Whether that warrants anything beyond
   "re-export" is a product call, not a technical one.
4. **Does adding `logic.number.round` require a Logic API bump?** ADR-0028
   fixed the Logic API at 2. Generated Lua that calls the new binding fails
   on a runtime that predates it, which is exactly what the version exists
   to signal — but generated Lua is regenerated per build and the export
   pipeline already pins a template min/max, so the mismatch may be
   unreachable in practice. This was not traced through the export pipeline
   during the audit and should be settled before implementation rather than
   assumed either way; it is the only decision here that can invalidate a
   shipped bundle.

## Implementation status

Implemented.

- Finding 1: `numberLiteralText` (shortest round-trip `double` → string) moved
  to the module's public surface
  (`logic-number-expression-format.h`/`.cpp`) and is now the only place a
  `double` reaches an `ostringstream` in codegen — used by the formatter, the
  Lua compiler's literal case, and every codegen call site in `logic-core.cpp`
  that previously wrote a raw radians/volume/amount/etc. value.
- Finding 2: grammar-recursion cap (`kMaximumNumberExpressionParseDepth = 64`,
  `logic-number-expression-parse.h`) enforced at entry to `parseExpression`
  and `parseFactor` via a scope guard, separate from the existing AST depth
  budget. Editor-side length cap added in
  `logic_board_editor_controller.cpp::commitExpressionText` as defense in
  depth.
- Finding 3: `NumericExpressionPolicy` enforcement moved out of the Vec2-only
  branch in `validateBlock` (`logic-core.cpp`) to cover any property holding a
  `NumberExpression`; the Camera Shake special case is deleted. Codegen no
  longer conflates "absent" with "present but dynamic": `emitGuardedVec2` and
  every former `literalNumberOf(...).value_or(...)` site now fail the compile
  with a diagnostic (`LB_CODEGEN_EXPRESSION` / `LB_CODEGEN_LITERAL_ONLY`)
  instead of substituting a default or `0`.
- Finding 4: `logic.number.round` added as a C++ binding in
  `logic-runtime.cpp` (half-up, idempotent on integral doubles including at
  and above 2^52); `logic-number-expression-compiler.cpp`'s Round case now
  emits `logic.number.round(x)` instead of `math.floor((x)+0.5)`. Tie rule
  documented in the completion summary.
- Minor findings 6, 7 fixed in the parser (`$''` now parses; the token-prefix
  scan recognises an unterminated quoted variable). Minor finding 8: this
  ADR's own stale "Known gap" note in ADR-0029 is corrected.
- Tests: literal fidelity and parse-recursion tests added to
  `number-expression-syntax-test.cpp`; `round()` semantics (through a real
  `LogicRuntime`/Lua, including the single-RNG-draw guarantee) and table-driven
  `LiteralOnly` policy enforcement added to `logic-board-test.cpp`.
  `scripts\build_runtime_tests.bat`: 57/57 green. `scripts\build.bat`: editor
  builds clean.
- Open question 4 (Logic API version bump for the new binding) was **not**
  taken: the compiler and runtime ship from the same build, so no runtime
  predating `logic.number.round` can load Lua that calls it. Not traced
  through the export pipeline's template min/max — flagged for review rather
  than assumed, per the open question.
- WASM target verified: `vendor\artcade-runtime\build_wasm.bat` builds clean
  (`game.js` / `game.wasm`, Emscripten/Ninja/Release), including
  `artcade-logic-core` and `artcade-logic-runtime` with this change.
