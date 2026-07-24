#include "editor-native/app/shortcuts/shortcut_router.h"

namespace ArtCade::EditorNative {

namespace {

int scopeSpecificity(const ShortcutScope& scope) {
    int score = 0;
    if (scope.requireTilemapContext) score += 100;
    if (scope.focusDomains != FocusDomainMask::Any
        && scope.focusDomains != FocusDomainMask::AnySceneAuthoring)
        score += 40;
    else if (scope.focusDomains == FocusDomainMask::AnySceneAuthoring)
        score += 20;
    if (scope.workspaces != WorkspaceMask::Any) score += 10;
    if (scope.modes != EditorModeMask::Any) score += 5;
    return score;
}

bool scopeMatches(const ShortcutScope& scope, const EditorShortcutContext& ctx) {
    if (ctx.modalOpen || ctx.exclusiveKeyCapture || ctx.popupOpen) return false;
    if (!ctx.windowFocused) return false;
    if (ctx.textEditing && !scope.allowTextEditing) return false;

    if ((workspaceMaskFor(ctx.workspace) & scope.workspaces) == WorkspaceMask::None)
        return false;
    if ((modeMaskFor(ctx.playing) & scope.modes) == EditorModeMask::None)
        return false;
    if ((overlayMaskFor(ctx.overlay) & scope.overlays) == OverlayMask::None)
        return false;

    const FocusDomainMask focus = focusMaskFor(ctx.focus);
    if (scope.focusDomains != FocusDomainMask::Any) {
        if (focus == FocusDomainMask::None) {
            // Unspecified focus: Scene authoring scopes only (not Console, etc.).
            if ((scope.focusDomains & FocusDomainMask::AnySceneAuthoring)
                == FocusDomainMask::None)
                return false;
        } else if ((focus & scope.focusDomains) == FocusDomainMask::None) {
            return false;
        }
    }

    if (scope.requireTilemapContext && !ctx.tilemapEditingAvailable) return false;
    if (scope.requireTilemapContext && ctx.tilemapOperationActive) return false;
    return true;
}

bool gestureArmed(const ShortcutGesture& gesture,
                  const KeyboardFrameSnapshot& keyboard,
                  ShortcutTrigger trigger) {
    if (!keyboard.windowFocused) return false;
    if (keyboard.modifiers != gesture.modifiers) return false;
    if (trigger == ShortcutTrigger::PressOnce) {
        return keyboard.pressed.test(gesture.key);
    }
    return keyboard.pressed.test(gesture.key) || keyboard.repeated.test(gesture.key);
}

} // namespace

ShortcutResolution resolveShortcut(const KeyboardFrameSnapshot& keyboard,
                                   const EditorShortcutContext& context,
                                   const ShortcutCatalogView& catalog) {
    ShortcutResolution best;
    int bestScore = -1;

    if (!catalog.bindings || catalog.count == 0) return best;
    if (context.modalOpen || context.exclusiveKeyCapture || context.popupOpen)
        return best;
    if (!keyboard.windowFocused || !context.windowFocused) return best;

    for (std::size_t i = 0; i < catalog.count; ++i) {
        const ShortcutBinding& b = catalog.bindings[i];
        if (context.textEditing && !b.scope.allowTextEditing) continue;
        if (!scopeMatches(b.scope, context)) continue;
        // F Focus must not fire when tilemap context owns F as Fill.
        if (b.action == EditorActionId::SceneFocusSelection
            && context.tilemapEditingAvailable)
            continue;
        if (!gestureArmed(b.gesture, keyboard, b.trigger)) continue;

        const int score = scopeSpecificity(b.scope);
        if (score > bestScore) {
            bestScore = score;
            best.matched = true;
            best.action = b.action;
            best.gesture = b.gesture;
            best.bindingId = b.id;
            best.requestedConsumption.consumePrimaryUntilRelease = true;
            best.requestedConsumption.primaryKey = b.gesture.key;
        }
    }
    return best;
}

} // namespace ArtCade::EditorNative
