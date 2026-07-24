#pragma once

#include "../scene_frame_context.h"

namespace ArtCade::AppRenderPasses {

/** Selection boxes and hidden-in-game outlines (editor). */
void execute_gizmo_pass(SceneFrameContext& ctx);

} // namespace ArtCade::AppRenderPasses
