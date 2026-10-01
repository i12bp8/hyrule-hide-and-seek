#pragma once
#include <algorithm>

namespace hs::hud {
struct Rect {
    float x, y, w, h;
    bool overlaps(const Rect& other, float gap = 4) const {
        return x < other.x + other.w + gap && x + w + gap > other.x &&
               y < other.y + other.h + gap && y + h + gap > other.y;
    }
};

// The centre lane leaves space for native mobile controls at either side of the screen.
inline Rect centre_card(float x, float y, float width, float top, float height, float maximum = 300) {
    const float w = std::min(maximum, std::max(0.0f, width * 0.56f));
    return {x + (width - w) * 0.5f, y + top, w, height};
}

struct ClueIndicator {
    float x, y, radius;
};
inline ClueIndicator clue_indicator(float x, float y, float width, float height,
    float hunterX, float hunterY) {
    // Keep the entire orbit clear of the status card and bottom role/control hints.
    const float radius = std::min(60.0f, std::max(0.0f, (height - 164) * 0.5f - 20));
    const float margin = radius + 18;
    return {std::clamp(hunterX, x + margin, x + width - margin),
        std::clamp(hunterY, y + 86 + margin, y + height - 78 - margin), radius};
}

struct ScoreLayout {
    Rect card;
    float rowHeight;
};
inline ScoreLayout score_layout(float x, float y, float width, float height, int rows) {
    const float available = std::max(0.0f, height - 96);
    const float heading = std::min(54.0f, available);
    const float row = rows > 0 ? std::min(20.0f, std::max(0.0f, available - heading) / rows) : 20;
    const float w = std::min(540.0f, std::max(0.0f, width - 32));
    const float h = heading + row * rows;
    return {{x + (width - w) * 0.5f, y + 60 + (available - h) * 0.5f, w, h}, row};
}
}
