#include "puppet.hpp"

#include "common.hpp"
#include "linkkit.hpp"
#include "local.hpp"
#include "maps.hpp"
#include "match.hpp"
#include "net.hpp"
#include "props.hpp"

#include <mods/svc/actor.h>

#include "JSystem/J3DGraphAnimator/J3DJoint.h"
#include "JSystem/J3DGraphAnimator/J3DModel.h"
#include "JSystem/J3DGraphAnimator/J3DModelData.h"
#include "JSystem/JKernel/JKRExpHeap.h"
#include "SSystem/SComponent/c_lib.h"
#include "SSystem/SComponent/c_math.h"
#include "d/d_bg_s_gnd_chk.h"
#include "d/d_cc_d.h"
#include "d/d_com_inf_game.h"
#include "d/d_kankyo.h"
#include "f_op/f_op_actor_mng.h"
#include "m_Do/m_Do_ext.h"
#include "m_Do/m_Do_mtx.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

// actor.h has no extern declaration of its own; mod.cpp imports it.
extern const ActorService* svc_actor;

namespace hs::puppet {

namespace {

constexpr u32 kLocalFlag = 0x100;
constexpr u32 kDecoyFlag = 0x200;
constexpr int kPropShift = 16;
constexpr int kDecoyCount = kCoverPointCount;
constexpr u32 kPuppetHeapSize = 640 * 1024;
constexpr u32 kPropHeapSize = 128 * 1024;
constexpr uint64_t kStaleMs = 4000;
constexpr float kSnapDistance = 600.0f;
constexpr float kLinkRadius = 40.0f;
constexpr float kLinkHeight = 150.0f;

// Human Link's joints (d_a_alink_wolf.inc): head 4, back 5, hands 9 and 14, held items 10 and 15.
constexpr u16 kJointHead = 4;
constexpr u16 kJointBack = 5;
constexpr u16 kJointHandL = 9;
constexpr u16 kJointItemL = 10;
constexpr u16 kJointHandR = 14;
constexpr u16 kJointItemR = 15;
constexpr u16 kJointLegs = 16;

ProfileName s_procName = -1;
ActorHandle s_handle = 0;
bool s_registered = false;

struct Slot {
    fpc_ProcID actor = fpcM_ERROR_PROCESS_ID_e;
    uint8_t color = 0xFF;
    // Written by the actor every frame, read by the HUD.
    bool visible = false;
    // A model can fail to draw while its fresh network position is still useful for hunter
    // markers, taunt clues and Hide & Seek touch tags.
    bool tracked = false;
    cXyz feet{0.0f, 0.0f, 0.0f};
    float height = 0.0f;
};
Slot s_slots[kSlots];
Slot s_localProp;
Slot s_decoys[kDecoyCount];
uint64_t s_worldSince = 0;
uint32_t s_decoyRound = 0;
int s_decoyMap = -1;

// Link's sword can hit these: both swords, plus the wolf in case a hunter transforms.
const dCcD_SrcCyl kCylSrc = {
    {
        {0, {{0, 0, 0}, {AT_TYPE_NORMAL_SWORD | AT_TYPE_MASTER_SWORD | AT_TYPE_WOLF_ATTACK | AT_TYPE_WOLF_CUT_TURN, 0x3}, 0}},
        {dCcD_SE_NONE, 0, 0, 0, {0}},
        {dCcD_SE_NONE, 0, 0, 0, {6}},
        {0},
    },
    {{
        {0.0f, 0.0f, 0.0f},
        kLinkRadius,
        kLinkHeight,
    }},
};

struct HeapScope {
    JKRHeap* previous;
    explicit HeapScope(JKRHeap* h) : previous(mDoExt_setCurrentHeap(h)) {}
    ~HeapScope() { mDoExt_setCurrentHeap(previous); }
};

// Prop models share their data with the real objects in the world, which put their own joint
// callbacks and animations on it. Swap those out around our calc() and put them back after.
class JointGuard {
public:
    explicit JointGuard(J3DModelData* data) : m_data(data) {
        const u16 n = data->getJointNum();
        m_saved.reserve(n);
        for (u16 i = 0; i < n; ++i) {
            J3DJoint* j = data->getJointNodePointer(i);
            m_saved.push_back({j->getCallBack(), j->getMtxCalc()});
            j->setCallBack(nullptr);
            j->setMtxCalc(nullptr);
        }
    }
    ~JointGuard() {
        for (u16 i = 0; i < m_saved.size(); ++i) {
            J3DJoint* j = m_data->getJointNodePointer(i);
            j->setCallBack(m_saved[i].first);
            j->setMtxCalc(m_saved[i].second);
        }
    }

private:
    J3DModelData* m_data;
    std::vector<std::pair<J3DJointCallBack, J3DMtxCalc*>> m_saved;
};

bool same_stage_as_me(const PlayerState& s) {
    return (s.flags & STATE_IN_WORLD) != 0 && std::strncmp(s.stage, local::stage(), 8) == 0;
}

// Match the native actor's shadow instead of giving every disguise the same telltale dark blob.
// Carryables and a few movable objects use simple shadows with fixed sizes; most static scenery
// has no dynamic shadow at all (encoded by PropInfo::shadowScale == 0).
float native_simple_shadow_size(int kind) {
    switch (kind) {
    case 0: return 40.0f;  // pot
    case 1: return 50.0f;  // big pot
    case 2: return 87.0f;  // crate (the base game's value)
    case 3: return 50.0f;  // barrel
    case 4: return 40.0f;  // skull
    case 5: return 50.0f;  // pumpkin
    case 6: return 40.0f;  // Kakariko pot
    case 10: return 40.0f; // cannonball
    case 11: return 45.0f; // Deku nut
    case 12: return 50.0f; // big blue pot
    case 13: return 40.0f; // small Twilight pot
    case 14: return 50.0f; // big Twilight pot
    case 22: return 25.0f; // bomb
    case 52: return 40.0f; // small mountain rock
    case 53: return 65.0f; // large mountain rock
    case 56: return 90.0f; // pushable grave
    default: return 0.0f;
    }
}

struct ModelShadowProfile {
    float yOffset;
    float depth;
    float footprint;
};

ModelShadowProfile native_model_shadow(int kind, const PropInfo& info) {
    switch (kind) {
    case 7: return {50.0f, 500.0f, 0.0f};   // small crate
    case 9: return {100.0f, 400.0f, 40.0f}; // Cucco
    case 15: return {0.0f, 400.0f, 0.0f};   // sign
    case 23: return {50.0f, 400.0f, 0.0f};  // beehive dropped on the ground
    case 35: return {0.0f, 800.0f, 120.0f}; // oil jar
    case 38: return {0.0f, 500.0f, 0.0f};   // howling stone
    case 42: return {0.0f, 400.0f, 20.0f};  // pole target
    case 43: return {0.0f, 400.0f, 20.0f};  // board target
    case 48: return {0.0f, 500.0f, 0.0f};   // Sacred Grove stone
    case 51: return {0.0f, 2000.0f, 0.0f};  // volcanic ball
    case 58: return {100.0f, 500.0f, 0.0f}; // map table
    default:
        return {std::max(info.height, 40.0f), std::max(info.height * 4.0f, 400.0f), 0.0f};
    }
}

class Puppet : public fopAc_ac_c {
public:
    Puppet() : mLinkCalc(1, &mLinkAnim) {}

