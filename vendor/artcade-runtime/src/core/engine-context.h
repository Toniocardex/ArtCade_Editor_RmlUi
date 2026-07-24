#pragma once

/**
 * EngineContext — lightweight dependency container.
 *
 * Instead of passing N pointers into every constructor, modules receive a
 * const reference to this struct and pull only what they need.
 *
 * Rules:
 *  - All pointers are non-owning (lifetime managed by Application).
 *  - A nullptr means the subsystem has not been initialised yet.
 *  - Modules must NOT store the EngineContext beyond init().
 */

namespace ArtCade {

// Forward declarations (no headers pulled in here)
namespace Modules {
    class Renderer;
    class Physics;
    class Input;
    class Audio;
    class LuaHost;
    class SceneManager;
    class RuntimeEntityGateway;
    class AssetLoader;
    class GameAPI;
    class TimeManager;
    class EventBus;
    class VariableManager;
    class GameStateManager;
    class TextureManager;
    class SpriteAnimator;
    class CameraManager;
    class TweenManager;
    class SaveLoadManager;
    class DialogManager;
}
class World;
class RuntimeProfiler;

struct EngineContext {
    // ---- core systems (existed before) ----
    Modules::Renderer*      renderer      = nullptr;
    Modules::Physics*       physics       = nullptr;
    Modules::Input*         input         = nullptr;
    Modules::Audio*         audio         = nullptr;
    Modules::LuaHost*       luaHost       = nullptr;
    Modules::SceneManager*  sceneManager  = nullptr;
    Modules::RuntimeEntityGateway* entityGateway = nullptr;
    Modules::AssetLoader*   assetLoader   = nullptr;
    Modules::GameAPI*       gameAPI       = nullptr;
    World*                  world         = nullptr;
    RuntimeProfiler*        profiler      = nullptr;

    // ---- new modules ----
    Modules::TimeManager*      timeManager      = nullptr;
    Modules::EventBus*         eventBus         = nullptr;
    Modules::VariableManager*  variableManager  = nullptr;
    Modules::GameStateManager* gameStateManager = nullptr;
    Modules::TextureManager*   textureManager   = nullptr;
    Modules::SpriteAnimator*   spriteAnimator   = nullptr;
    Modules::CameraManager*    cameraManager    = nullptr;
    Modules::TweenManager*     tweenManager     = nullptr;
    Modules::SaveLoadManager*  saveLoadManager  = nullptr;
    Modules::DialogManager*    dialogManager    = nullptr;
};

} // namespace ArtCade
