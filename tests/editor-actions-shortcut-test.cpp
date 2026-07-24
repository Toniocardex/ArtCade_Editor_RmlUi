// ADR-0024 Phase 1 — pure ShortcutRouter / ActionStateResolver / EscapeOwner.

#include "editor_core_test_harness.h"

#include "editor-native/app/editor_build_info.h"
#include "editor-native/app/shortcuts/editor_action_catalog.h"
#include "editor-native/app/shortcuts/editor_action_state.h"
#include "editor-native/app/shortcuts/escape_routing.h"
#include "editor-native/app/shortcuts/keyboard_shortcut_projection.h"
#include "editor-native/app/shortcuts/shortcut_conflicts.h"
#include "editor-native/app/shortcuts/shortcut_format.h"
#include "editor-native/app/shortcuts/shortcut_router.h"
#include "editor-native/model/project_io.h"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>

using namespace ArtCade::EditorNative;

namespace {

KeyboardFrameSnapshot press(ShortcutKey key, ShortcutModifier mods = ShortcutModifier::None) {
    KeyboardFrameSnapshot k;
    k.pressed.set(key);
    k.held.set(key);
    k.modifiers = mods;
    k.windowFocused = true;
    return k;
}

KeyboardFrameSnapshot repeat(ShortcutKey key, ShortcutModifier mods = ShortcutModifier::None) {
    KeyboardFrameSnapshot k;
    k.repeated.set(key);
    k.held.set(key);
    k.modifiers = mods;
    k.windowFocused = true;
    return k;
}

EditorShortcutContext sceneCtx() {
    EditorShortcutContext c;
    c.workspace = EditorWorkspaceKind::Scene;
    c.focus = KeyboardFocusDomain::Hierarchy;
    c.windowFocused = true;
    return c;
}

ShortcutCatalogView catalog() {
    return {shortcutBindings(), shortcutBindingCount()};
}

} // namespace