    int create();
    int execute();
    int draw();
    int destroy();

private:
    bool buildLink(uint8_t color);
    void freeLink();
    bool poseLink(const PlayerState& s, bool moving);
    bool updateProp(int kind, bool moving);
    void releaseProp();
    void updateGround();
    void armHitbox(const match::Player& p, bool disguised, int kind);
    void publish(bool visible, float height, bool tracked = false);
    Slot& slot() { return mLocal ? s_localProp : s_slots[mSlot]; }

    int mSlot = 0;
    bool mLocal = false;
    bool mDecoy = false;
    bool mGrounded = false;
    int mFixedProp = 0;
    bool mVisible = false;
    bool mDisguised = false;
    bool mHaveTarget = false;
    JKRExpHeap* mHeap = nullptr;

    // Link
    uint8_t mColor = 0xFF;
    J3DModel* mBody = nullptr;
    J3DModel* mHead = nullptr;
    J3DModel* mFace = nullptr;
    J3DModel* mHands = nullptr;
    J3DModel* mSword = nullptr;
    J3DModel* mSheath = nullptr;
    J3DModel* mShield = nullptr;
    mDoExt_AnmRatioPack mLinkAnim;
    mDoExt_MtxCalcAnmBlendTbl mLinkCalc;
    J3DAnmTransform* mPoseAnim = nullptr;
    float mPoseFrame = 0.0f;

    // Prop
    int mPropKind = -1;
    bool mPropRequested = false;
    bool mPropFailed = false;
    const char* mPropArc = nullptr;
    request_of_phase_process_class mPropPhase;
    JKRExpHeap* mPropHeap = nullptr;
    J3DModel* mPropModel = nullptr;
    mDoExt_bckAnm* mPropIdle = nullptr;
    mDoExt_bckAnm* mPropMove = nullptr;
    bool mPropMoving = false;
    int mWantedProp = -1;

