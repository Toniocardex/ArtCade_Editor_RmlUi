# ADR-0039 — Splash Screen as an Exclusive Startup Phase

**Status:** Implemented — see "Implementation status" below
**Date:** 2026-07-28
**Scope:** `vendor/artcade-runtime/src/app` (`app_loop.cpp`, `app_project_*.cpp`,
`app.h`), `GameplaySession` (`gameplay_session.h`/`.cpp`),
`vendor/artcade-runtime/src/modules/game-state` (`splash-state.h`/`.cpp`).
No change to the project format, the Logic API, or `GameStateManager`.
**Related:** Supersedes the temporary splash gating previously implemented in
`app_loop.cpp` (a `splashActive` local recomputed every frame, gating
`simulating` and timing the splash off `simulatedDt`). That fix was
behaviorally correct — it did stop gameplay from ticking under the splash —
but relied on a boolean recomputed per frame instead of an explicit phase.
This ADR replaces it with the state machine below. This revision
incorporates six binding corrections and several minor refinements from
design review before implementation started; no code changes from the
superseded fix have shipped beyond that temporary gate.

## Decision

The splash stops being a passive overlay drawn on top of an already-running
scene. `Application` gains a small, startup-only state machine:

```text
Inactive → Splash → Activating → Complete
```

`Inactive` is also the steady state for every path that never runs a
standalone splash — Editor Play and WASM edit-mode preview never enter
`Splash` or `Activating` at all; they stay `Inactive` for the life of the
process (see §3 and §20).

The scene and its runtime scopes may be *prepared* before the splash, but
until the startup phase reaches `Complete`, none of the following run:

- no `On Start`;
- no gameplay input;
- no fixed step;
- no physics;
- no Logic/Script update;
- no scene rendering.

This ADR does **not** introduce:

- a second OS process;
- a second executable;
- a new generic manager;
- changes to `GameStateManager`;
- asynchronous loading in this slice;
- an `onComplete` callback that activates gameplay from inside `SplashState`.

## 1. Current problem

Today `SplashState` is explicitly documented as an overlay rendered by
`Application`, not a phase that controls the lifecycle. While it is showing,
the main loop still:

- forwards input to `GameplaySession`;
- runs `tickFixedStep()`;
- updates physics, Logic, Script, animation and transitions;
- updates camera shake;
- advances the splash using simulated time.

The problem also starts earlier than the loop: during `loadProject()`, Logic
and Script scopes are installed and their `On Start` events are dispatched
immediately. The splash is only created afterward. The current APIs confirm
that installation and activation are coupled:

```text
installLogicScopesForActiveScene()
    → install scopes
    → logicRuntime_->dispatchStart()

installScriptScopesForActiveScene()
    → install scopes
    → scriptRuntime_->dispatchStart()
```

The fix therefore has to act on two distinct boundaries:

```text
1. block simulation while the splash is showing
2. defer Logic/Script On Start until the splash finishes
```

## 2. Target flow

```text
Application::loadProject
│
├─ parse and validate ProjectDoc
├─ compile Logic Board
├─ validate and load Script Assets
├─ materialize the World
├─ configure variables and runtime settings
├─ prepare Logic scopes
├─ prepare Script scopes
└─ startupPhase_ = Splash (Free) or Activating (Pro / no splash)
       │
       ├─ presentation-only timer
       ├─ splash-exclusive render
       ├─ no gameplay input
       ├─ no fixed step
       └─ accumulator always zeroed
              │
              ▼
        startupPhase_ = Activating
       │
       ├─ reset input edge state
       ├─ reset accumulator
       ├─ Logic On Start
       ├─ Script On Start
       ├─ presentation-only refresh (no fixed step — see §14)
       └─ startupPhase_ = Complete
              │
              ▼
        startupPhase_ = Complete
       │
       ├─ dispatchInput
       ├─ tickFixedStep
       ├─ physics / Logic / Script
       ├─ camera shake
       └─ scene render
```

## 3. `GameplayStartupPhase`

The first draft of this ADR proposed a six-value `ApplicationPhase` spanning
the whole application lifecycle (`Preparing`, `Splash`, `ActivatingGameplay`,
`Running`, `Failed`, `ShuttingDown`). Review rejected that scope: `Application`
already owns `running_` as the authority for shutting the loop down, so a
`ShuttingDown` value would be a second representation of the same fact;
`Preparing` never corresponds to a rendered frame because `loadProject()` is
synchronous and returns before `mainLoop()` starts; and `Failed` would
partially duplicate the existing error-return convention (see §18).

The state machine is scoped to exactly the problem this ADR solves — startup
— and nothing else:

```cpp
enum class GameplayStartupPhase {
    Inactive,
    Splash,
    Activating,
    Complete,
};
```

Semantics:

- `Inactive` — no standalone startup sequence in progress. This is also the
  permanent state for Editor Play and WASM edit-mode preview, which never
  run a splash (§20).
