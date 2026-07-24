#pragma once

namespace sol { class state; }

namespace ArtCade {
struct EngineContext;

namespace Modules {

/** Bind read-only Component properties used by Logic Board Value Sources. */
void bindComponentValueAPI(sol::state& lua, const EngineContext& ctx);

} // namespace Modules
} // namespace ArtCade
