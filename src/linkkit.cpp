#include "linkkit.hpp"

#include "common.hpp"
#include "recolor.hpp"

#include <mods/svc/texture.h>

#include "JSystem/J3DGraphAnimator/J3DAnimation.h"
#include "JSystem/J3DGraphAnimator/J3DModelData.h"
#include "JSystem/J3DGraphBase/J3DTexture.h"
#include "JSystem/J3DGraphLoader/J3DAnmLoader.h"
#include "JSystem/JKernel/JKRArchive.h"
#include "JSystem/JKernel/JKRExpHeap.h"
#include "JSystem/JKernel/JKRHeap.h"
#include "d/actor/d_a_alink.h"
#include "d/d_com_inf_game.h"
#include "d/d_resorce.h"
#include "m_Do/m_Do_ext.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace hs::linkkit {

namespace {

constexpr u32 kHeapWanted = 24u << 20;
constexpr u32 kHeapMinimum = 6u << 20;
constexpr u32 kRootReserve = 16u << 20;
constexpr u32 kAnimBufferSize = 0x10800;  // what daAlink_c's own animation buffers hold
constexpr int kAnimCacheSize = 256;

JKRExpHeap* s_heap = nullptr;
bool s_heapTried = false;

struct Raw {
    u8* data = nullptr;
    u32 size = 0;
};

struct Files {
    bool tried = false;
    bool ok = false;
    Raw body, head, face, hands, sword, sheath, shield;
} s_files;

LinkModels s_shared;  // face, hands and equipment, same for every colour
bool s_sharedLoaded = false;
LinkModels s_models[kMaxPlayers];
bool s_modelsTried[kMaxPlayers] = {};

struct AnimEntry {
    u16 idx = 0xFFFF;
    J3DAnmTransform* anm = nullptr;
    u8* buffer = nullptr;
    uint64_t failedAt = 0;
};
AnimEntry s_anims[kAnimCacheSize];

struct HeapScope {
    JKRHeap* previous;
    explicit HeapScope(JKRHeap* h) : previous(mDoExt_setCurrentHeap(h)) {}
    ~HeapScope() { mDoExt_setCurrentHeap(previous); }
};

// ---- files ---------------------------------------------------------------------------------

Raw copy_out(JKRArchive* arc, const char* name) {
    Raw out;
    void* res = arc->getResource('BMWR', name);
    if (res == nullptr) {
        mods::log::warn("linkkit: {} not found", name);
        return out;
    }
    const u32 size = arc->getResSize(res);
    u8* buf = static_cast<u8*>(s_heap->alloc(size, 32));
    if (buf == nullptr) return out;
    std::memcpy(buf, res, size);
    out.data = buf;
    out.size = size;
    return out;
}

bool load_files() {
    if (s_files.tried) return s_files.ok;
    s_files.tried = true;
    if (heap() == nullptr) return false;

    struct Want {
        const char* arc;
        std::vector<std::pair<const char*, Raw*>> files;
    };
    const Want wants[] = {
        {"/res/Object/Kmdl.arc",
            {{"al.bmd", &s_files.body}, {"al_head.bmd", &s_files.head}, {"al_face.bmd", &s_files.face},
                {"al_hands.bmd", &s_files.hands}}},
        {"/res/Object/Alink.arc", {{"al_swa.bmd", &s_files.sword}, {"al_poda.bmd", &s_files.sheath}}},
        {"/res/Object/HyShd.arc", {{"al_sha.bmd", &s_files.shield}}},
    };
    for (const Want& want : wants) {
        JKRArchive* arc =
            JKRArchive::mount(want.arc, JKRArchive::MOUNT_MEM, s_heap, JKRArchive::MOUNT_DIRECTION_TAIL);
        if (arc == nullptr) {
            mods::log::warn("linkkit: could not open {}", want.arc);
            continue;
        }
        for (const auto& [name, raw] : want.files) *raw = copy_out(arc, name);
        arc->unmount();
    }
    s_files.ok = s_files.body.data && s_files.head.data && s_files.face.data && s_files.hands.data;
    mods::log::info("linkkit: Link's files {} ({} KB free in our heap)", s_files.ok ? "ready" : "MISSING",
        s_heap->getFreeSize() / 1024);
    return s_files.ok;
}

J3DModelData* load_model(const Raw& raw, bool copy, int color) {
    if (raw.data == nullptr) return nullptr;
    u8* data = raw.data;
    if (copy) {
        data = static_cast<u8*>(s_heap->alloc(raw.size, 32));
        if (data == nullptr) return nullptr;
        std::memcpy(data, raw.data, raw.size);
        if (color > 0) recolor::bmd(data, raw.size, static_cast<uint8_t>(color));
    }
    HeapScope scope(s_heap);
    return dRes_info_c::loaderBasicBmd('BMWR', data);
}

// ---- local tunic ----------------------------------------------------------------------------

struct LocalTex {
    const void* pointer = nullptr;
    TextureReplacementHandle handle = 0;
};
LocalTex s_local[8];
int s_localColor = -1;

}  // namespace