- `Splash` — standalone startup is blocked on the splash.
- `Activating` — `On Start` dispatch is running; simulation is still blocked.
- `Complete` — the normal loop is allowed to simulate.

Default:

```cpp
GameplayStartupPhase startupPhase_ =
    GameplayStartupPhase::Inactive;
```

Only the standalone project-load path moves off `Inactive`:

```text
Free tier      → Splash
No splash tier → Activating
```

`Activating` always resolves to `Complete` within the same frame it starts
(§13) — it is not a multi-frame waiting state, only a marker that gates
simulation while `On Start` runs.

No parallel boolean flags are needed:

```cpp
bool splashActive_;
bool gameplayPending_;
bool gameplayStarted_;
```

The phase is the single authority for startup sequencing. The only phase-
associated data is:

```cpp
std::unique_ptr<Modules::SplashState> splash_;
```

This state is not persisted and does not belong to `ProjectDocument`.

### Simulation gate

`canSimulate` combines the startup gate with the existing WASM edit-mode
gate (`EditorAPI::s_mode`), rather than replacing it:

```cpp
const bool startupAllowsSimulation =
    startupPhase_ == GameplayStartupPhase::Inactive ||
    startupPhase_ == GameplayStartupPhase::Complete;

#ifdef ARTCADE_WASM
const bool hostAllowsSimulation = EditorAPI::s_mode == 1;
#else
const bool hostAllowsSimulation = true;
#endif

const bool canSimulate =
    startupAllowsSimulation && hostAllowsSimulation;
```

Because Editor Play and WASM preview stay `Inactive` for their whole
lifetime, `startupAllowsSimulation` is always `true` for them and
`hostAllowsSimulation` continues to be the only gate that applies —
unchanged behavior, no accidental splash and no accidental simulation block
in the editor.

## 4. Freezing the phase for the frame

The first draft had a timing gap: `tickSplash()` could flip
`startupPhase_` to `Activating` mid-frame, and the *same* frame's render
call — running later in `tickFrameEnd()` — would then read the new phase.
That produces exactly the sequencing the ADR is meant to prevent:

```text
frame the splash finishes
→ phase becomes Activating
→ scene gets rendered
→ On Start has not run yet
```

The phase used for both ticking and rendering must be captured once, at the
top of the frame, before anything can mutate it:

```cpp
void Application::loopIteration()
{
    profiler_.beginFrame();
    if (!canContinueRunning()) return;

    const GameplayStartupPhase framePhase = startupPhase_;

    const float frameDt =
        sanitizeFrameDt(mod_->renderer->deltaTime());

    mod_->input->poll();
    processHostControls();

    switch (framePhase) {
    case GameplayStartupPhase::Splash:
        tickSplash(frameDt);
        break;
    case GameplayStartupPhase::Activating:
        tickGameplayActivation();
        break;
    case GameplayStartupPhase::Inactive:
    case GameplayStartupPhase::Complete:
        tickGameplay(frameDt);
        break;
    }

    tickFrameEnd(framePhase);
}
```

`tickFrameEnd()` — which today calls `renderActiveScene()` unconditionally —
takes the frozen phase and renders accordingly instead of re-reading
`startupPhase_`:

```cpp
void Application::tickFrameEnd(GameplayStartupPhase framePhase)
{
    if (framePhase == GameplayStartupPhase::Splash) {
        renderSplashFrame();
    } else {
        renderActiveScene();
    }
    // flush console lines, reset input frame state, profiler bookkeeping...
}
```

Resulting sequence across three frames:

```text
Frame N   — framePhase = Splash
          splash reaches completion
          startupPhase_ = Activating
          render: still Splash (frozen framePhase)

Frame N+1 — framePhase = Activating
          reset input edge state
          Logic On Start
          Script On Start
          startupPhase_ = Complete
          render: scene, after On Start (frozen framePhase != Splash)
          no fixed step

Frame N+2 — framePhase = Complete
          first gameplay input
          first fixed step
```

This is exactly the contract the ADR promises, made explicit at the frame
boundary instead of implied.

## 5. `SplashState` stays passive

`SplashState` must not:

- know about `Application`;
- know about `GameplaySession`;
- hold capturing callbacks;
- start the scene directly;
- mutate the application's startup phase.

Recommended API:

```cpp
class SplashState {
public:
    explicit SplashState(const std::string& tier = "free");

    // Advance once per rendered frame using unscaled presentation time.
    // Independent from gameplay simulation. Returns true only on the
    // update that reaches completion.
    bool update(float presentationDt);

    void render(int screenWidth, int screenHeight) const;
};
```

`update()`'s return value is the completion edge:

```text
false → splash still active
true  → splash reached completion during this update
```

It is not a callback and does not invert ownership. The transition stays
visible at the composition root:

```cpp
if (splash_->update(presentationDt)) {
    startupPhase_ = GameplayStartupPhase::Activating;
}
```

