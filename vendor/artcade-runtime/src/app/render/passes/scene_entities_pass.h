#pragma once

#include "../scene_frame_context.h"

namespace ArtCade::AppRenderPasses {

/** Sorted entity sprites, gauges, and scene fade overlay. */
void execute_scene_entities_pass(SceneFrameContext& ctx);

} // namespace ArtCade::AppRenderPasses
