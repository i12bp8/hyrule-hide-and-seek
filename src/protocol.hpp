#pragma once

// Game messages. The relay prepends/strips one byte (see server/src/room.js); everything here is
// the payload. All numbers are little-endian. server/bots.mjs speaks the same format; keep them in
// sync and bump kProtocolVersion on any change (the relay refuses mismatched versions).

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace hs {

// v12: full arenas without play-area bounds, new scenery, no hunter whistle.
// v11: Prop Hunt only, the new disguise catalogue and arenas, decoy swaps, hunter whistles and
// score awards.
constexpr int kProtocolVersion = 12;

enum MsgType : uint8_t {
    MSG_STATE = 1,     // everyone -> everyone, 10 Hz
    MSG_HELLO = 2,     // everyone -> everyone when joining: wanted colour
    MSG_SETTINGS = 10, // host -> all
    MSG_ROSTER = 11,   // host -> all
    MSG_ROUND = 12,    // host -> all: a round starts
    MSG_PHASE = 13,    // host -> all
    MSG_FOUND = 14,    // host -> all: a hider was found
    MSG_RESULTS = 15,  // host -> all: round over
    MSG_DECOYS = 16,   // host -> all: complete active-decoy snapshot
    MSG_RUPEES = 17,   // host -> all: active treasure snapshot
    MSG_CLUE = 18,     // host -> all: round, hider, sound, clue kind
    MSG_PICKUP = 19,   // host -> all: round, hider, points, completed collection bonus
    MSG_READY = 20,    // -> host: loaded into the round's map
    MSG_HIT = 21,      // -> host: I hit / touched this hider
    MSG_HUNTER_OUT = 22, // host -> all: round, eliminated hunter
    MSG_TAUNT = 23,    // hider -> host: round, sound
    MSG_PLACE_DECOY = 24, // hider -> host
    MSG_HIT_DECOY = 25,   // hunter -> host: remove the struck decoy
    MSG_COLLECT_RUPEE = 26, // hider -> host: round, pickup id
    MSG_MISS = 27,     // hunter -> host: round, cumulative missed swings (u16)
    MSG_SWAP = 28,     // hider -> host: round; swap places with my newest decoy
    MSG_TELEPORT = 29, // host -> one player: round, x, y, z, yaw (a confirmed swap)
    // 30 was the v11 hunter whistle; keep the number unused.
    MSG_AWARD = 31,    // host -> all: round, player, award kind, points
};

// Score feedback the host announces with MSG_AWARD.
enum class Award : uint8_t {
    DecoyFooled,   // a hunter struck your decoy
    CloseCall,     // a hunter swung and missed right next to you
    BoldTaunt,     // a taunt with a hunter close by
    FirstBlood,    // the round's first find
    QuickFind,     // another find soon after your last
    LastStanding,  // the only prop left when time ran out
    Count
};

enum class ClueKind : uint8_t { Manual, Final, Stationary, Treasure };

enum class Phase : uint8_t { Lobby = 0, Gather = 1, Hide = 2, Seek = 3, Results = 4 };
enum class Role : uint8_t { None = 0, Hider = 1, Hunter = 2, Spectator = 3 };

constexpr uint8_t kRandomMap = 0xFF;

class Writer {
public:
    explicit Writer(uint8_t type) { u8(type); }
    void u8(uint8_t v) { m_buf.push_back(v); }
    void s8(int8_t v) { u8(static_cast<uint8_t>(v)); }
    void u16(uint16_t v) {
        u8(static_cast<uint8_t>(v));
        u8(static_cast<uint8_t>(v >> 8));
    }
    void s16(int16_t v) { u16(static_cast<uint16_t>(v)); }
    void u32(uint32_t v) {
        u16(static_cast<uint16_t>(v));
        u16(static_cast<uint16_t>(v >> 16));
    }
    void f32(float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, 4);
        u32(bits);
    }
    // Fixed-size, zero padded string.
    void fixed(const char* s, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            u8(s != nullptr && std::strlen(s) > i ? static_cast<uint8_t>(s[i]) : 0);
        }
    }
    const std::vector<uint8_t>& bytes() const { return m_buf; }