`isDone()` is dropped from the public API unless a concrete consumer needs
it (a test asserting mid-splash state, for instance). The edge-triggered
return from `update()` is sufficient for the loop; keeping a second query
method that nothing calls would just be an unused surface to maintain.

## 6. Splash timing separated from simulation, and clamp ownership

The splash advances on real frame time, not `simulatedDt`. Two different
concerns are involved and each is owned by one place:

- `Application` sanitizes the *general* frame time (finite, non-negative)
  before it is used for anything, splash included.
- `SplashState` owns its own maximum presentation step, so the "how long is
  too long a single splash update" policy lives with the component it
  describes, not as a constant duplicated in the loop.

```cpp
// Application: general sanitization only.
float Application::sanitizeFrameDt(float rawFrameDt) const {
    return std::isfinite(rawFrameDt) && rawFrameDt > 0.f ? rawFrameDt : 0.f;
}
```

```cpp
// SplashState: owns its own clamp.
bool SplashState::update(float dt) {
    if (!std::isfinite(dt) || dt <= 0.f) return false;
    dt = std::min(dt, kMaxPresentationStep);  // e.g. 0.25f
    timer_ += dt;
    return /* just crossed the completion threshold */;
}
```

The clamp prevents a suspended process or a paused debugger from completing
the splash instantly on resume — but it is `SplashState`'s own invariant, not
a value the loop needs to know or repeat.

The splash must be independent of:

- the fixed timestep;
- `timeScale`;
- gameplay pause;
- how many fixed steps ran;
- Editor/WASM mode.

Update the existing comment on `SplashState::update()`:

```cpp
// Old
// Call once per fixed step.

// New
// Advance once per rendered frame using unscaled presentation
// time. Independent from gameplay simulation.
```

## 7. `prepare` / `start` split in `GameplaySession`

### Internal state

```cpp
enum class GameplayActivationState {
    Empty,
    Prepared,
    Started,
};

GameplayActivationState activationState_ =
    GameplayActivationState::Empty;
```

### New API

```cpp
bool prepareActiveSceneGameplay();
bool startPreparedActiveSceneGameplay();
```

### `prepareActiveSceneGameplay()`

Responsibilities:

- discard any previous scopes;
- install the active scene's Logic scopes;
- create/install Script scopes;
- reset the collision tracking that needs it;
- prepare every data structure required;
- run no gameplay callback;
- move from `Empty` to `Prepared`.

Must not call:

```cpp
logicRuntime_->dispatchStart();
scriptRuntime_->dispatchStart();
```

### `startPreparedActiveSceneGameplay()`

Responsibilities:

- require `Prepared`;
- dispatch Logic `On Start`;
- dispatch Script `On Start`;
- move to `Started`;
- guarantee exactly one activation.

A second call must not re-run `On Start`. An explicit, diagnosed rejection is
required, not a silent no-op.

## 8. Transition table and reset boundaries for `GameplayActivationState`

The first draft only rejected `prepare()` while `Started`, which left a gap:
calling `prepare()` again while already `Prepared` would silently reinstall
scopes. The contract is now a complete table:

| Current state | Operation | Result |
|---|---|---|
| `Empty` | `prepare` | → `Prepared` |
| `Prepared` | `prepare` | reject |
| `Started` | `prepare` | reject |
| `Prepared` | `start` | → `Started` |
| `Empty` | `start` | reject |
| `Started` | `start` | reject |
| any | reset for a new World/scene | → `Empty` |

```cpp
bool GameplaySession::prepareActiveSceneGameplay() {
    if (activationState_ != GameplayActivationState::Empty) {
        reportActivationContractViolation(
            "prepare requires Empty state");
        return false;
    }
    // prepare...
}

bool GameplaySession::startPreparedActiveSceneGameplay() {
    if (activationState_ != GameplayActivationState::Prepared) {
        reportActivationContractViolation(
            "start requires Prepared state");
        return false;
    }
    // start...
}
```

`reportActivationContractViolation()` logs with enough context (current
state, requested operation, caller) to diagnose a misuse without a debugger —
this is a programming-contract violation, not a runtime/user-content error
(see §18 for that distinction).

### Reset call sites

The prior draft said only "reset on load/reset/shutdown," which is not
precise enough to implement against. The binding list of call sites that
must reset `activationState_` to `Empty`:

- loading a new `World`;
- project replacement;
- Editor entering or exiting Play mode;
- activating a new scene (scene transition, §17);
- restarting the current scene;
- application shutdown;
- a failed `prepareActiveSceneGameplay()` call (see §9 — reset happens as
  part of the failure path itself, not as a separate step the caller must
  remember).

## 9. Partial-preparation failure cleanup

`prepareActiveSceneGameplay()` must not leave partially-installed scopes
behind when it fails partway through:

