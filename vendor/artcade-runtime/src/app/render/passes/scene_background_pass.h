#pragma once

#include "../scene_frame_context.h"

namespace ArtCade::AppRenderPasses {

/** Scene backdrop, world fill, parallax layers, and tilemaps. */
void execute_scene_background_pass(SceneFrameContext& ctx);

} // namespace ArtCade::AppRenderPasses