private:
    std::vector<uint8_t> m_buf;
};

class Reader {
public:
    Reader(const uint8_t* data, size_t size) : m_data(data), m_size(size) {}
    bool ok() const { return m_ok; }
    uint8_t u8() {
        if (m_pos + 1 > m_size) {
            m_ok = false;
            return 0;
        }
        return m_data[m_pos++];
    }
    int8_t s8() { return static_cast<int8_t>(u8()); }
    uint16_t u16() {
        const uint16_t lo = u8();
        return static_cast<uint16_t>(lo | (u8() << 8));
    }
    int16_t s16() { return static_cast<int16_t>(u16()); }
    uint32_t u32() {
        const uint32_t lo = u16();
        return lo | (static_cast<uint32_t>(u16()) << 16);
    }
    float f32() {
        const uint32_t bits = u32();
        float v;
        std::memcpy(&v, &bits, 4);
        return v;
    }
    // Reads `wire` bytes into `out` (which holds wire + 1 chars) and terminates it.
    void fixed(char* out, size_t wire) {
        for (size_t i = 0; i < wire; ++i) out[i] = static_cast<char>(u8());
        out[wire] = '\0';
    }

private:
    const uint8_t* m_data;
    size_t m_size;
    size_t m_pos = 0;
    bool m_ok = true;
};

// One animation slot of Link's upper or lower body (daAlink_c::mNowAnmPackUnder/Upper).
struct AnimSlot {
    uint16_t idx = 0xFFFF;  // index into the AlAnm archive, 0xFFFF = none
    float frame = 0.0f;
    uint8_t ratio = 0;  // blend weight * 255
};

enum StateFlags : uint8_t {
    STATE_IN_WORLD = 1 << 0,  // standing in a stage (not on a menu or loading)
    STATE_WOLF = 1 << 1,
    STATE_DISGUISED = 1 << 2,  // drawn as a prop
    STATE_SWORD = 1 << 3,      // sword in hand
    STATE_SHIELD = 1 << 4,     // shield in hand
    STATE_COMPACT = 1 << 5,    // no Link animation slots (props / loading)
};

struct PlayerState {
    uint8_t flags = 0;
    char stage[9] = {};
    int8_t room = -1;
    float x = 0, y = 0, z = 0;
    int16_t yaw = 0;
    uint8_t prop = 0;
    int16_t propYaw = 0;
    AnimSlot under[3];
    AnimSlot upper[3];

    void write(Writer& w) const {
        w.u8(flags);
        w.fixed(stage, 8);
        w.s8(room);
        w.f32(x);
        w.f32(y);
        w.f32(z);
        w.s16(yaw);
        w.u8(prop);
        w.s16(propYaw);
        if (flags & STATE_COMPACT) return;
        for (const AnimSlot& a : under) write_slot(w, a);
        for (const AnimSlot& a : upper) write_slot(w, a);
    }

    void read(Reader& r) {
        flags = r.u8();
        r.fixed(stage, 8);
        room = r.s8();
        x = r.f32();
        y = r.f32();
        z = r.f32();
        yaw = r.s16();
        prop = r.u8();
        propYaw = r.s16();
        if (flags & STATE_COMPACT) return;
        for (AnimSlot& a : under) read_slot(r, a);
        for (AnimSlot& a : upper) read_slot(r, a);
    }

private:
    static void write_slot(Writer& w, const AnimSlot& a) {
        w.u16(a.idx);
        w.f32(a.frame);
        w.u8(a.ratio);
    }
    static void read_slot(Reader& r, AnimSlot& a) {
        a.idx = r.u16();
        a.frame = r.f32();
        a.ratio = r.u8();
    }
};

}  // namespace hs