JKRHeap* heap() {
    if (s_heap != nullptr || s_heapTried) return s_heap;
    s_heapTried = true;
    JKRHeap* root = JKRHeap::getRootHeap();
    if (root == nullptr) return nullptr;
    const u32 rootFree = static_cast<u32>(root->getFreeSize());
    u32 size = kHeapWanted;
    if (rootFree < size + kRootReserve) size = rootFree > kRootReserve ? rootFree - kRootReserve : 0;
    if (size < kHeapMinimum) {
        mods::log::warn("linkkit: only {} KB free in the root heap, other players won't be drawn",
            rootFree / 1024);
        return nullptr;
    }
    s_heap = JKRExpHeap::create(size, root, false);
    if (s_heap != nullptr) {
        s_heap->setName("HideSeekHeap");
        mods::log::info("linkkit: {} MB heap for other players", size >> 20);
    }
    return s_heap;
}

bool ready() {
    if (!load_files()) return false;
    // Cache both poses while the local Link's archive is known to be live. Remote actors are
    // recreated during a map warp; asking for their first animation in that transition window was
    // the reason a hunter could have a name tag but no body for the whole Seek phase.
    anim(kIdleAnim);
    anim(kWalkAnim);
    return true;
}

const LinkModels* link_models(uint8_t color) {
    if (color >= kMaxPlayers || !load_files()) return nullptr;
    if (!s_sharedLoaded) {
        s_sharedLoaded = true;
        s_shared.face = load_model(s_files.face, false, 0);
        s_shared.hands = load_model(s_files.hands, false, 0);
        s_shared.sword = load_model(s_files.sword, false, 0);
        s_shared.sheath = load_model(s_files.sheath, false, 0);
        s_shared.shield = load_model(s_files.shield, false, 0);
        if (s_shared.hands != nullptr) {
            // Every hand pose is its own shape; the puppet shows one per hand.
            // Materials 0-4 are left-hand poses, 5-10 right-hand ones; show a relaxed pair.
            for (u16 i = 0; i < s_shared.hands->getMaterialNum(); ++i) {
                J3DShape* shape = s_shared.hands->getMaterialNodePointer(i)->getShape();
                if (i == 1 || i == 6) shape->show();
                else shape->hide();
            }
        }
    }
    LinkModels& m = s_models[color];
    if (!s_modelsTried[color]) {
        s_modelsTried[color] = true;
        m = s_shared;
        m.body = load_model(s_files.body, true, color);
        m.head = load_model(s_files.head, true, color);
        mods::log::info("linkkit: {} tunic {}", color_of(color).name, m.body ? "loaded" : "FAILED");
    }
    return m.body != nullptr && m.head != nullptr && m.face != nullptr && m.hands != nullptr ? &m : nullptr;
}

