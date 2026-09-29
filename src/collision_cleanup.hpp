#pragma once
#include <cstddef>
#include <cstdint>

namespace hs {
// The game retains this tick's collision pointers until the draw/collision pass. Clear entries
// inside an actor before its destructor can run or its storage can be reused by another actor.
template<class Object>
size_t clear_actor_collision(Object** entries, uint16_t& count, const void* actor, size_t bytes) {
    const auto start = reinterpret_cast<uintptr_t>(actor);
    size_t removed = 0;
    uint16_t kept = 0;
    const uint16_t previous = count;
    for (uint16_t i = 0; i < previous; ++i) {
        const auto pointer = reinterpret_cast<uintptr_t>(entries[i]);
        if (pointer >= start && pointer - start < bytes) {
            ++removed;
        } else if (entries[i] != nullptr) entries[kept++] = entries[i];
    }
    // Camera queries assume every entry below the count is non-null, even though the combat
    // collision pass accepts holes. Compact all lists so both readers remain safe.
    for (uint16_t i = kept; i < previous; ++i) entries[i] = nullptr;
    count = kept;
    return removed;
}
}
