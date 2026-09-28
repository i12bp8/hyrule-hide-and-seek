#pragma once

// On-screen overlay: timer, role, banners, the hunters' blindfold, name tags and the scoreboard.

namespace hs::hud {

// Adds the overlay to this frame's 2D draw list. Call from the draw phase (Link's draw hook).
void queue();

}  // namespace hs::hud