```cpp
bool GameplaySession::prepareActiveSceneGameplay() {
    if (activationState_ != GameplayActivationState::Empty)
        return false;

    if (!prepareLogicScopesForActiveScene()) {
        clearPreparedGameplayScopes();
        return false;
    }

    if (!prepareScriptScopesForActiveScene()) {
        clearPreparedGameplayScopes();
        return false;
    }

    activationState_ = GameplayActivationState::Prepared;
    return true;
}
```

Blocking entry into `Complete` is not sufficient on its own: if Script scope
preparation fails after Logic scopes already installed, those Logic scopes
must not linger in memory bound to a scene that never activates.
`clearPreparedGameplayScopes()` tears down whatever was installed and leaves
`activationState_` at `Empty`, satisfying the reset-boundary list in §8.

## 10. `loadProject()` changes

```cpp
mod_->gameplaySession->loadWorldProject(doc);

if (!mod_->gameplaySession->prepareActiveSceneGameplay()) {
    return false;
}

applyRuntimeSettings(
    runtimeSettingsFromProjectDoc(doc),
    ViewportPolicy::NativePlay);

licenseTier_ = doc.licenseTier;

if (requiresStartupSplash(doc)) {
    splash_ = std::make_unique<Modules::SplashState>(licenseTier_);
    startupPhase_ = GameplayStartupPhase::Splash;
} else {
    splash_.reset();
    startupPhase_ = GameplayStartupPhase::Activating;
}
```

Whether to show the splash is host/product policy, not a `GameplaySession`
concern.

### One path for Free and Pro

```text
Free
→ Inactive → Splash → Activating → Complete

Pro (no splash)
→ Inactive → Activating → Complete
```

Tier changes only whether the `Splash` phase is entered, not how gameplay is
prepared or activated.

## 11. Main loop, phase by phase

See §4 for the frame-phase-freezing structure. No separate class per phase
is needed — an enum plus private helper methods on `Application` is enough.

## 12. Behavior during `Splash`

```cpp
void Application::tickSplash(float frameDt) {
    accumulator_ = 0.f;

    if (!splash_) {
        startupPhase_ = GameplayStartupPhase::Activating;
        return;
    }

    if (splash_->update(frameDt)) {
        startupPhase_ = GameplayStartupPhase::Activating;
    }
}
```

Allowed during this phase:

- host input polling (window close, fullscreen toggle);
- a future "skip splash" action;
- splash rendering.

Forbidden:

```cpp
gameplaySession->dispatchInput(...);
gameplaySession->tickFixedStep(...);
gameplaySession->updateCameraShake(...);
```

The accumulator is zeroed every frame so no catch-up backlog builds up to be
replayed once the splash ends.

## 13. Gameplay activation

```cpp
void Application::tickGameplayActivation() {
    accumulator_ = 0.f;

    // Discard edge input (pressed/released) accumulated while the splash
    // was visible — see §19 for what this does and does not guarantee.
    mod_->input->resetFrameState();

    if (!mod_->gameplaySession->startPreparedActiveSceneGameplay()) {
        // Structural failure — see §18. Log and fall back; this is not the
        // same category as an error inside an author's On Start callback.
        std::cerr << "[App] Gameplay activation failed to start from a "
                     "non-Prepared state; aborting standalone startup\n";
        running_ = false;
        return;
    }

    mod_->gameplaySession->drainLogicDiagnostics();
    mod_->gameplaySession->drainScriptDiagnostics();

    splash_.reset();
    startupPhase_ = GameplayStartupPhase::Complete;
}
```

### No fixed step in the activation frame

The `Activating` frame runs only:

```text
clear input edge state
→ Logic On Start
→ Script On Start
→ drain and print startup diagnostics (see §18)
→ presentation-only refresh (§14)
```

The first gameplay input and the first fixed step arrive the following
frame. This prevents:

- the input used to skip the splash from leaking into gameplay;
- physics moving an entity immediately after `On Start`, before a frame has
  rendered the post-`On Start` state;
- the transition frame mixing time that partly belonged to the splash;
- startup ordering becoming frame-rate dependent.

## 14. Presentation without a fixed step

The `Activating` frame's render must show the state `On Start` produced,
without having run any simulation. This needs verifying against the real
pipeline, not just asserted: today the draw queue is cleared before input
and fixed steps run, and simulation is what normally populates the content
that gets rendered. If `renderActiveScene()` depends on data that is only
ever produced inside `tickFixedStep()`, the activation frame could render:

