#include "../include/game-api.h"
#include "../../event-bus/include/event-bus.h"

namespace ArtCade::Modules {

GameAPI::GameAPI(const EngineContext& ctx) : ctx_(ctx) {}

bool GameAPI::init()     { return true; }
void GameAPI::shutdown() {
    if (ctx_.eventBus) {
        for (const uint32_t token : eventBridgeTokens_)
            ctx_.eventBus->unsubscribe(token);
    }
    eventBridgeTokens_.clear();
    luaState_ = nullptr;
}

void GameAPI::registerAll(sol::state& lua) {
    luaState_ = &lua;      // cached for per-frame dispatch (lifecycle events)
    bindDebugAPI  (lua);   // first: debug.log available for others
    bindEventAPI  (lua);   // event bus (no deps)
    bindTimeAPI   (lua);   // timer system (no deps)
    bindEntityAPI (lua);   // entity / object / pool
    bindPhysicsAPI(lua);   // collision / physics
    bindInputAPI  (lua);   // input
    bindIntentAPI (lua);   // movement/platformer intents
    bindComponentAPI(lua); // component runtime APIs (Tranche 2)
    bindAudioAPI  (lua);   // audio
    bindTextAPI   (lua);   // text.draw
    bindStateAPI  (lua);   // state
    bindSaveAPI   (lua);   // save
    bindCameraAPI (lua);   // camera
    bindAnimationAPI (lua);
    bindLifecycleAPI (lua);
    bindGridAPI      (lua);
    bindShaderAPI    (lua);
    bindDialogAPI    (lua);
}

} // namespace ArtCade::Modules
