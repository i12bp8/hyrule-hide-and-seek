#pragma once
#include "net.hpp"

#include <vector>

namespace hs::net {

struct Sent {
    uint8_t to;
    std::vector<uint8_t> bytes;
};

struct FakeNet {
    bool online = true;
    uint8_t self = 1;
    uint8_t host = 1;
    std::string code = "TESTS";
    Member members[kSlots];
    std::vector<Sent> sent;
    int metaCount = 0;

    void add(int id, const char* name) {
        members[id].present = true;
        copy_str(members[id].name, name);
    }
    void remove(int id) { members[id].present = false; }
};

extern FakeNet g_fake;

}  // namespace hs::net