    // Ground, shadow and the hunters' target
    dBgS_ObjGndChk mGndChk;
    f32 mGroundY = -G_CM3D_F_INF;
    dCcD_Stts mStts;
    dCcD_Cyl mCyl;
    bool mCylArmed = false;
    u32 mShadowKey = 0;
};

int Puppet::create() {
    fopAcM_ct(this, Puppet);
    const u32 prm = fopAcM_GetParam(this);
    mSlot = static_cast<int>(prm & 0xFF);
    mLocal = (prm & kLocalFlag) != 0;
    mDecoy = (prm & kDecoyFlag) != 0;
    mFixedProp = static_cast<int>((prm >> kPropShift) & 0xFF);
    const int maxSlot = mDecoy ? kDecoyCount : kMaxPlayers;
    if (mSlot < 1 || mSlot > maxSlot || (mDecoy && mFixedProp >= prop_count()) ||
        linkkit::heap() == nullptr) {
        return cPhs_ERROR_e;
    }
    // Only a remote human Link needs the large private model heap. Local props and decorative
    // decoys allocate only their small prop-model heap.
    if (!mLocal && !mDecoy) {
        mHeap = JKRExpHeap::create(kPuppetHeapSize, linkkit::heap(), false);
        if (mHeap == nullptr) {
            mods::log::warn("puppet {}: out of memory", mSlot);
            return cPhs_ERROR_e;
        }
    }
    mStts.Init(0xFF, 0xFF, this);
    mCyl.Set(kCylSrc);
    mCyl.SetStts(&mStts);
    if (mDecoy) mCyl.OffTgSetBit();
    fopAcM_setCullSizeBox(this, -160.0f, -20.0f, -160.0f, 160.0f, 260.0f, 160.0f);
    return cPhs_COMPLEATE_e;
}

bool Puppet::buildLink(uint8_t color) {
    freeLink();
    const linkkit::LinkModels* m = linkkit::link_models(color);
    if (m == nullptr) return false;
    HeapScope scope(mHeap);
    mBody = mDoExt_J3DModel__create(m->body, 0x80000, 0x11000084);
    mHead = mDoExt_J3DModel__create(m->head, 0x80000, 0x11000084);
    mFace = mDoExt_J3DModel__create(m->face, 0x80000, 0x11020284);
    mHands = mDoExt_J3DModel__create(m->hands, 0x80000, 0x11000084);
    if (m->sword != nullptr) mSword = mDoExt_J3DModel__create(m->sword, 0x80000, 0x11000284);
    if (m->sheath != nullptr) mSheath = mDoExt_J3DModel__create(m->sheath, 0x80000, 0x11000084);
    if (m->shield != nullptr) mShield = mDoExt_J3DModel__create(m->shield, 0x80000, 0x11000084);
    if (mBody == nullptr || mHead == nullptr || mFace == nullptr || mHands == nullptr) {
        mods::log::warn("puppet {}: could not build Link ({} KB free)", mSlot, mHeap->getFreeSize() / 1024);
        freeLink();
        return false;
    }
    mColor = color;
    mPoseAnim = nullptr;
    mPoseFrame = 0.0f;
    return true;
}

void Puppet::freeLink() {
    // Models were allocated in mHeap; freeing the heap's contents releases them all.
    if (mHeap != nullptr) mHeap->freeAll();
    mBody = mHead = mFace = mHands = mSword = mSheath = mShield = nullptr;
    mLinkAnim.setAnmTransform(nullptr);
    mLinkAnim.setRatio(0.0f);
    mPoseAnim = nullptr;
    mPoseFrame = 0.0f;
    mColor = 0xFF;
}

bool Puppet::poseLink(const PlayerState& s, bool moving) {
    // Remote animation blends can briefly reference cutscene-only resources or half-loaded upper
    // body poses. For multiplayer readability, use one known complete animation for the whole
    // skeleton: idle while still, walk while moving. Never fall back to an unanimated bind pose.
    J3DAnmTransform* wanted = linkkit::anim(moving ? linkkit::kWalkAnim : linkkit::kIdleAnim);
    if (wanted == nullptr && mPoseAnim == nullptr) wanted = linkkit::anim(linkkit::kIdleAnim);
    if (wanted != nullptr && wanted != mPoseAnim) {
        mPoseAnim = wanted;
        mPoseFrame = 0.0f;
    }
    if (mPoseAnim == nullptr) return false;

    const float maxFrame = static_cast<float>(mPoseAnim->getFrameMax());
    mPoseFrame += moving ? 1.1f : 0.45f;
    if (maxFrame > 0.0f) mPoseFrame = std::fmod(mPoseFrame, maxFrame);
    else mPoseFrame = 0.0f;
    mPoseAnim->setFrame(mPoseFrame);
    mLinkAnim.setAnmTransform(mPoseAnim);
    mLinkAnim.setRatio(1.0f);

    mDoMtx_stack_c::transS(current.pos.x, current.pos.y, current.pos.z);
    mDoMtx_stack_c::YrotM(shape_angle.y);
    mBody->setBaseTRMtx(mDoMtx_stack_c::get());
    // The actor system uses this matrix to transform our custom culling box. Without it, the
    // replacement can be culled even while standing directly in front of the camera.
    fopAcM_SetMtx(this, mBody->getBaseTRMtx());

    // One calculator at the root drives every joint. Clear the old split joints in case this model
    // was rebuilt after a colour or stage change.
    J3DModelData* data = mBody->getModelData();
    data->getJointNodePointer(0)->setMtxCalc(&mLinkCalc);
    if (data->getJointNum() > 1) data->getJointNodePointer(1)->setMtxCalc(nullptr);
    if (data->getJointNum() > kJointLegs) data->getJointNodePointer(kJointLegs)->setMtxCalc(nullptr);
    mBody->calc();

    mFace->setBaseTRMtx(mBody->getAnmMtx(kJointHead));
    mFace->calc();
    mHead->setBaseTRMtx(mBody->getAnmMtx(kJointHead));
    mHead->calc();
    mHands->setBaseTRMtx(mBody->getBaseTRMtx());
    mHands->calc();
    mHands->setAnmMtx(1, mBody->getAnmMtx(kJointHandL));
    mHands->setAnmMtx(2, mBody->getAnmMtx(kJointHandR));

    // Equipment, placed the way daAlink_c::setItemMatrix does it.
    if (mSheath != nullptr) {
        mSheath->setBaseTRMtx(mBody->getAnmMtx(kJointBack));
        mSheath->calc();
    }
    if (mSword != nullptr) {
        if (s.flags & STATE_SWORD) {
            mSword->setBaseTRMtx(mBody->getAnmMtx(kJointItemL));
        } else {
            mDoMtx_stack_c::copy(mBody->getAnmMtx(kJointBack));
            mDoMtx_stack_c::transM(-18.5f, 0.14f, 12.2f);
            mDoMtx_stack_c::XYZrotM(0, cM_deg2s(33.1f), 0);
            mSword->setBaseTRMtx(mDoMtx_stack_c::get());
        }
        mSword->calc();
    }
    if (mShield != nullptr) {
        if (s.flags & STATE_SHIELD) {
            mShield->setBaseTRMtx(mBody->getAnmMtx(kJointItemR));
        } else {
            mDoMtx_stack_c::copy(mBody->getAnmMtx(kJointBack));
            mDoMtx_stack_c::transM(4.2f, -4.4f, -20.0f);
            mDoMtx_stack_c::XYZrotM(cM_deg2s(91.0f), cM_deg2s(57.0f), cM_deg2s(180.0f));
            mShield->setBaseTRMtx(mDoMtx_stack_c::get());
        }
        mShield->calc();
    }
    return true;
}

bool Puppet::updateProp(int kind, bool moving) {
    if (kind != mWantedProp) {
        releaseProp();
        mWantedProp = kind;
        mPropKind = kind;
    }
    if (mPropKind < 0) mPropKind = kind;
    // Never make a player or decoy disappear because one optional model failed. Retry the known
    // good first catalogue entry (the small Ordon pot) until the player chooses another prop.
    if (mPropFailed) {
        if (mPropKind == 0) return false;
        mods::log::warn("puppet: {} failed; showing Pot instead", prop_info(mPropKind).name);
        releaseProp();
        mPropKind = 0;
    }
    mPropArc = prop_info(mPropKind).arc;
    if (mPropModel == nullptr) {
        const cPhs_Step step = static_cast<cPhs_Step>(dComIfG_resLoad(&mPropPhase, mPropArc));
        mPropRequested = true;
        if (step == cPhs_ERROR_e) {
            mods::log::warn("puppet: could not load {}", mPropArc);
            mPropFailed = true;
            return false;
        }
        if (step != cPhs_COMPLEATE_e) return false;
        const PropInfo& info = prop_info(mPropKind);
        auto* data = static_cast<J3DModelData*>(info.bmd != nullptr
                ? dComIfG_getObjectRes(mPropArc, info.bmd)
                : dComIfG_getObjectRes(mPropArc, info.bmdIndex));
        if (data == nullptr) {
            if (info.bmd != nullptr) mods::log::warn("puppet: {} has no {}", mPropArc, info.bmd);
            else mods::log::warn("puppet: {} has no model #{}", mPropArc, info.bmdIndex);
            mPropFailed = true;
            return false;
        }
        mPropHeap = JKRExpHeap::create(kPropHeapSize, linkkit::heap(), false);
        if (mPropHeap == nullptr) {
            mPropFailed = true;
            return false;
        }
        HeapScope scope(mPropHeap);
        mPropModel = mDoExt_J3DModel__create(data, 0x80000, 0x11000084);
        const auto bck = [&](const char* name) -> mDoExt_bckAnm* {
            if (name == nullptr) return nullptr;
            auto* res = static_cast<J3DAnmTransform*>(dComIfG_getObjectRes(mPropArc, name));
            if (res == nullptr) return nullptr;
            auto* anm = JKR_NEW mDoExt_bckAnm();
            if (anm == nullptr || !anm->init(res, TRUE, J3DFrameCtrl::EMode_LOOP, 1.0f, 0, -1, false)) return nullptr;
            return anm;
        };
        mPropIdle = bck(info.idleBck);
        mPropMove = bck(info.moveBck);
        if (mPropModel == nullptr) {
            mods::log::warn("puppet: could not create {} model", info.name);
            mPropFailed = true;
            return false;
        }
    }

    const PropInfo& info = prop_info(mPropKind);
    mDoMtx_stack_c::transS(current.pos.x, current.pos.y, current.pos.z);
    mDoMtx_stack_c::YrotM(shape_angle.y);
    mDoMtx_stack_c::scaleM(info.scale, info.scale, info.scale);
    mPropModel->setBaseTRMtx(mDoMtx_stack_c::get());
    fopAcM_SetMtx(this, mPropModel->getBaseTRMtx());

    mDoExt_bckAnm* anm = moving && mPropMove != nullptr ? mPropMove : mPropIdle;
    if (anm != nullptr) anm->play();
    mPropMoving = moving;
    return true;
}

void Puppet::releaseProp() {
    if (mPropHeap != nullptr) {
        mPropHeap->destroy();
        mPropHeap = nullptr;
    }
    mPropModel = nullptr;
    mPropIdle = mPropMove = nullptr;
    if (mPropRequested && mPropArc != nullptr) dComIfG_resDelete(&mPropPhase, mPropArc);
    mPropRequested = false;
    mPropFailed = false;
    mPropKind = -1;
    mPropArc = nullptr;
}

void Puppet::updateGround() {
    cXyz probe = current.pos;
    probe.y += 60.0f;
    mGndChk.SetPos(&probe);
    mGroundY = dComIfG_Bgsp().GroundCross(&mGndChk);
    if (mGroundY != -G_CM3D_F_INF) {
        tevStr.YukaCol = dComIfG_Bgsp().GetPolyColor(mGndChk);
        tevStr.room_no = dComIfG_Bgsp().GetRoomId(mGndChk);
    }
}

void Puppet::armHitbox(const match::Player& p, bool disguised, int kind) {
    const match::Match& m = match::get();
    mCylArmed = m.phase == Phase::Seek && match::my_role() == Role::Hunter && p.role == Role::Hider &&
                !p.found;
    if (!mCylArmed) return;
    const float r = disguised ? prop_info(kind).radius : kLinkRadius;
    const float h = disguised ? prop_info(kind).height : kLinkHeight;
    // Prop hiders participate in the game's normal object-correction collision on the hunter's
    // client. Link is pushed around the cylinder just like an NPC, while the network-owned prop
    // remains authoritative and cannot be shoved out of place. Human hiders stay non-solid.
    mCyl.SetCoSPrm(disguised && prop_info(kind).solid ? 0x79 : 0);
    mCyl.SetC(current.pos);
    mCyl.SetR(r);
    mCyl.SetH(h);
    dComIfG_Ccsp()->Set(&mCyl);
}

void Puppet::publish(bool visible, float height, bool tracked) {
    if (mDecoy) return;
    Slot& s = slot();
    s.visible = visible;
    s.tracked = tracked;
    s.feet = current.pos;
    s.height = height;
}

int Puppet::execute() {
    // A sword hit registered by last frame's collision pass.
    if (mCylArmed && mCyl.ChkTgHit()) {
        if (mCyl.GetTgHitAc() == dComIfGp_getPlayer(0)) {
            local::note_hit();
            match::report_hit(static_cast<uint8_t>(mSlot));
        }
        mCyl.ClrTgHit();
    }
    // Apply/clear last frame's object-correction result before registering the cylinder again.
    mStts.Move();
    mCyl.ClrCoHit();
    mCylArmed = false;

    if (mDecoy) {
        mDisguised = true;
        if (!mGrounded) {
            updateGround();
            if (mGroundY != -G_CM3D_F_INF) {
                current.pos.y = mGroundY;
                old.pos = current.pos;
                mGrounded = true;
            }
        }
        mVisible = mGrounded && updateProp(mFixedProp, false);
        if (mVisible && prop_info(mPropKind).solid) {
            // Decorative props use correction collision only: a wrong sword guess remains a miss,
            // but Link can no longer walk through a crate, pot, rock or piece of furniture.
            const PropInfo& info = prop_info(mPropKind);
            mCyl.SetCoSPrm(0x79);
            mCyl.SetC(current.pos);
            mCyl.SetR(std::clamp(info.radius * 0.85f, 25.0f, 110.0f));
            mCyl.SetH(info.height);
            dComIfG_Ccsp()->Set(&mCyl);
        }
        return 1;
    }

    if (mLocal) {
        fopAc_ac_c* player = dComIfGp_getPlayer(0);
        if (player == nullptr) {
            mVisible = false;
            publish(false, 0.0f, false);
            return 1;
        }
        const bool moving = (player->current.pos - current.pos).abs() > 1.0f;
        current.pos = player->current.pos;
        shape_angle.y = player->shape_angle.y;
        updateGround();
        // Keep the selected prop loaded while we are in the room. When Hide begins, the prop can
        // replace Link in the same frame instead of leaving an invisible archive-loading gap.
        const bool ready = updateProp(local::prop(), local::disguised() && moving);
        mVisible = local::disguised() && ready;
        publish(mVisible, mVisible ? prop_info(mPropKind).height : 0.0f, mVisible);
        return 1;
    }

    const match::Player& p = match::player(mSlot);
    const PlayerState& s = p.state;
    mVisible = p.present && p.hasState && same_stage_as_me(s) && now_ms() - p.stateAt < kStaleMs;
    if (!mVisible) {
        publish(false, 0.0f, false);
        return 1;
    }

    const cXyz target(s.x, s.y, s.z);
    const cXyz before = current.pos;
    if (!mHaveTarget || (target - current.pos).abs() > kSnapDistance) {
        current.pos = target;
        shape_angle.y = s.yaw;
        mHaveTarget = true;
    } else {
        current.pos += (target - current.pos) * 0.35f;
        cLib_addCalcAngleS2(&shape_angle.y, s.yaw, 3, 0x2000);
    }
    old.pos = before;
    updateGround();

    mDisguised = (s.flags & STATE_DISGUISED) != 0;
    const int kind = s.prop < prop_count() ? s.prop : 0;
    const bool moving = (current.pos - before).abs() > 1.0f;
    if (mDisguised) {
        mVisible = updateProp(kind, moving);
    } else {
        if (mPropModel != nullptr || mPropRequested) releaseProp();
        const uint8_t color = p.color;
        if (mBody == nullptr || color != mColor) buildLink(color);
        // Even if another mod lets the remote player enter wolf form, keep the clear human hunter
        // silhouette rather than making that player disappear from this mode.
        mVisible = mBody != nullptr && poseLink(s, moving);
    }
    const int shownKind = mDisguised && mPropKind >= 0 ? mPropKind : kind;
    armHitbox(p, mDisguised, shownKind);
    publish(mVisible, mDisguised ? prop_info(shownKind).height : kLinkHeight, true);
    return 1;
}

int Puppet::draw() {
    if (!mVisible) return 1;
    g_env_light.settingTevStruct(0, &current.pos, &tevStr);
    float shadow = kLinkRadius;
    bool modelShadow = false;
    if (mDisguised || mLocal) {
        if (mPropModel == nullptr) return 1;
        g_env_light.setLightTevColorType_MAJI(mPropModel, &tevStr);
        // Carryable objects use modelUpdateDL(), not a separate calc()/entryDL() pair. Keep the
        // shared resource's actor callbacks out of the whole update so our model cannot run a
        // real pot/crate actor's joint callback with this Puppet as its owner.
        J3DModelData* data = mPropModel->getModelData();
        JointGuard guard(data);
        mDoExt_bckAnm* anm = mPropMoving && mPropMove != nullptr ? mPropMove : mPropIdle;
        if (anm != nullptr) anm->entry(data);
        mDoExt_modelUpdateDL(mPropModel);
        const PropInfo& info = prop_info(mPropKind);
        if (info.shadowScale <= 0.0f) {
            shadow = 0.0f;
        } else if (const float nativeSize = native_simple_shadow_size(mPropKind); nativeSize > 0.0f) {
            shadow = nativeSize;
        } else {
            shadow = 1.0f;  // a positive sentinel; projected shadows use their profile below
            modelShadow = true;
        }
    } else {
        // modelEntryDL alone does not submit custom actors on Dusklight's interpolated PC frames.
        // Updating the body and rigid attachments here keeps a complete Link visible every render
        // frame. Hands retain Link's normal entry path because their two joint matrices are placed
        // manually after calc().
        if (mPoseAnim != nullptr) mPoseAnim->setFrame(mPoseFrame);
        J3DModel* updated[] = {mBody, mFace, mHead, mSheath, mSword, mShield};
        for (J3DModel* model : updated) {
            if (model == nullptr) continue;
            g_env_light.setLightTevColorType_MAJI(model, &tevStr);
            mDoExt_modelUpdateDL(model);
        }
        if (mHands != nullptr) {
            g_env_light.setLightTevColorType_MAJI(mHands, &tevStr);
            mDoExt_modelEntryDL(mHands);
        }
    }
    if (mGroundY != -G_CM3D_F_INF && shadow > 0.0f) {
        if (modelShadow && mPropModel != nullptr) {
            const PropInfo& info = prop_info(mPropKind);
            const ModelShadowProfile profile = native_model_shadow(mPropKind, info);
            cXyz shadowPos = current.pos;
            shadowPos.y += profile.yOffset;
            mShadowKey = dComIfGd_setShadow(mShadowKey, 1, mPropModel, &shadowPos, profile.depth,
                profile.footprint, current.pos.y, mGroundY, mGndChk, &tevStr, shape_angle.y, 1.0f,
                dDlst_shadowControl_c::getSimpleTex());
        } else {
            dComIfGd_setSimpleShadow(&current.pos, mGroundY, shadow, mGndChk, shape_angle.y,
                1.0f, dDlst_shadowControl_c::getSimpleTex());
        }
    }
    return 1;
}

int Puppet::destroy() {
    releaseProp();
    if (mHeap != nullptr) {
        mHeap->destroy();
        mHeap = nullptr;
    }
    if (!mDecoy) publish(false, 0.0f, false);
    this->~Puppet();
    return 1;
}

int puppet_create(void* self) {
    return static_cast<Puppet*>(self)->create();
}
int puppet_delete(void* self) {
    return static_cast<Puppet*>(self)->destroy();
}
int puppet_execute(void* self) {
    return static_cast<Puppet*>(self)->execute();
}
int puppet_draw(void* self) {
    return static_cast<Puppet*>(self)->draw();
}
int puppet_is_delete(void*) {
    return 1;
}

const ActorProfileDesc kProfile = {
    .name = "HSPupt",
    .priority_group = 7,
    .process_size = sizeof(Puppet),
    .draw_priority = fpcDwPi_OBJ_LBOX_e,
    .status = fopAcStts_UNK_0x40000_e | fopAcStts_UNK_0x4000_e | fopAcStts_CULL_e,
    .group = fopAc_ACTOR_e,
    .cull_type = fopAc_CULLBOX_CUSTOM_e,
    .create_function = puppet_create,
    .delete_function = puppet_delete,
    .execute_function = puppet_execute,
    .is_delete_function = puppet_is_delete,
    .draw_function = puppet_draw,
};

void manage(Slot& slot, bool want, int id, bool local, const cXyz& at) {
    if (slot.actor != fpcM_ERROR_PROCESS_ID_e && fopAcM_SearchByID(slot.actor) == nullptr) {
        slot.actor = fpcM_ERROR_PROCESS_ID_e;
        slot.visible = false;
        slot.tracked = false;
    }
    if (!local && want && slot.actor != fpcM_ERROR_PROCESS_ID_e) {
        // Rebuilt in the new colour by the actor itself; nothing to do here.
    }
    if (want && slot.actor == fpcM_ERROR_PROCESS_ID_e) {
        ActorSpawnParams params{};
        params.parameters = static_cast<uint32_t>(id) | (local ? kLocalFlag : 0u);
        params.room_num = -1;
        params.position = {at.x, at.y, at.z};
        params.scale = {1.0f, 1.0f, 1.0f};
        ActorId created = 0;
        if (svc_actor->create_actor(mod_ctx, s_procName, &params, &created) == MOD_OK) {
            slot.actor = created;
        }
    } else if (!want && slot.actor != fpcM_ERROR_PROCESS_ID_e) {
        svc_actor->delete_actor(mod_ctx, slot.actor);
        slot.actor = fpcM_ERROR_PROCESS_ID_e;
        slot.visible = false;
        slot.tracked = false;
    }
}

void manage_decoy(Slot& slot, bool want, int number, int kind, const cXyz& at, s16 yaw) {
    if (slot.actor != fpcM_ERROR_PROCESS_ID_e && fopAcM_SearchByID(slot.actor) == nullptr) {
        slot.actor = fpcM_ERROR_PROCESS_ID_e;
    }
    if (want && slot.actor == fpcM_ERROR_PROCESS_ID_e) {
        ActorSpawnParams params{};
        params.parameters = static_cast<uint32_t>(number + 1) | kDecoyFlag |
                            (static_cast<uint32_t>(kind) << kPropShift);
        params.room_num = -1;
        params.position = {at.x, at.y, at.z};
        params.angle = {0, yaw, 0};
        params.scale = {1.0f, 1.0f, 1.0f};
        ActorId created = 0;
        if (svc_actor->create_actor(mod_ctx, s_procName, &params, &created) == MOD_OK) {
            slot.actor = created;
        }
    } else if (!want && slot.actor != fpcM_ERROR_PROCESS_ID_e) {
        svc_actor->delete_actor(mod_ctx, slot.actor);
        slot.actor = fpcM_ERROR_PROCESS_ID_e;
    }
}

}  // namespace

bool register_actor() {
    if (s_registered) return true;
    if (svc_actor->register_actor(mod_ctx, &kProfile, &s_procName, &s_handle) != MOD_OK) {
        mods::log::error("could not register the player puppet actor");
        return false;
    }
    s_registered = true;
    return true;
}

bool unregister_actor() {
    if (!s_registered) return true;
    if (svc_actor->unregister_actor(mod_ctx, s_handle) != MOD_OK) return false;
    s_registered = false;
    for (Slot& s : s_slots) s = Slot{};
    s_localProp = Slot{};
    for (Slot& s : s_decoys) s = Slot{};
    s_decoyRound = 0;
    s_decoyMap = -1;
    return true;
}

void update() {
    if (!s_registered) return;
    const bool online = net::status() == net::Status::Online;
    const bool world = local::in_world() && !dComIfGp_isEnableNextStage();
    const uint64_t now = now_ms();
    if (!world) s_worldSince = 0;
    else if (s_worldSince == 0) s_worldSince = now;
    // Give a freshly loaded stage a moment before adding actors to it.
    const bool settled = world && now - s_worldSince > 500;

    const uint8_t me = net::self_id();
    for (int id = 1; id <= kMaxPlayers; ++id) {
        const match::Player& p = match::player(id);
        const bool want = online && settled && id != me && p.present && p.hasState &&
                          same_stage_as_me(p.state) && now - p.stateAt < kStaleMs;
        manage(s_slots[id], want, id, false, cXyz(p.state.x, p.state.y, p.state.z));
    }
    fopAc_ac_c* player = dComIfGp_getPlayer(0);
    const bool wantLocal = online && settled && player != nullptr && !net::room_code().empty();
    manage(s_localProp, wantLocal, me != 0 ? me : 1, true,
        player != nullptr ? player->current.pos : cXyz(0.0f, 0.0f, 0.0f));

    const match::Match& m = match::get();
    const int mapIndex = m.map;
    const bool propRound = online && settled && match::in_round() &&
                           m.settings.mode == Mode::PropHunt && mapIndex >= 0 &&
                           mapIndex < map_count() &&
                           std::strncmp(local::stage(), map_info(mapIndex).stage, 8) == 0;
    if (!propRound) {
        for (int i = 0; i < kDecoyCount; ++i) {
            manage_decoy(s_decoys[i], false, i, 0, cXyz(0.0f, 0.0f, 0.0f), 0);
        }
        s_decoyRound = 0;
        s_decoyMap = -1;
        return;
    }
    if (s_decoyRound != m.round || s_decoyMap != mapIndex) {
        // Remove the old actors first; the next update creates this map's authored layout after
        // deletion has been requested. Keeping layouts stable is what makes the scenery believable.
        for (int i = 0; i < kDecoyCount; ++i) {
            manage_decoy(s_decoys[i], false, i, 0, cXyz(0.0f, 0.0f, 0.0f), 0);
        }
        s_decoyRound = m.round;
        s_decoyMap = mapIndex;
        return;
    }
    for (int i = 0; i < kDecoyCount; ++i) {
        const CoverPoint point = cover_point(mapIndex, i);
        // Start above an authored point. The actor grounds itself before becoming visible, so a
        // changed stage layout or missing floor never leaves a floating prop.
        const cXyz at(point.x, point.y + 1200.0f, point.z);
        manage_decoy(s_decoys[i], propRound, i, point.prop, at, point.yaw);
    }
}

bool local_prop_visible() {
    return s_localProp.visible;
}

bool anchor(int id, cXyz& feet, float& height) {
    if (id < 1 || id > kMaxPlayers || !s_slots[id].tracked) return false;
    feet = s_slots[id].feet;
    height = s_slots[id].height;
    return true;
}

}  // namespace hs::puppet