J3DAnmTransform* anim(uint16_t idx) {
    if (idx == 0xFFFF || heap() == nullptr) return nullptr;
    AnimEntry* slot = nullptr;
    const uint64_t now = now_ms();
    for (AnimEntry& e : s_anims) {
        if (e.idx == idx) {
            if (e.failedAt == 0) return e.anm;
            if (now - e.failedAt < 1000) return nullptr;
            // Resource reads can fail briefly while AlAnm is being remounted for a stage. A
            // transient miss must not permanently make every remote Link invisible.
            e = AnimEntry{};
            slot = &e;
            break;
        }
        if (slot == nullptr && e.idx == 0xFFFF) slot = &e;
    }
    if (slot == nullptr) return nullptr;  // cache full: the puppet falls back to idle
    slot->idx = idx;
    JKRArchive* arc = dComIfGp_getAnmArchive();
    if (arc == nullptr) {
        slot->idx = 0xFFFF;  // try again once the game has its archive
        return nullptr;
    }
    u8* buffer = static_cast<u8*>(s_heap->alloc(kAnimBufferSize, 32));
    if (buffer == nullptr) {
        slot->failedAt = now;
        return nullptr;
    }
    const u32 read = JKRReadIdxResource(buffer, kAnimBufferSize, idx, arc);
    if (read == 0) {
        s_heap->free(buffer);
        slot->failedAt = now;
        return nullptr;
    }
    s_heap->resize(buffer, (read + 31) & ~31u);
    J3DAnmBase* loaded;
    {
        HeapScope scope(s_heap);
        loaded = J3DAnmLoaderDataBase::load(buffer);
    }
    if (loaded == nullptr || loaded->getKind() != 0) {  // 0 = transform (bck)
        s_heap->free(buffer);
        slot->failedAt = now;
        return nullptr;
    }
    slot->anm = static_cast<J3DAnmTransform*>(loaded);
    slot->buffer = buffer;
    return slot->anm;
}

void recolor_local_link(uint8_t color) {
    if (svc_texture == nullptr) return;
    daAlink_c* link = daAlink_getAlinkActorClass();
    if (link == nullptr || link->checkWolf()) return;
    J3DModel* models[2] = {link->mpLinkModel, link->mpLinkHatModel};
    const void* pointers[8] = {};
    int n = 0;
    for (J3DModel* model : models) {
        if (model == nullptr) continue;
        J3DTexture* tex = model->getModelData()->getTexture();
        for (u16 i = 0; tex != nullptr && i < tex->getNum() && n < 8; ++i) {
            if (tex->getResTIMG(i)->format == GX_TF_CMPR) pointers[n++] = tex->getImgDataPtr(i);
        }
    }
    bool same = s_localColor == color;
    for (int i = 0; i < 8 && same; ++i) same = s_local[i].pointer == pointers[i];
    if (same) return;

    for (LocalTex& t : s_local) {
        if (t.handle != 0) svc_texture->unregister(mod_ctx, t.handle);
        t = LocalTex{};
    }
    s_localColor = color;
    if (color == 0) return;
    int k = 0;
    for (J3DModel* model : models) {
        if (model == nullptr) continue;
        J3DTexture* tex = model->getModelData()->getTexture();
        for (u16 i = 0; tex != nullptr && i < tex->getNum() && k < 8; ++i) {
            const ResTIMG* timg = tex->getResTIMG(i);
            if (timg->format != GX_TF_CMPR) continue;
            const u32 w = timg->width, h = timg->height;
            const u32 mips = std::max<u32>(timg->mipmapCount, 1);
            const u32 bytes = recolor::cmpr_size(w, h, mips);
            std::vector<u8> copy(tex->getImgDataPtr(i), tex->getImgDataPtr(i) + bytes);
            recolor::cmpr(copy.data(), bytes, color_of(color));
            TextureKey key = TEXTURE_KEY_INIT;
            key.kind = TEXTURE_KEY_POINTER;
            key.pointer = tex->getImgDataPtr(i);
            key.width = w;
            key.height = h;
            key.gx_format = GX_TF_CMPR;
            TextureData data = TEXTURE_DATA_INIT;
            data.data = copy.data();
            data.size = bytes;
            data.width = w;
            data.height = h;
            data.mip_count = mips;
            data.gx_format = GX_TF_CMPR;
            s_local[k].pointer = key.pointer;
            svc_texture->register_data(mod_ctx, &key, &data, &s_local[k].handle);
            ++k;
        }
    }
}

void shutdown() {
    if (svc_texture != nullptr) {
        for (LocalTex& t : s_local) {
            if (t.handle != 0) svc_texture->unregister(mod_ctx, t.handle);
            t = LocalTex{};
        }
    }
    s_localColor = -1;
    // Everything below lives in our heap; destroying it frees all of it at once. Puppets are
    // gone by now (the actors are deleted before the mod unloads).
    for (AnimEntry& e : s_anims) e = AnimEntry{};
    for (int i = 0; i < kMaxPlayers; ++i) {
        s_models[i] = LinkModels{};
        s_modelsTried[i] = false;
    }
    s_shared = LinkModels{};
    s_sharedLoaded = false;
    s_files = Files{};
    if (s_heap != nullptr) {
        s_heap->destroy();
        s_heap = nullptr;
    }
    s_heapTried = false;
}

}  // namespace hs::linkkit
