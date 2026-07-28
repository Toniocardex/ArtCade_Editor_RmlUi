# ADR-0037 — Logic Board Random Session Entropy

**Status:** Accepted — implemented (see Implementation status)
**Date:** 2026-07-28
**Scope:** `LogicRuntime` scope RNG seeding (`vendor/artcade-runtime/src/modules/logic-runtime`);
new `GameplaySessionSeed` helper
(`vendor/artcade-runtime/src/app/src/gameplay_session_seed.h/.cpp`); the
`LogicRuntime` construction site in `GameplaySession` boot
(`vendor/artcade-runtime/src/app/src/gameplay_session.cpp`). No schema, JSON,
catalog, parser/compiler, or editor UI changes.
**Related:** ADR-0028 (introduced board-local deterministic RNG, `logic.random.range`,
`entity.set_position`), ADR-0029 (`random(min, max)` expression syntax and grammar),
Constitution §21 / AC-LUA-001 ("Il dispatch è deterministico e usa snapshot quando la
collezione può mutare")

## Context

Reported symptom: a `Set Position` action using `random(0, scene.width)` on an
`On Start` rule appears to "not work" — the object lands at the same X every
time the project is played, as if the expression were a fixed literal.

### What was ruled out

The full expression pipeline was traced end to end and is correct:

- Grammar and parser accept `random(min, max)` and `scene.width`
  (`logic-number-expression-parse.cpp:33,260-264,303-304`).
- The compiler emits `logic.random.range(context, <min>, <max>)`
  (`logic-number-expression-compiler.cpp:113-119`).
- `Set Position` codegen evaluates X and Y and applies only if both are
  finite, exactly as ADR-0028 specifies (`logic-core.cpp:73-87,531-537`,
  `emitGuardedVec2`).
- The Lua binding `logic.random.range` → `ContextProxy::randomRange` and
  `context:scene_world_width()` are wired and reachable
  (`logic-runtime.cpp:405-420,770-775,741`).
- `number_expression_syntax_test.exe` (155/155) and `logic_board_test.exe`
  (1087/1087) pass, including the JSON round-trip test for a `randomRange`
  expression over `scene.worldWidth`
  (`vendor/artcade-runtime/tests/logic-board-test.cpp:2156-2185`).

None of this explains the symptom. No test in the suite exercises
`logic.random.range` through an installed `LogicRuntime` scope and asserts a
*numeric* result — only syntax, validation, and JSON round-trip are covered.
That gap in coverage is itself part of what this ADR fixes (see Test plan).

### Root cause

The xorshift32 generator itself (`logic-runtime.cpp:412-419`) is correct and
advances normally. The per-scope RNG **seed** is a pure function of two
values that repeat identically on every Play, with no entropy input
anywhere:

```cpp
// logic-runtime.cpp:854-858 — LogicRuntime::install()
const ScopeToken token = impl_->nextScope++;
uint32_t seed = 0xA5A5A5A5u
    ^ static_cast<uint32_t>(owner) * 0x9E3779B9u
    ^ static_cast<uint32_t>(token) * 0x85EBCA6Bu;
if (seed == 0) seed = 1u;
```

- `nextScope` is an `Impl` member that always starts at `1`
  (`logic-runtime.cpp:466`).
- `Impl`, and therefore `LogicRuntime`, is constructed fresh on every Play —
  `logicRuntime_ = std::make_unique<Logic::LogicRuntime>(*logicHost_);`
  (`gameplay_session.cpp:413`) — and torn down on Stop
  (`gameplay_session.cpp:614`).
- `owner` (`EntityId`) is assigned by scene/entity setup, which reloads the
  same project file in the same order every Play, so the same entity gets
  the same id every time.
- Confirmed by direct search: `logic-runtime.cpp` contains no
  `std::random_device`, `std::chrono`, `time(nullptr)`, or any other entropy
  source.

Consequence: `random(0, scene.width)` on the Player's `On Start` rule
evaluates to the **same value on every single Play**, because `owner` and
`token` are identical every time. Within one Play session, different
entities/scopes *do* get different seeds and therefore different values
(e.g. several coins spawned in the same run land at different X) — the
determinism is only visible session-to-session, which is exactly the
reported symptom and easy to miss in code review because it looks correct
in isolation.

This is half of an intentional design: ADR-0028 explicitly bans
`math.random()` in favor of a private, per-scope generator, for reasons that
still hold (see Rejected alternatives). What's missing is the other half —
an entropy source feeding that generator once per session — which no ADR
ever decided to omit; it was simply never added.

## Decision

### Ownership

An earlier draft of this ADR proposed drawing entropy inside `LogicRuntime`
itself, behind an optional constructor parameter defaulting to
`std::nullopt`. Rejected in review: it puts a policy decision (where does
session randomness come from) inside a module whose ownership, per
ADR-0028, is the RNG *algorithm*, not session start-up policy. The corrected
split:

```text
GameplaySessionSeed → pure, testable function: produces one session seed
GameplaySession     → composition root: calls it once, decides when
LogicRuntime        → receives the seed, derives per-scope seeds,
                       owns and advances the per-scope generators
```

`GameplaySession` is already the place that constructs a new `LogicRuntime`
and wires up the other runtime services (`gameplay_session.cpp:413`), so it
is the natural composition root for this decision — the same reasoning
already used for `getSceneWorldSize()` and the rest of `ILogicRuntimeHost`.

### Constructor: mandatory, explicit seed

```cpp
explicit LogicRuntime(ILogicRuntimeHost& host,
                      uint32_t sessionSeed,
                      LogicRuntimeLimits limits = {});
```

No optional parameter, no implicit default inside `LogicRuntime`. Every
caller states its intent:

```cpp
// Production
LogicRuntime runtime(host, GameplaySessionSeed::make());

// Reproducible test
LogicRuntime runtime(host, 0x12345678u);

// Future replay
LogicRuntime runtime(host, replayMetadata.logicSessionSeed);
```

The current constructor takes only `host` and `limits`, so this change is
mechanical and localized to the one production call site plus any test/fake
that constructs a real `LogicRuntime` directly (fake `ILogicRuntimeHost`
implementations are unaffected — this only changes `LogicRuntime`'s own
constructor).

**Why not `std::optional<uint32_t> sessionSeed = std::nullopt`:** a test or
future headless tool that constructs `LogicRuntime` normally, without
thinking about randomness at all, would silently become non-deterministic.
A mandatory parameter guarantees: production randomness is chosen
explicitly, test determinism is chosen explicitly, `LogicRuntime` never
reads the clock or OS entropy itself, and no caller's behavior changes
because it happened to omit an argument.

### Session seed generation — `GameplaySessionSeed` helper

An earlier draft kept the generator as anonymous-namespace functions inside
`gameplay_session.cpp`, while also requiring a direct test asserting two
consecutive calls differ. Those two requirements conflict: a test outside
the translation unit cannot call into an anonymous namespace. Resolved by
extracting the pure functions into a small, separately testable unit:

```text
vendor/artcade-runtime/src/app/src/gameplay_session_seed.h
vendor/artcade-runtime/src/app/src/gameplay_session_seed.cpp
```

```cpp
// gameplay_session_seed.h
namespace ArtCade::GameplaySessionSeed {
uint32_t make() noexcept;
}
```

This is not a service, a manager, application-global state, or a new
runtime authority — it is one pure, testable function consumed by the
composition root.

**Generation algorithm.** The first draft combined a `steady_clock` reading,
an atomic counter, and `random_device` output with XOR, then compressed the
64-bit result to 32 bits. That does not actually guarantee two consecutive
calls differ: XOR-combining independent 64-bit sources and truncating to 32
bits can coincidentally collide, so "two consecutive calls always differ"
was not a provable property of that construction — only true with very high
probability. Corrected to a construction whose uniqueness bound is provable:

```cpp
// gameplay_session_seed.cpp
namespace {

uint32_t permuteSeed(uint32_t value) noexcept {
    // MurmurHash3 finalizer — a bijection on uint32_t, so distinct inputs
    // are guaranteed to stay distinct after mixing.
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

uint32_t makeProcessSeedMaterial() noexcept {
    uint64_t material = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());

    try {
        std::random_device device;
        material ^= static_cast<uint64_t>(device()) << 32;
        material ^= static_cast<uint64_t>(device());
    } catch (...) {
        // The clock remains the best-effort process differentiator.
    }

    return static_cast<uint32_t>(material ^ (material >> 32));
}

}  // namespace

namespace ArtCade::GameplaySessionSeed {

uint32_t make() noexcept {
    static const uint32_t processSeed = makeProcessSeedMaterial();
    static std::atomic<uint32_t> sequence{0};
    const uint32_t ordinal = sequence.fetch_add(1, std::memory_order_relaxed);
    return permuteSeed(processSeed + ordinal);
}

}  // namespace ArtCade::GameplaySessionSeed
```

- `processSeed` differentiates separate process runs (computed once, best
  effort from the clock plus `random_device`).
- `ordinal` differentiates sessions within the same process and does not
  collide with itself before a `uint32_t` wraps.
- `processSeed + ordinal` is only ever fed through `permuteSeed`, a
  bijection on `uint32_t` (MurmurHash3's finalizer) — a bijection cannot
  introduce a new collision, so any two distinct `ordinal` values in the
  same process produce distinct outputs.

The precise, provable guarantee: **consecutive session seeds in the same
process are distinct until the 32-bit session counter wraps.** Four billion
Plays inside one process execution is not an operationally relevant case.
The result of `make()` can legitimately be `0` — that is fine, because it is
the *final per-scope seed* that must never be zero, which
`LogicRuntime::install()` already guarantees independently (see below).

**Why not `std::random_device` alone:** its quality and availability are
platform-dependent, construction can throw, and it gives no uniqueness
guarantee between two calls made close together — which is exactly what the
counter+permutation construction above fixes deterministically instead of
probabilistically. `random_device::entropy()` is deliberately not consulted
to decide whether the source is "real" — it is not a reliable operational
signal.

`gameplay_session.cpp:413` calls `GameplaySessionSeed::make()` once, at the
point it constructs `LogicRuntime`.

### Per-scope seed derivation

Formula is unchanged in shape, with the session seed folded in and the
zero-check moved after the full combination (xorshift32 has zero as an
absorbing state — a zero seed would produce zero forever):

```cpp
uint32_t seed = 0xA5A5A5A5u
    ^ static_cast<uint32_t>(owner) * 0x9E3779B9u
    ^ static_cast<uint32_t>(token) * 0x85EBCA6Bu
    ^ sessionSeed_;
if (seed == 0) seed = 1u;
```

`Impl` stores the constructor argument as `sessionSeed_` (not
`sessionSalt_` — it is the authoritative session seed that per-scope seeds
are derived from, not a supplementary salt).

### Determinism — corrected claim

The accurate statement:

> The same `sessionSeed`, combined with the same `EntityId`s, the same scope
> installation order, and the same order of `random()` calls, produces the
> same sequence.

The seed alone does not guarantee a full replay if any of the following
also change: entity materialization order, `EntityId` assignment, scope
installation order, event dispatch flow, or the number/order of draws.
AC-LUA-001 is unaffected either way: within a session, generator state only
advances through the existing deterministic call order — this ADR changes
the starting point, not that ordering guarantee.

## Rejected alternatives

- **`math.random()` directly.** Lua's `math.random` is process-global
  mutable state shared by the *entire* Lua VM — not just other Logic Board
  scopes, but any other script sharing that state (e.g. the particle system
  Lua in `logic-components-test.cpp` already calls `math.random()`). A
  board's random draw would then depend on unrelated call order elsewhere in
  the program, which is unpredictable, untestable, and violates AC-LUA-001's
  spirit of deterministic, isolated per-board dispatch. ADR-0028 already
  rejected this explicitly; nothing here reopens that.
- **Reseed from the OS clock on every `random()` call.** Would break
  determinism *within* a session and make the private per-scope generator
  redundant.
- **Seed decision inside `LogicRuntime`, via optional constructor
  parameter.** Puts a session-startup policy decision inside the
  RNG-algorithm module, and an optional/defaulted parameter lets any caller
  (including a future test or headless tool) become non-deterministic by
  omission.
- **XOR-combine clock/counter/`random_device` and truncate to 32 bits, with
  no bijective mixing step.** Does not actually guarantee two consecutive
  calls differ (see Session seed generation above) — replaced by the
  counter-then-bijective-permutation construction.
- **Generator kept anonymous inside `gameplay_session.cpp`.** Untestable
  from outside the translation unit, which directly conflicts with the
  requirement to test that consecutive calls differ. Replaced by the
  `GameplaySessionSeed` helper.
- **New `IGameplayRuntimeHost` method for the seed.** That interface has
  multiple implementations — `RuntimeLogicHostAdapter` in production plus
  fake hosts in `logic-board-test.cpp` and
  `gameplay-tick-order-characterization-test.cpp` — and every one would need
  a new method for a concern `GameplaySession`, not the host abstraction,
  should own. A constructor parameter needs no interface change and touches
  exactly one production call site.

## Test plan

The most important gap this ADR surfaces is that nothing exercises
`logic.random.range` through a real, installed `LogicRuntime` scope and
checks a numeric result — everything today stops at syntax/validation/JSON.

### `GameplaySessionSeed::make()` test

```cpp
const uint32_t first = GameplaySessionSeed::make();
const uint32_t second = GameplaySessionSeed::make();
CHECK(first != second);
```

This is a guaranteed property of the counter + bijective permutation (see
Session seed generation), not a probabilistic assertion about the clock or
`random_device`.

### E2E `LogicRuntime` random test

Build a board:

```text
On Start
→ Set Position
   X = random(0, scene.width)
   Y = 0
```

Compile the board, load the program into `LogicRuntime`, install the scope,
run `dispatchStart`, and capture the actual `Vec2` the fake host's
`setPosition()` receives — record the real floating-point value, not a
stringified or integer-truncated form. Required cases:

- same `sessionSeed` + same `owner` → same first value
- different `sessionSeed` + same `owner` → different first value (use two
  known constants and assert inequality deterministically — do not assert
  that two `std::random_device` draws differ, which would be a probabilistic
  test)
- same session + different scopes → different sequences. Compare a short
  sequence of several values per scope, not just the first sample, e.g.
  `scope A: [a1, a2, a3]` vs `scope B: [b1, b2, b3]` differ, obtained by
  dispatching the rule repeatedly (or via a rule/trigger that draws more
  than once) for each scope
- same scope, two draws → generator advances between calls
- every result is finite and within the requested range

## Open questions — resolved

1. **Is `std::random_device` acceptable as the default entropy source?**
   Yes, as one best-effort ingredient inside `GameplaySessionSeed::make()`,
   not the sole authority, and not inside `LogicRuntime`.
2. **Should exported/shipped games get the same session entropy as editor
   Play?** Yes. Editor Play and an exported build should share the same
   semantics: a normal new session gets a new seed; a test/replay run gets
   an explicit, fixed seed. Because both go through the same
   `GameplaySession` composition root, no separate export policy is needed
   for this slice. A future QA/CLI flag could pass a fixed seed into that
   same composition root. Exports should not be deterministically fixed by
   default — that would surprise authors and reproduce the exact defect
   reported here, just in shipped games instead of the editor.
3. **Should an author be able to pin a board's seed on purpose (e.g. a
   seeded daily-challenge feature)?** Confirmed out of scope. The mandatory
   explicit-seed constructor does not preclude this — a future feature
   (`Runtime launch seed`, `Project gameplay seed`, `Daily challenge seed`,
   `Replay seed`) would be a separate, persisted product decision. Nothing
   about it is added now to `ProjectDocument`, the Logic Board schema,
   `NumberExpression`, the Inspector, or the project format.

## Consequences

- `random(...)` expressions vary from Play to Play, matching what an author
  typing "Random" expects, while still varying per-entity within one session
  as it does today.
- `LogicRuntime`'s constructor signature changes (new mandatory parameter);
  the one production call site (`gameplay_session.cpp:413`) and any test
  that constructs a real `LogicRuntime` directly must be updated to pass a
  seed. Fake `ILogicRuntimeHost` implementations are unaffected.
- One new small translation unit (`gameplay_session_seed.h/.cpp`) in
  `vendor/artcade-runtime/src/app/src`.
- No schema, JSON, catalog, Logic API, or project-format version change.

## Definition of Done

- `LogicRuntime` does not read the clock or OS entropy directly.
- Every constructor call passes an explicit seed.
- `GameplaySessionSeed::make()` generates exactly one seed per session, and
  is unit-tested independently of `GameplaySession`.
- The session seed enters every scope's seed derivation.
- The final per-scope seed can never be zero.
- Same seed + same call flow ⇒ same sequence (see corrected determinism
  claim above — flow includes entity/scope order, not the seed alone).
- Consecutive Plays in the same process use different seeds (provable up to
  the 32-bit counter wrap, not merely probable).
- Editor Play and exported runtime share the same seeding policy.
- A numeric E2E test exists exercising compiler → Lua → scope → host,
  capturing the real `Vec2` value and comparing short sequences across
  scopes, not just a single sample.
- No `math.random` introduced.
- No Logic API, Board schema, or project format bump.
- The WASM/HTML5 export target still builds. `std::random_device` +
  `try`/`catch` is appropriate on desktop but must be checked against the
  project's actual Emscripten build flags — if that target builds with
  exceptions disabled, the `random_device` contribution inside
  `makeProcessSeedMaterial()` must be guarded by conditional compilation for
  that platform; the clock plus process seed plus counter remain sufficient
  there on their own.
- `scripts\build.bat --test` stays green.

## Implementation status

Implemented as designed:

- `GameplaySessionSeed::make()` added
  (`vendor/artcade-runtime/src/app/src/gameplay_session_seed.h/.cpp`),
  wired into the `artcade-gameplay-session` CMake library.
- `LogicRuntime`'s constructor now takes a mandatory `uint32_t sessionSeed`
  (`logic-runtime.h`/`.cpp`); `install()` folds it into the per-scope seed,
  zero-check unchanged in spirit (moved after the full combination).
- `gameplay_session.cpp:413` (production) calls
  `GameplaySessionSeed::make()` once per session.
- All ~58 direct `LogicRuntime(...)` construction sites in
  `logic-board-test.cpp` and `gameplay-tick-order-characterization-test.cpp`
  updated to pass a fixed `kTestSessionSeed` constant.
- New tests: `gameplay-session-seed-test.cpp` (consecutive-call and
  1000-call uniqueness, both provable, not probabilistic — new
  `gameplay_session_seed_test` CMake target, linking only the seed source,
  not the full Raylib-dependent `artcade-gameplay-session` library) and
  `testRandomExpressionSessionSeed` in `logic-board-test.cpp` (the missing
  E2E case: compile → Lua → scope → host, same-seed/same-owner equality,
  different-seed inequality, per-scope sequences captured via a new
  `Host::positionHistory` field carrying the real `Vec2`, generator
  advancement, and range/finiteness).
- Full suite green: `scripts\build_runtime_tests.bat` — 57/57 tests pass
  (`logic_board_test` 1228 assertions, `gameplay_session_seed_test` 1001
  assertions, both 0 failed).
- WASM DoD item resolved, not just flagged: built `artcade-gameplay-session`
  under `emcmake`/`emmake` (Ninja, Release, `PLATFORM=Web`) and
  `gameplay_session_seed.cpp` compiled and linked cleanly. This also
  confirms the open question from the design phase — nothing in this build
  passes `-fno-exceptions` anywhere in the dependency chain (consistent with
  `app/CMakeLists.txt`'s own comment, "C++ exceptions required by Sol2," and
  with `logic-runtime.cpp` already relying on `sol::error` throw/catch
  through the same toolchain) — so the `try`/`catch` around
  `std::random_device` needs no conditional compilation guard on this
  target. The full `game` WASM executable link (and the `dist/wasm` copy
  step) was not run, to avoid overwriting the WASM preview artifacts of the
  editor session that was open during this work; the library-level build is
  sufficient to verify the new translation unit itself.