int main() {
    std::string catalogError;
    assert(validateEditorActionCatalog(&catalogError));

    // Formatting
    assert(formatShortcutGesture({ShortcutKey::Z, ShortcutModifier::Primary}) == "Ctrl+Z");
    assert(formatShortcutGesture(
               {ShortcutKey::Z, ShortcutModifier::Primary | ShortcutModifier::Shift})
           == "Ctrl+Shift+Z");
    assert(formatShortcutGesture({ShortcutKey::F5, ShortcutModifier::None}) == "F5");

    // Exact modifiers: Ctrl+D ≠ Ctrl+Shift+D
    {
        const auto dup = resolveShortcut(
            press(ShortcutKey::D, ShortcutModifier::Primary), sceneCtx(), catalog());
        assert(dup.matched && dup.action == EditorActionId::SceneDuplicateInstance);

        const auto create = resolveShortcut(
            press(ShortcutKey::D, ShortcutModifier::Primary | ShortcutModifier::Shift),
            sceneCtx(), catalog());
        assert(create.matched
               && create.action == EditorActionId::SceneCreateInstanceOfSelectedType);

        const auto bare = resolveShortcut(press(ShortcutKey::D), sceneCtx(), catalog());
        assert(!bare.matched);
    }

    // Focus vs Fill: no conflict in catalog; runtime picks Fill when tilemap
    {
        EditorShortcutContext focusOnly = sceneCtx();
        focusOnly.tilemapEditingAvailable = false;
        const auto focus = resolveShortcut(press(ShortcutKey::F), focusOnly, catalog());
        assert(focus.matched && focus.action == EditorActionId::SceneFocusSelection);

        EditorShortcutContext fillCtx = sceneCtx();
        fillCtx.tilemapEditingAvailable = true;
        const auto fill = resolveShortcut(press(ShortcutKey::F), fillCtx, catalog());
        assert(fill.matched && fill.action == EditorActionId::TilemapFillTool);
    }

    // Same-scope conflict detection: fabricate overlapping bindings
    {
        const ShortcutBinding clash[] = {
            {1, EditorActionId::Undo,
             {ShortcutKey::Z, ShortcutModifier::Primary},
             {WorkspaceMask::Any, FocusDomainMask::Any, OverlayMask::NoneOnly,
              EditorModeMask::Edit, false, false},
             ShortcutTrigger::PressOnce},
            {2, EditorActionId::Redo,
             {ShortcutKey::Z, ShortcutModifier::Primary},
             {WorkspaceMask::Any, FocusDomainMask::Any, OverlayMask::NoneOnly,
              EditorModeMask::Edit, false, false},
             ShortcutTrigger::PressOnce},
        };
        ShortcutConflict conflict;
        assert(!validateShortcutCatalogConflicts(clash, 2, &conflict));
        assert(conflict.first == EditorActionId::Undo);
        assert(conflict.second == EditorActionId::Redo);
    }

    // Precedence: modal / text suppress Scene
    {
        EditorShortcutContext modal = sceneCtx();
        modal.modalOpen = true;
        assert(!resolveShortcut(press(ShortcutKey::Delete), modal, catalog()).matched);

        EditorShortcutContext text = sceneCtx();
        text.textEditing = true;
        assert(!resolveShortcut(press(ShortcutKey::Delete), text, catalog()).matched);
        assert(!resolveShortcut(
                    press(ShortcutKey::Z, ShortcutModifier::Primary), text, catalog())
                    .matched);
    }

    // Unfocused window → no action
    {
        auto k = press(ShortcutKey::Delete);
        k.windowFocused = false;
        assert(!resolveShortcut(k, sceneCtx(), catalog()).matched);

        EditorShortcutContext unfocused = sceneCtx();
        unfocused.windowFocused = false;
        assert(!resolveShortcut(press(ShortcutKey::Delete), unfocused, catalog()).matched);
    }

    // PressOnce ignores repeat
    {
        const auto once = resolveShortcut(
            press(ShortcutKey::Z, ShortcutModifier::Primary), sceneCtx(), catalog());
        assert(once.matched && once.action == EditorActionId::Undo);

        const auto rep = resolveShortcut(
            repeat(ShortcutKey::Z, ShortcutModifier::Primary), sceneCtx(), catalog());
        assert(!rep.matched);
    }

    // Matched + consumption request even when action would be disabled
    {
        const auto res = resolveShortcut(
            press(ShortcutKey::Z, ShortcutModifier::Primary), sceneCtx(), catalog());
        assert(res.matched);
        assert(res.requestedConsumption.consumePrimaryUntilRelease);
        assert(res.requestedConsumption.primaryKey == ShortcutKey::Z);

        EditorActionContext actionCtx;
        actionCtx.canUndo = false;
        const auto state = resolveActionState(res.action, actionCtx);
        assert(!state.enabled);
    }

    // Console copy requires Console focus
    {
        EditorShortcutContext hierarchy = sceneCtx();
        assert(!resolveShortcut(
                    press(ShortcutKey::C, ShortcutModifier::Primary), hierarchy, catalog())
                    .matched);

        EditorShortcutContext console = sceneCtx();
        console.focus = KeyboardFocusDomain::Console;
        const auto copy = resolveShortcut(
            press(ShortcutKey::C, ShortcutModifier::Primary), console, catalog());
        assert(copy.matched && copy.action == EditorActionId::ConsoleCopySelection);

        EditorActionContext st;
        st.focus = KeyboardFocusDomain::Console;
        st.consoleSelectionAvailable = false;
        assert(!resolveActionState(EditorActionId::ConsoleCopySelection, st).enabled);
        st.consoleSelectionAvailable = true;
        assert(resolveActionState(EditorActionId::ConsoleCopySelection, st).enabled);
    }

    // Escape owner precedence: modal before capture; opacity before global
    {
        EscapeContext e;
        e.modalOpen = true;
        e.logicKeyCapture = true;
        assert(resolveEscapeOwner(e) == EscapeOwner::Modal);

        e = {};
        e.logicKeyCapture = true;
        assert(resolveEscapeOwner(e) == EscapeOwner::LogicKeyCapture);

        e = {};
        e.backgroundOpacityDraft = true;
        e.tilemapOperationActive = true;
        assert(resolveEscapeOwner(e) == EscapeOwner::BackgroundOpacityDraft);

        e = {};
        assert(resolveEscapeOwner(e) == EscapeOwner::None);
    }

    // PendingEditPolicy on descriptors for migrated file/run actions
    {
        const auto* save = findActionDescriptor(EditorActionId::SaveProject);
        assert(save && save->pendingEditPolicy == PendingEditPolicy::CommitThenExecute);
        const auto* undo = findActionDescriptor(EditorActionId::Undo);
        assert(undo && undo->pendingEditPolicy == PendingEditPolicy::SuppressWhileEditing);
    }

    // Help actions + F1 + stable keys
    {
        const auto* shortcuts = findActionDescriptorByStableKey("help.keyboard_shortcuts");
        assert(shortcuts && shortcuts->id == EditorActionId::ShowKeyboardShortcuts);
        const auto* about = findActionDescriptorByStableKey("help.about");
        assert(about && about->id == EditorActionId::ShowAboutArtCade);
        assert(findActionDescriptorByStableKey("unknown.help.action") == nullptr);

        assert(formatPrimaryShortcut(EditorActionId::ShowKeyboardShortcuts) == "F1");
        const auto f1 = resolveShortcut(press(ShortcutKey::F1), sceneCtx(), catalog());
        assert(f1.matched && f1.action == EditorActionId::ShowKeyboardShortcuts);

        EditorActionContext helpOk;
        assert(resolveActionState(EditorActionId::ShowKeyboardShortcuts, helpOk).enabled);
        assert(resolveActionState(EditorActionId::ShowAboutArtCade, helpOk).enabled);

        EditorActionContext confirmModal;
        confirmModal.modalOpen = true;
        confirmModal.helpDialogOpen = false;
        assert(!resolveActionState(EditorActionId::ShowKeyboardShortcuts, confirmModal)
                    .enabled);
        assert(resolveActionState(EditorActionId::CancelCurrentOperation, confirmModal)
                   .enabled);

        EditorActionContext helpModal;
        helpModal.modalOpen = true;
        helpModal.helpDialogOpen = true;
        assert(resolveActionState(EditorActionId::ShowAboutArtCade, helpModal).enabled);
        assert(resolveActionState(EditorActionId::ShowKeyboardShortcuts, helpModal).enabled);
        assert(!resolveActionState(EditorActionId::SaveProject, helpModal).enabled);
    }

    // Keyboard shortcut projection
    {
        EditorActionContext live;
        live.modalOpen = true;
        live.focus = KeyboardFocusDomain::Modal;
        live.workspace = EditorWorkspaceKind::Scene;
        live.projectAvailable = true;
        live.canUndo = true;
        live.canRedo = true;
        const EditorActionContext eval = makeShortcutHelpEvaluationContext(live);
        assert(!eval.modalOpen);
        assert(eval.focus == KeyboardFocusDomain::SceneViewport);

        KeyboardShortcutProjectionInput allInput;
        allInput.filter = KeyboardShortcutsFilter::All;
        allInput.availabilityContext = eval;
        const auto all = buildKeyboardShortcutProjection(allInput);
        assert(!all.empty());

        bool foundRedo = false;
        for (const auto& item : all) {
            if (item.action == EditorActionId::Redo) {
                foundRedo = true;
                assert(item.formattedGestures.size() == 2);
            }
            // Unbound placeholders must be absent.
            assert(item.action != EditorActionId::LogicDeleteSelection);
            assert(item.action != EditorActionId::ScriptFind);
        }
        assert(foundRedo);

        KeyboardShortcutProjectionInput generalInput = allInput;
        generalInput.filter = KeyboardShortcutsFilter::General;
        const auto general = buildKeyboardShortcutProjection(generalInput);
        assert(!general.empty());
        for (const auto& item : general) {
            const auto* d = findActionDescriptor(item.action);
            assert(d);
            assert(d->category != EditorActionCategory::Scene);
            assert(d->category != EditorActionCategory::Tilemap);
        }

        KeyboardShortcutProjectionInput sceneInput = allInput;
        sceneInput.filter = KeyboardShortcutsFilter::Scene;
        const auto scene = buildKeyboardShortcutProjection(sceneInput);
        assert(!scene.empty());
        bool sawScene = false;
        bool sawTilemap = false;
        for (const auto& item : scene) {
            const auto* d = findActionDescriptor(item.action);
            assert(d);
            assert(d->category == EditorActionCategory::Scene
                   || d->category == EditorActionCategory::Tilemap);
            if (d->category == EditorActionCategory::Scene) sawScene = true;
            if (d->category == EditorActionCategory::Tilemap) sawTilemap = true;
        }
        assert(sawScene && sawTilemap);

        KeyboardShortcutProjectionInput searchInput = allInput;
        searchInput.search = "ctrl+shift+z";
        const auto searched = buildKeyboardShortcutProjection(searchInput);
        assert(searched.size() == 1);
        assert(searched.front().action == EditorActionId::Redo);

        // Availability must not collapse solely because the live modal focus is Modal.
        bool anyAvailable = false;
        for (const auto& item : all) {
            if (item.currentlyAvailable) anyAvailable = true;
        }
        assert(anyAvailable);
    }

    // Build metadata authorities
    {
        assert(currentProjectSchemaVersion() == 10);
        const EditorBuildInfo editor = makeEditorBuildInfo();
        assert(editor.productName == "ArtCade Studio");
        assert(editor.editorVersion == "0.1.0");
        assert(editor.projectSchemaVersion == currentProjectSchemaVersion());
        assert(!editor.editorBuildId.empty());

        const BundledRuntimeInfo missing =
            loadBundledRuntimeInfo(std::filesystem::path{"no-such-templates-root"});
        assert(!missing.available);

        const AboutArtCadeProjection aboutMissing =
            buildAboutArtCadeProjection(std::filesystem::path{"no-such-templates-root"});
        assert(aboutMissing.editor.projectSchemaVersion == 10);
        assert(!aboutMissing.runtime.available);
    }

    std::cout << "editor_actions_shortcut_test: OK\n";
    return 0;
}