- empty;
- stale (previous frame's data);
- incomplete;
- missing initial sprites/animation state.

**Contract:** the `Activating` frame must be able to produce a valid
presentation of the state resulting from `On Start` without running
simulation. If rendering depends on data normally produced by the fixed
step, an explicit presentation-only refresh must be introduced —
**not** a fixed step run with `dt = 0`:

```cpp
// Do not do this:
tickFixedStep(0.f);
```

`tickFixedStep(0.f)` would still run every system it contains — physics
solver, Logic tick, Script update, collision resolution — with a contract
none of them were written to expect ("run once, advance nothing"). A
presentation refresh is a narrower, explicitly-scoped operation (e.g.
resolving transforms/sprites from the World state into whatever the
renderer consumes) that makes no claim about being a simulation step.

**Mandatory test** (also listed in §22):

```text
On Start → Set Position
Activating frame:
→ position updated
→ sprite visible at the new position
→ no physics/time/Logic tick occurred
```

## 15. Behavior during `Complete`

Only this phase runs simulation:

```cpp
void Application::tickGameplay(float frameDt) {
    if (!canSimulate) {   // §3 — startup gate AND host/WASM gate
        accumulator_ = 0.f;
        return;
    }

    accumulator_ += frameDt;
    accumulator_ = std::min(accumulator_, targetDt_ * 4.f);

    mod_->renderer->clearDrawQueue();

    GameplayInputFrame inputFrame = collectGameplayInput();
    mod_->gameplaySession->dispatchInput(inputFrame);

    float simulatedDt = 0.f;
    while (accumulator_ >= targetDt_) {
        mod_->gameplaySession->tickFixedStep(targetDt_);
        accumulator_ -= targetDt_;
        simulatedDt += targetDt_;
    }

    if (simulatedDt > 0.f) {
        mod_->gameplaySession->updateCameraShake(simulatedDt);
    }

    drainGameplayDiagnostics();
}
```

No fallback to raw `frameTime` for camera shake while gameplay is not
active — shake only advances alongside real simulated time.

## 16. Splash-exclusive rendering

The splash is no longer drawn on top of `renderActiveScene()`; §4 already
shows the frame-phase-gated call in `tickFrameEnd()`. `renderSplashFrame()`:

```cpp
void Application::renderSplashFrame() {
    mod_->renderer->beginFrame();
    ClearBackground(BLACK);
    if (splash_) {
        splash_->render(
            mod_->renderer->screenWidth(),
            mod_->renderer->screenHeight());
    }
    mod_->renderer->endFrame();
}
```

The scene may already be materialized in memory, but it is neither updated
nor rendered while `framePhase == Splash`.

## 17. Subsequent scene transitions

The `prepare`/`start` split also applies to ordinary scene activations, but
the splash applies only to the executable's first startup. For later
transitions:

```text
request scene transition
→ materialize new scene
→ prepare new scopes
→ start immediately
→ new scene's On Start
```

These do not return to `Splash`. Loading screens for heavy transitions may
be introduced later; that is out of scope here. Scene transitions are one of
the binding reset-boundary call sites for `GameplayActivationState` (§8):
`prepare` must observe `Empty` before installing the new scene's scopes.

## 18. Error handling: structural failures vs. authored-content errors

The first draft said any failure during scope preparation, Logic/Script
activation, or runtime preflight moves to a `Failed` phase. That does not
match the runtime's actual error-reporting contract and conflates two
different kinds of failure.

**Structural failure** — the mechanism itself is broken:

- `startPreparedActiveSceneGameplay()` called from a state other than
  `Prepared`;
- scopes fail to install;
- a required runtime (`logicRuntime_`, `scriptRuntime_`) is missing.

These prevent reaching `Complete`. `startPreparedActiveSceneGameplay()`
returns `false`; `Application::tickGameplayActivation()` treats that as fatal
to the standalone startup sequence (§13) — but this is the **existing**
error-return convention (`loadProject()` already returns `false` and logs on
failure elsewhere in this file), not a new `Failed` phase with its own
rendering and recovery machinery.

**Authored-content error** — something inside a project author's `On Start`
callback goes wrong:

- a Lua exception in a Script's `On Start`;
- a Logic rule's host action failing.

`LogicRuntime::dispatchStart()` returns `void` — rule errors are collected as
runtime diagnostics, not surfaced as an activation result. The loop already
drains and prints Logic/Script diagnostics without terminating the session.
This ADR **preserves that policy**: `startPreparedActiveSceneGameplay()`
returns `false` only for structural violations, never to invent a new
fatality policy for content that happens to error inside `On Start`.

`Application::tickGameplayActivation()` drains diagnostics immediately after
dispatching `On Start` (§13), rather than letting them sit unreported until
the first `Complete`-phase frame — an author-side startup error should be
visible in the console at the moment it happens, not delayed behind the
activation frame boundary.

No `ApplicationPhase::Failed` value exists in this design (§3); a structural
failure during standalone startup is handled the same way a structural
failure during `loadProject()` already is.

## 19. Input contract: edge events vs. held state

`resetFrameState()` clears the *edge* events (pressed/released) accumulated
while the splash was up. It does not — and cannot, by construction — change
whether a key that is still physically down reads as `Held` once gameplay
starts.

**Policy:**

- Edge events (`Pressed`/`Released`) produced during `Splash`/`Activating`
  are discarded.
- `Held` state is read normally starting from the first `Complete` frame.

```text
Space pressed and released during the splash
→ no gameplay event

Space still held when gameplay starts
→ Held is observable normally from frame one
```

This is ordinary input-system semantics, not a new latch, and does not need
special-casing. The earlier phrasing "splash input never reaches gameplay"
was broader than what `resetFrameState()` actually guarantees, and is
corrected here to the precise edge-vs-held statement above.

A future "press any key to skip" feature would need the specific key used
for the skip to be consumed until release, so it does not immediately read
as `Held` in the first `Complete` frame. That is **not** implemented by this
ADR — noted so it is not rediscovered as a gap later.

## 20. Free/Pro policy (v1)

Stated explicitly, since `SplashState` can technically support different
presentation styles and the product decision must not be left implicit:

```text
Policy v1:
- Free tier: show the splash.
- Pro tier: no splash; go straight to Activating.
```

Editor Play and WASM edit-mode preview are unaffected by tier — they never
run the standalone startup sequence at all and stay `Inactive` (§3).

## 21. Files to change

**Runtime application**

```text
vendor/artcade-runtime/src/app/src/app_loop.cpp
```

- `GameplayStartupPhase` switch, frozen per-frame (§4);
- `tickSplash`, `tickGameplayActivation`, `tickGameplay` split out;
- `tickFrameEnd(GameplayStartupPhase)` takes the frozen phase;
- accumulator gating;
- input gating;
- camera-shake gating;
- phase-based rendering.

```text
vendor/artcade-runtime/src/app/src/app_project_*.cpp
```

- `loadProject()` prepares but does not start (§10);
- selects `Splash` vs. `Activating`;
- no early `On Start`.

```text
vendor/artcade-runtime/src/app/include/app.h
```

- `GameplayStartupPhase`;
- `startupPhase_` member;
- private helper declarations.

**GameplaySession**

```text
vendor/artcade-runtime/src/app/src/gameplay_session.h
vendor/artcade-runtime/src/app/src/gameplay_session.cpp
```

- `GameplayActivationState` and its full transition table (§8);
- `prepareActiveSceneGameplay()`, `clearPreparedGameplayScopes()` (§9);
- `startPreparedActiveSceneGameplay()`;
- `reportActivationContractViolation()`;
- scope installation separated from `On Start` dispatch;
- state reset wired into every call site listed in §8.

**Splash**

```text
vendor/artcade-runtime/src/modules/game-state/include/splash-state.h
vendor/artcade-runtime/src/modules/game-state/src/splash-state.cpp
```

- presentation-only timer, clamp owned internally (§6);
- `update()` returns the completion edge; `isDone()` dropped unless a real
  consumer needs it;
- updated comments;
- no callback.

## 22. Test plan

**`SplashState` unit tests**

```text
update with a positive dt
→ timer advances

duration not yet reached
→ update returns false

duration reached
→ update returns true exactly once

subsequent update
→ does not produce a new transition

non-finite or negative dt
→ does not corrupt the timer, does not advance
```

**`GameplaySession` prepare/start**

```text
prepare from Empty
→ Logic scopes installed
→ Script scopes installed
→ Logic On Start not dispatched
→ Script On Start not dispatched
→ state = Prepared

prepare from Prepared or Started
→ rejected, no scope reinstallation

prepare: Script scope preparation fails after Logic scopes installed
→ Logic scopes are torn down (clearPreparedGameplayScopes)
→ state = Empty

start from Prepared
→ Logic On Start exactly once
→ Script On Start exactly once
→ state = Started

start from Empty or Started
→ rejected
→ no On Start dispatched
```

**Application splash gating**

```text
during Splash, for N frames:
→ dispatchInput call count = 0
→ tickFixedStep call count = 0
→ camera shake update call count = 0
→ accumulator stays 0
→ world state unchanged
```

**Frame-phase freezing** (§4)

```text
frame where the splash completes:
→ tick uses Splash (frozen framePhase)
→ render uses Splash (frozen framePhase)
→ phase becomes Activating only after this frame

next frame:
→ tick uses Activating
→ Logic/Script On Start dispatched
→ render uses Activating (i.e. renders the scene, post-On-Start)
→ no fixed step this frame

following frame:
→ tick uses Complete
→ first gameplay input and first fixed step occur
```

**Activation without a fixed step** (§14)

```text
On Start → Set Position
Activating frame:
→ position updated
→ sprite visible at the new position
→ no physics/time/Logic tick occurred
```

**Structural vs. authored-content errors** (§18)

```text
startPreparedActiveSceneGameplay() called from Empty
→ returns false
→ standalone startup aborts (existing error-return convention)

Script On Start throws a Lua error
→ diagnostic drained and printed after On Start dispatch
→ activation still completes (state = Started, phase → Complete)
→ no fatal abort
```

**Input edge vs. held** (§19)

```text
Space pressed and released during Splash/Activating
→ no gameplay event ever observed

Space held continuously from before Splash through the first
Complete frame
→ Held is observable starting the first Complete frame
```

**Rendering**

```text
Splash phase
→ renderActiveScene not called
→ splash render called

Complete phase
→ splash render not called
→ renderActiveScene called
```

**Tier without a splash**

```text
Inactive → Activating → Complete
On Start exactly once
no regression versus today's plain startup
```

**Scene transitions after startup** (§17)

```text
transition after Complete
→ new scene prepared (Empty → Prepared)
→ started immediately (Prepared → Started)
→ no return to Splash
```

**Editor/WASM unaffected** (§20)

```text
Editor Play / WASM edit-mode preview
→ startupPhase_ stays Inactive for the whole session
→ no SplashState ever constructed
→ existing EditorAPI::s_mode gating behavior unchanged
```

## 23. Definition of Done

The slice is closed only when:

- the splash is an exclusive startup phase of `Application`, expressed as
  `GameplayStartupPhase`, not a rendered overlay;
- the phase used for ticking and rendering is frozen once per frame (§4);
- no scene is rendered under the splash;
- no gameplay input reaches `GameplaySession` during `Splash`;
- no fixed step runs during `Splash` or `Activating`;
- the accumulator stays at zero throughout `Splash`;
- Logic `On Start` is not dispatched during `loadProject()`;
- Script `On Start` is not dispatched during `loadProject()`;
- `prepare` and `start` are separate operations with the full transition
  table in §8 enforced, including rejection paths;
- a partially-failed `prepare` leaves no installed scopes behind (§9);
- activation happens exactly once per scene lifetime;
- the `Activating` frame renders the post-`On Start` state without running a
  fixed step (§14);
- structural failures use the existing error-return convention; authored-
  content errors during `On Start` remain non-fatal diagnostics (§18);
- the input contract is edge-discarded / held-preserved, as stated in §19,
  not the broader "no splash input ever reaches gameplay" claim;
- Free and Pro share one pipeline, differing only in whether `Splash` runs
  (§10, §20);
- Editor Play and WASM edit-mode preview never leave `Inactive` and never
  construct a `SplashState`;
- later scene transitions do not re-enter `Splash`;
- no new manager or second lifecycle authority is introduced;
- no change to the project format or the Logic API;
- the runtime test suite and the full build stay green.

## Rejected alternatives

**`onComplete` callback as the transition authority.** Rejected because it:

- hides a lifecycle side effect inside `SplashState::update()`;
- couples presentation to the composition root;
- introduces lifetime coupling through a captured lambda;
- does not by itself solve the early-`On Start` problem;
- does not remove the need for the caller to know the startup phase anyway.

**Splash as a `GameStateManager` state.** Rejected because
`GameStateManager` is already updated inside
`GameplaySession::tickFixedStep()` — it is internal to the very gameplay
simulation the splash must prevent from starting. It would also mix
executable lifecycle with author-facing gameplay states.

**A six-value, whole-lifecycle `ApplicationPhase`.** The original draft's
`Preparing`/`Splash`/`ActivatingGameplay`/`Running`/`Failed`/`ShuttingDown`
enum was rejected on review (§3): it duplicated `running_` (`ShuttingDown`),
included a phase with no corresponding rendered frame (`Preparing`, since
`loadProject()` is synchronous), and partially duplicated the existing
error-return convention (`Failed`, see §18). `GameplayStartupPhase` is scoped
to the startup problem only.

**A fixed step with `dt = 0` to populate the activation frame's render
data.** Rejected (§14) because it would run every system in the fixed-step
pipeline — physics, Logic, Script, collision — under a "do nothing" contract
none of them were written to satisfy. A narrower, explicitly-scoped
presentation-only refresh is required instead.

**Asynchronous loading.** Deferred until there is a real cost that justifies
it. The proposed state machine allows, in the future:

```text
PreparingAsync
AND minimumSplashDuration
→ Activating
```

without changing `GameplaySession`'s contract.

## Final decision to share

The splash becomes an exclusive phase of the application's startup instead
of a passive overlay. `Application` owns a small state machine scoped to
startup only — `Inactive → Splash → Activating → Complete` — frozen once per
frame so ticking and rendering always agree on which phase produced them.
`GameplaySession` separates scope preparation from scope activation, with a
complete transition table and explicit reset boundaries, so Logic and Script
`On Start` cannot fire during `loadProject()` and cannot fire twice. While
`Splash` or `Activating` is current, no gameplay input, fixed step, physics,
Logic/Script update, or scene render occurs; the `Activating` frame renders
the post-`On Start` state through an explicit presentation-only refresh, not
a disguised fixed step. Structural failures use the project's existing
error-return convention; errors inside an author's own `On Start` code
remain non-fatal diagnostics, unchanged from today. Editor Play and WASM
preview never leave `Inactive` and are unaffected.

## Implementation status

Implemented.

- `SplashState` (`splash-state.h`/`.cpp`): edge-triggered `update()`
  (`completionReported_` latch), internal `kMaxPresentationStep` clamp,
  `isDone()` removed - no consumer needed it.
- `GameplayStartupPhase` added to `app.h`/wired through `app_loop.cpp`
  exactly as §4/§11-16 describe: `framePhase` frozen once at the top of
  `loopIteration()`, `tickSplash`/`tickGameplayActivation`/`tickGameplay`
  split out, `tickFrameEnd(framePhase)` branches render, `renderSplashFrame()`
  added as a raw raylib frame bracket (no `PresentationSnapshot` — the real
  `Renderer::beginFrame()` needs one and drives the full scene pipeline,
  which the splash must not touch; `SplashState::render()` already drew with
  raw raylib calls for the same reason). `tickGameplay()` deliberately keeps
  the pre-ADR `if (canSimulate) {...} accumulator/dispatch/fixed-step...}`
  shape with an *unconditional* trailing camera-shake update, rather than the
  ADR's simplified single-`canSimulate`-gate pseudocode, because the existing
  WASM edit-mode behavior (camera shake keeps decaying off raw frame time
  even while `s_mode` blocks simulation) is unrelated to this ADR and must
  not regress; `tickGameplay()` is in any case only reachable for
  `Inactive`/`Complete` via the switch, so no redundant startup-phase check
  was needed inside it.
- `GameplaySession::GameplayActivationState` (`Empty`/`Prepared`/`Started`)
  with the full transition table from §8, `prepareActiveSceneGameplay()`,
  `startPreparedActiveSceneGameplay()`, `clearPreparedGameplayScopes()`
  (partial-failure cleanup, §9), `reportActivationContractViolation()`, and
  `resetGameplayActivationState()` for the reset-boundary call sites that
  aren't already covered by a fresh session (`loadProject`) or by
  `shutdownLogicModules()` (wired to reset to `Empty`, satisfying the
  "application shutdown" boundary).
- `installLogicScopesForActiveScene()`/`installScriptScopesForActiveScene()`
  stripped of their `dispatchStart()` calls (moved into
  `startPreparedActiveSceneGameplay()`); `loadProject()`
  (`app_project_lifecycle.cpp`) now calls `prepareActiveSceneGameplay()`
  only, and decides `Splash` vs. `Activating` at the same point it used to
  unconditionally construct `SplashState` for the free tier.
- Scene transitions (`app_scene_lifecycle.cpp::handleSceneTransition`):
  `resetGameplayActivationState()` then prepare-then-start immediately, per
  §17 — this is the sole call site for scene-scoped activation outside
  `loadProject()`, so the reset is unconditional and self-healing regardless
  of what state a previous scene (or Editor Play's first activation) left
  behind.
- **Scope correction found during implementation, not anticipated by the
  ADR's Scope line:** `src/editor-native/model/play_session.cpp` — the
  native RmlUi editor's own Editor Play implementation
  (`PlaySession::materialize`) — calls the same
  `GameplaySession::install*ForActiveScene()` API directly at two sites (its
  own scene-transition callback, and Play's initial activation). Stripping
  `dispatchStart()` out of those methods without updating this file would
  have silently broken Editor Play: entities would install scopes but never
  receive `On Start`. Both sites were migrated to the same
  reset+prepare+start / prepare+start pattern used in
  `app_scene_lifecycle.cpp` and `app_project_lifecycle.cpp`, preserving
  identical behavior (Editor Play still has no splash/startup phase to defer
  through, §20 — prepare and start simply run back-to-back). This file is
  outside `vendor/artcade-runtime` and was not listed in this ADR's Scope;
  it is the "GameplaySession" API's third call site and needed the same
  migration as the two that were listed.
- Verified: `game.exe` (standalone/export runtime) and
  `artcade-editor-native.exe` both build clean. Vendored runtime suite
  (`scripts\build_runtime_tests.bat`): 57/57 green. Full editor gate
  (`scripts\build.bat --test`): all suites green, including
  `logic-board-editor-test` (874 assertions) and the other Play-adjacent
  suites that exercise `PlaySession`.
- **Not done:** the dedicated new tests §22 describes (SplashState edge-
  return unit tests, the `GameplayActivationState` transition-table tests,
  frame-phase-freezing test, activation-without-a-fixed-step test, edge-vs-
  held input test) were not authored — the existing suites confirm no
  regression but do not yet assert the new contract directly. `§14`'s claim
  that `buildFrameSnapshot()` re-resolves live World state on every render
  call (so the `Activating` frame needs no `tickFixedStep(0.f)` workaround)
  was verified by reading `gameplay_session.h`'s doc comments on
  `buildFrameSnapshot()`, not by an end-to-end screenshot of a project whose
  `On Start` moves an entity — offered as a follow-up if stronger
  confirmation is wanted.
