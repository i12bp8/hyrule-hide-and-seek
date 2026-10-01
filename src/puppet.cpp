#include "puppet.hpp"

#include "common.hpp"
#include "interpolation.hpp"
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
#include "d/d_bg_w.h"
#include "d/d_cc_d.h"
#include "d/d_com_inf_game.h"
#include "d/d_kankyo.h"
#include "f_op/f_op_actor_mng.h"
#include "m_Do/m_Do_ext.h"
#include "m_Do/m_Do_mtx.h"

#include <cmath>
#include <cstring>
#include <vector>
#ifdef HS_STOCK_RENDER_TEST
#include <cstdio>
#endif

// actor.h has no extern declaration of its own; mod.cpp imports it.
extern const ActorService* svc_actor;

namespace hs::puppet {

namespace {

constexpr u32 kLocalFlag = 0x100;
constexpr u32 kDecoyFlag = 0x200;
constexpr u32 kRupeeFlag = 0x400;
constexpr u32 kPropShift = 16;
constexpr u32 kDecoyIdShift = 24;
constexpr u32 kPuppetHeapSize = 640 * 1024;
constexpr u32 kPropHeapSize = 128 * 1024;
constexpr uint64_t kStaleMs = 4000;
constexpr float kSnapDistance = 600.0f;
constexpr float kLinkRadius = 40.0f;
constexpr float kLinkHeight = 150.0f;
// After our sword hits a decoy it stays hidden this long while the host confirms. A confirmed hit
// deletes it; a rejected one (hunter too far away by the host's account) brings it back.
constexpr uint64_t kDecoyHitHideMs = 1500;

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
struct DecoySlot {
    Slot slot;
    uint8_t id = 0;
    uint8_t kind = 0;
};
DecoySlot s_decoys[match::kMaxActiveDecoys];
struct RupeeSlot { Slot slot; uint16_t id = 0; };
RupeeSlot s_rupees[match::kMaxRupees];
uint64_t s_worldSince = 0;

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

// The push cylinder of a solid disguise, set like the native pots' and rocks' (Co 0x79): it
// corrects Link's position but takes no sword hits (kCylSrc does that) and deals no damage.
const dCcD_SrcCyl kSolidSrc = {
    {
        {0, {{0, 0, 0}, {0, 0}, 0x79}},
        {dCcD_SE_NONE, 0, 0, 0, {0}},
        {dCcD_SE_NONE, 0, 0, 0, {0}},
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

class Puppet : public fopAc_ac_c {
public:
    Puppet() : mLowerCalc(3, mLowerPack), mUpperCalc(3, mUpperPack) {}

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
    void armDecoyHitbox(int kind);
    void armSolid(int kind);
    void releaseSolid();
    void publish(bool visible, float height, bool tracked = false);
    Slot& slot() {
        return mRupee ? s_rupees[mSlot - 1].slot : mDecoy ? s_decoys[mSlot - 1].slot : (mLocal ? s_localProp : s_slots[mSlot]);
    }

    int mSlot = 0;
    bool mLocal = false;
    bool mDecoy = false;
    bool mRupee = false;
    uint8_t mDecoyId = 0;
    uint8_t mFixedProp = 0;
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
    mDoExt_AnmRatioPack mLowerPack[3];
    mDoExt_AnmRatioPack mUpperPack[3];
    mDoExt_MtxCalcAnmBlendTbl mLowerCalc;
    mDoExt_MtxCalcAnmBlendTbl mUpperCalc;
    // Shallow animation copies share immutable key data, but each slot owns its frame. Otherwise
    // upper/lower blends and players using the same cached BCK overwrite one another's time.
    J3DAnmTransformKey mLowerPose[3];
    J3DAnmTransformKey mUpperPose[3];
    J3DMtxCalcNoAnm<J3DMtxCalcCalcTransformMaya, J3DMtxCalcJ3DSysInitMaya> mBindCalc;
    J3DAnmTransform* mPoseAnim = nullptr;
    float mPoseFrame = 0.0f;

    // Prop
    int mPropKind = -1;
    bool mPropRequested = false;
    bool mPropFailed = false;
    const char* mPropArc = nullptr;
    const char* mAnimationArc = nullptr;
    bool mAnimationRequested = false;
    request_of_phase_process_class mAnimationPhase;
    request_of_phase_process_class mPropPhase;
    JKRExpHeap* mPropHeap = nullptr;
    J3DModel* mPropModel = nullptr;
    mDoExt_bckAnm* mPropIdle = nullptr;
    mDoExt_bckAnm* mPropMove = nullptr;
    mDoExt_brkAnm* mRupeeColor = nullptr;
    bool mPropMoving = false;
    int mWantedProp = -1;

    // Ground, shadow and the hunters' target
    dBgS_ObjGndChk mGndChk;
    f32 mGroundY = -G_CM3D_F_INF;
    dCcD_Stts mStts;
    dCcD_Cyl mCyl;
    bool mCylArmed = false;

    // Solid like the real object: a push cylinder, or the archive's collision mesh.
    dCcD_Cyl mSolidCyl;
    dBgW* mBgW = nullptr;
    Mtx mBgMtx;
    bool mBgRegistered = false;
    bool mBgFailed = false;
    uint64_t mHiddenUntil = 0;
};

int Puppet::create() {
    fopAcM_ct(this, Puppet);
    // ActorService creates in the current layer, which can still be the root when a room joins.
    // Put world actors in the play scene like native actors: a root-layer puppet is also drawn
    // by the presentation root iterator after the play scene's actor queue already drew it.
    fopAcM_setStageLayer(&LEAFDRAW_BASE(this));
    const u32 prm = fopAcM_GetParam(this);
    mSlot = static_cast<int>(prm & 0xFF);
    mLocal = (prm & kLocalFlag) != 0;
    mDecoy = (prm & kDecoyFlag) != 0;
    mRupee = (prm & kRupeeFlag) != 0;
    mFixedProp = static_cast<uint8_t>(prm >> kPropShift);
    mDecoyId = static_cast<uint8_t>(prm >> kDecoyIdShift);
    const int maxSlot = mRupee ? match::kMaxRupees : mDecoy ? match::kMaxActiveDecoys : kMaxPlayers;
    if (mSlot < 1 || mSlot > maxSlot || (mDecoy && mDecoyId == 0) || linkkit::heap() == nullptr) {
        return cPhs_ERROR_e;
    }
    // Only a remote human Link needs the large private model heap. The local disguise only needs
    // its small prop-model heap.
    if (!mLocal && !mDecoy && !mRupee) {
        mHeap = JKRExpHeap::create(kPuppetHeapSize, linkkit::heap(), false);
        if (mHeap == nullptr) {
            mods::log::warn("puppet {}: out of memory", mSlot);
            return cPhs_ERROR_e;
        }
    }
    mStts.Init(0xFF, 0xFF, this);
    mCyl.Set(kCylSrc);
    mCyl.SetStts(&mStts);
    mSolidCyl.Set(kSolidSrc);
    mSolidCyl.SetStts(&mStts);
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
    for (auto* packs : {mLowerPack, mUpperPack}) {
        for (int i = 0; i < 3; ++i) {
            packs[i].setAnmTransform(nullptr);
            packs[i].setRatio(0.0f);
        }
    }
    mPoseAnim = nullptr;
    mPoseFrame = 0.0f;
    mColor = 0xFF;
}

bool Puppet::poseLink(const PlayerState& s, bool moving) {
    // A complete fallback also animates bots and peers whose current animation is from a demo
    // archive. Real peers supply the native three-slot upper/lower blends, including sword swings.
    J3DAnmTransform* wanted = linkkit::anim(moving ? linkkit::kWalkAnim : linkkit::kIdleAnim);
    if (wanted == nullptr && mPoseAnim == nullptr) wanted = linkkit::anim(linkkit::kIdleAnim);
    if (wanted != nullptr && wanted != mPoseAnim) {
        mPoseAnim = wanted;
        mPoseFrame = 0.0f;
    }
    if (mPoseAnim != nullptr) {
        const float maxFrame = static_cast<float>(mPoseAnim->getFrameMax());
        mPoseFrame += moving ? 1.1f : 0.45f;
        if (maxFrame > 0.0f) mPoseFrame = std::fmod(mPoseFrame, maxFrame);
        else mPoseFrame = 0.0f;
        mPoseAnim->setFrame(mPoseFrame);
    }

    const float elapsedFrames = std::min<uint64_t>(now_ms() - match::player(mSlot).stateAt, 100) * 0.03f;
    const auto prepare = [&](const AnimSlot* slots, mDoExt_AnmRatioPack* packs,
                             J3DAnmTransformKey* poses) {
        for (int i = 0; i < 3; ++i) {
            J3DAnmTransform* source = slots[i].ratio != 0 && std::isfinite(slots[i].frame)
                                         ? linkkit::anim(slots[i].idx) : nullptr;
            const bool network = source != nullptr;
            if (i == 0 && source == nullptr) source = mPoseAnim;
            packs[i].setRatio(network ? slots[i].ratio / 255.0f : (i == 0 ? 1.0f : 0.0f));
            packs[i].setAnmTransform(nullptr);
            if (source == nullptr) continue;
            poses[i] = *static_cast<J3DAnmTransformKey*>(source);
            float frame = network ? std::max(0.0f, slots[i].frame) + elapsedFrames : mPoseFrame;
            const float end = static_cast<float>(source->getFrameMax());
            if (end > 0.0f) {
                frame = source->getAttribute() == J3DFrameCtrl::EMode_LOOP
                            ? std::fmod(frame, end) : std::min(frame, end - 0.001f);
            } else frame = 0.0f;
            poses[i].setFrame(frame);
            packs[i].setAnmTransform(&poses[i]);
        }
    };
    prepare(s.under, mLowerPack, mLowerPose);
    prepare(s.upper, mUpperPack, mUpperPose);

    mDoMtx_stack_c::transS(current.pos.x, current.pos.y, current.pos.z);
    mDoMtx_stack_c::YrotM(shape_angle.y);
    mBody->setBaseTRMtx(mDoMtx_stack_c::get());
    // The actor system uses this matrix to transform our custom culling box. Without it, the
    // replacement can be culled even while standing directly in front of the camera.
    fopAcM_SetMtx(this, mBody->getBaseTRMtx());

    // Native human Link: lower body at root and legs, upper body at joint 1. Model data is shared
    // by a tunic colour, so restore calculators after this actor has calculated its own matrices.
    J3DModelData* data = mBody->getModelData();
    {
        JointGuard guard(data);
        J3DMtxCalc* lower = mLowerPack[0].getAnmTransform() != nullptr
                               ? static_cast<J3DMtxCalc*>(&mLowerCalc) : &mBindCalc;
        J3DMtxCalc* upper = mUpperPack[0].getAnmTransform() != nullptr
                               ? static_cast<J3DMtxCalc*>(&mUpperCalc) : lower;
        data->getJointNodePointer(0)->setMtxCalc(lower);
        if (data->getJointNum() > 1) data->getJointNodePointer(1)->setMtxCalc(upper);
        if (data->getJointNum() > kJointLegs) data->getJointNodePointer(kJointLegs)->setMtxCalc(lower);
        mBody->calc();
    }

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
    // Disabled legacy IDs can still arrive from an older peer or saved local state. Keep packet
    // numbering stable but display the known-good pot instead of a broken composite/world model.
    if (!prop_on_map(kind, -1) && !(mRupee && kind == rupee_prop())) kind = 0;
    if (kind != mWantedProp) {
        releaseProp();
        mWantedProp = kind;
        mPropKind = kind;
    }
    if (mPropKind < 0) mPropKind = kind;
    // Never make a player disappear because one optional model failed. Retry the known
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
        mAnimationArc = info.animationArc;
        if (mAnimationArc != nullptr) {
            mAnimationRequested = true;
            const auto animationStep = dComIfG_resLoad(&mAnimationPhase, mAnimationArc);
            if (animationStep == cPhs_ERROR_e) { mPropFailed = true; return false; }
            if (animationStep != cPhs_COMPLEATE_e) return false;
        }
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
            auto* res = static_cast<J3DAnmTransform*>(dComIfG_getObjectRes(mAnimationArc != nullptr ? mAnimationArc : mPropArc, name));
            if (res == nullptr) return nullptr;
            auto* anm = JKR_NEW mDoExt_bckAnm();
            if (anm == nullptr || !anm->init(res, TRUE, J3DFrameCtrl::EMode_LOOP, 1.0f, 0, -1, false)) return nullptr;
            return anm;
        };
        mPropIdle = bck(info.idleBck);
        mPropMove = bck(info.moveBck);
        if (mRupee) {
            auto* color = static_cast<J3DAnmTevRegKey*>(dComIfG_getObjectRes(mPropArc, 7));
            mRupeeColor = color != nullptr ? JKR_NEW mDoExt_brkAnm() : nullptr;
            if (mRupeeColor != nullptr && !mRupeeColor->init(data, color, FALSE, J3DFrameCtrl::EMode_LOOP, 0.0f, 0, -1)) mRupeeColor = nullptr;
        }
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
    mDoMtx_stack_c::transM(info.offsetX, info.offsetY, info.offsetZ);
    mPropModel->setBaseTRMtx(mDoMtx_stack_c::get());
    fopAcM_SetMtx(this, mPropModel->getBaseTRMtx());

    mDoExt_bckAnm* anm = moving && mPropMove != nullptr ? mPropMove : mPropIdle;
    if (anm != nullptr) anm->play();
    mPropMoving = moving;
    // Calculate on the simulation tick, while our animation and callback overrides are active.
    // Drawing uses modelEntryDL: presentation frames reuse the simulation's packets instead of
    // registering them again in the still-live draw buffers.
    {
        JointGuard guard(mPropModel->getModelData());
        if (anm != nullptr) anm->entry(mPropModel->getModelData());
        mPropModel->calc();
    }
    return true;
}

void Puppet::releaseProp() {
    // The collision mesh lives in the prop heap: unregister it before the heap goes.
    releaseSolid();
    mBgW = nullptr;
    mBgFailed = false;
    if (mPropHeap != nullptr) {
        mPropHeap->destroy();
        mPropHeap = nullptr;
    }
    mPropModel = nullptr;
    mPropIdle = mPropMove = nullptr;
    mRupeeColor = nullptr;
    if (mAnimationRequested && mAnimationArc != nullptr) dComIfG_resDelete(&mAnimationPhase, mAnimationArc);
    mAnimationRequested = false;
    mAnimationArc = nullptr;
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
                !p.found && match::my_hunter_life() != 0;
    if (!mCylArmed) return;
    const float r = disguised ? prop_info(kind).radius : kLinkRadius;
    const float h = disguised ? prop_info(kind).height : kLinkHeight;
    // This cylinder is a sword target only. Network-owned correction cylinders push Link at stale
    // positions and behave badly for thin/offset models, so disguises never register as solids.
    mCyl.SetCoSPrm(0);
    mCyl.SetC(current.pos);
    mCyl.SetR(r);
    mCyl.SetH(h);
    dComIfG_Ccsp()->Set(&mCyl);
}

void Puppet::armDecoyHitbox(int kind) {
    const match::Match& m = match::get();
    mCylArmed = m.phase == Phase::Seek && match::my_role() == Role::Hunter && match::my_hunter_life() != 0;
    if (!mCylArmed) return;
    mCyl.SetCoSPrm(0);
    mCyl.SetC(current.pos);
    mCyl.SetR(prop_info(kind).radius);
    mCyl.SetH(prop_info(kind).height);
    dComIfG_Ccsp()->Set(&mCyl);
}

void Puppet::armSolid(int kind) {
    // Our own disguise never blocks us; everyone else sees it through a puppet of their own.
    if (mLocal || mPropModel == nullptr) {
        releaseSolid();
        return;
    }
    const PropInfo& info = prop_info(kind);
    const PropSolid& solid = prop_solid(kind);
    // The model's origin, after the catalogue's offset corrections, is where the native actor
    // would stand. Collision is placed around it rather than around the network position.
    const MtxP model = mPropModel->getBaseTRMtx();
    if (solid.kind == Solid::Cylinder) {
        releaseSolid();
        mSolidCyl.SetC(cXyz(model[0][3], current.pos.y, model[2][3]));
        mSolidCyl.SetR(solid.radius > 0.0f ? solid.radius : info.radius);
        mSolidCyl.SetH(info.height);
        dComIfG_Ccsp()->Set(&mSolidCyl);
        return;
    }
    if (solid.kind != Solid::Background || mBgFailed) {
        releaseSolid();
        return;
    }

    mDoMtx_stack_c::copy(model);
    mDoMtx_stack_c::scaleM(solid.bgScaleX, solid.bgScaleY, solid.bgScaleZ);
    MTXCopy(mDoMtx_stack_c::get(), mBgMtx);
    if (mBgW == nullptr) {
        auto* dzb = static_cast<cBgD_t*>(solid.dzb != nullptr
                ? dComIfG_getObjectRes(mPropArc, solid.dzb)
                : dComIfG_getObjectRes(mPropArc, solid.dzbIndex));
        HeapScope scope(mPropHeap);
        mBgW = dzb != nullptr ? JKR_NEW dBgW() : nullptr;
        // Set() returns true on failure. A prop without its collision is still a usable disguise.
        if (mBgW == nullptr || mBgW->Set(dzb, dBgW::MOVE_BG_e, &mBgMtx)) {
            mods::log::warn("puppet: no collision for {}", info.name);
            mBgW = nullptr;
            mBgFailed = true;
            return;
        }
        mBgW->SetCrrFunc(dBgS_MoveBGProc_TypicalRotY);
    }
    if (mBgRegistered) {
        mBgW->Move();
        return;
    }
    // Never close a mesh around us: a decoy appears where its hider is standing, and a disguise
    // can load in on top of anyone. Become solid once our Link has stepped clear of it.
    if (fopAc_ac_c* player = dComIfGp_getPlayer(0)) {
        const cXyz& at = player->current.pos;
        const float dx = at.x - model[0][3];
        const float dz = at.z - model[2][3];
        const float reach = info.radius + kLinkRadius;
        const bool clear = dx * dx + dz * dz > reach * reach || at.y > current.pos.y + info.height ||
                           at.y + kLinkHeight < current.pos.y;
        if (!clear) return;
    }
    if (dComIfG_Bgsp().Regist(mBgW, this)) {
        mods::log::warn("puppet: could not register collision for {}", info.name);
        mBgFailed = true;
        return;
    }
    mBgRegistered = true;
    mBgW->Move();
}

void Puppet::releaseSolid() {
    if (mBgRegistered && mBgW != nullptr) dComIfG_Bgsp().Release(mBgW);
    mBgRegistered = false;
}

void Puppet::publish(bool visible, float height, bool tracked) {
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
            if (mDecoy) {
                // A decoy is deliberately a miss: it disappears, but does not suppress the
                // hunter's configured missed-swing penalty. Clear it from our path at once rather
                // than after the host's round trip; the host's snapshot then deletes it for all.
                match::report_decoy_hit(mDecoyId);
                mHiddenUntil = now_ms() + kDecoyHitHideMs;
            } else {
                local::note_hit();
                match::report_hit(static_cast<uint8_t>(mSlot));
            }
        }
        mCyl.ClrTgHit();
    }
    // Advance and clear last frame's collision state before registering the target again.
    mStts.Move();
    mCyl.ClrCoHit();
    mSolidCyl.ClrCoHit();
    mCylArmed = false;

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

    if (mRupee) {
        mDisguised = true;
        shape_angle.y = static_cast<int16_t>((now_ms() % 2400) * 65536 / 2400);
        mVisible = updateProp(rupee_prop(), false);
        publish(mVisible, 60.0f);
        return 1;
    }

    if (mDecoy) {
        mDisguised = true;
        old.pos = current.pos;
        updateGround();
        const int kind = prop_on_map(mFixedProp, -1) ? mFixedProp : 0;
        mVisible = updateProp(kind, false) && now_ms() >= mHiddenUntil;
        const int shownKind = mPropKind >= 0 ? mPropKind : kind;
        if (mVisible) {
            armDecoyHitbox(shownKind);
            armSolid(shownKind);
        } else {
            releaseSolid();
        }
        publish(mVisible, prop_info(shownKind).height, false);
        return 1;
    }

    const match::Player& p = match::player(mSlot);
    const PlayerState s = interpolate_state(p.previousState, p.previousStateAt, p.state, p.stateAt, now_ms());
    mVisible = p.present && p.hasState && same_stage_as_me(s) && now_ms() - p.stateAt < kStaleMs;
    if (!mVisible) {
        releaseSolid();
        publish(false, 0.0f, false);
        return 1;
    }

    mDisguised = (s.flags & STATE_DISGUISED) != 0;
    const cXyz target(s.x, s.y, s.z);
    const cXyz before = current.pos;
    if (!mHaveTarget || (target - current.pos).abs() > kSnapDistance) {
        current.pos = target;
        shape_angle.y = s.yaw;
        mHaveTarget = true;
    } else {
        current.pos = target;
        shape_angle.y = mDisguised ? s.propYaw : s.yaw;
    }
    old.pos = before;
    updateGround();

    mDisguised = (s.flags & STATE_DISGUISED) != 0;
    const int kind = prop_on_map(s.prop, -1) ? s.prop : 0;
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
    // A disguised hider blocks the way like the object they copy. Found hiders are out of the
    // round, and the undisguised hunters keep walking through each other as before.
    if (mDisguised && mVisible && !p.found) armSolid(shownKind);
    else releaseSolid();
    publish(mVisible, mDisguised ? prop_info(shownKind).height : kLinkHeight, true);
#ifdef HS_STOCK_RENDER_TEST
    if (mSlot == 2 && !mDisguised) {
        static uint64_t last = 0;
        if (now_ms() - last > 3000) {
            last = now_ms();
            mods::log::info("HUNTER_DEBUG: visible={} body={} condition={} pos=({},{},{})", mVisible,
                mBody != nullptr, static_cast<int>(actor_condition), current.pos.x, current.pos.y, current.pos.z);
            std::fflush(nullptr);
        }
    }
#endif
    return 1;
}

int Puppet::draw() {
    if (!mVisible) return 1;
    g_env_light.settingTevStruct(0, &current.pos, &tevStr);
    float shadow = kLinkRadius;
    if (mDisguised || mLocal) {
        if (mPropModel == nullptr) return 1;
        g_env_light.setLightTevColorType_MAJI(mPropModel, &tevStr);
        if (mRupeeColor != nullptr) mRupeeColor->entry(mPropModel->getModelData(), match::rupee_points() == 2 ? 4.0f : 0.0f);
        // Keep shared resource callbacks away from our model. Its matrices were calculated in
        // execute(); EntryDL refreshes materials on presentation frames without re-entering the
        // packets that Dusklight retained from the last simulation tick.
        J3DModelData* data = mPropModel->getModelData();
        JointGuard guard(data);
        mDoExt_bckAnm* anm = mPropMoving && mPropMove != nullptr ? mPropMove : mPropIdle;
        if (anm != nullptr) anm->entry(data);
        mDoExt_modelEntryDL(mPropModel);
        if (mRupeeColor != nullptr) mRupeeColor->remove(data);
        // Never use model-projected shadows here: those redraw the prop's geometry into the shadow
        // pass. A metadata-sized simple quad is cheap and cannot double a complex model's indices.
        shadow = prop_info(mPropKind).simpleShadowSize;
    } else {
#ifdef HS_STOCK_RENDER_TEST
        if (mSlot == 2) {
            static uint64_t last = 0;
            if (now_ms() - last > 3000) {
                last = now_ms();
                mods::log::info("HUNTER_DEBUG: draw body joints={} mtxY={} shapes={} rootY={}",
                    mBody->getModelData()->getJointNum(), mBody->getBaseTRMtx()[1][3],
                    mBody->getModelData()->getShapeNum(), mBody->getAnmMtx(0)[1][3]);
                std::fflush(nullptr);
            }
        }
#endif
        // poseLink() has already calculated the body and every attachment. Only enter their
        // packets on simulation frames: UpdateDL's locked-model path also enters on presentation
        // frames, creating cycles in the retained material list and an unbounded geometry stream.
        if (mPoseAnim != nullptr) mPoseAnim->setFrame(mPoseFrame);
        J3DModel* updated[] = {mBody, mFace, mHead, mSheath, mSword, mShield};
        for (J3DModel* model : updated) {
            if (model == nullptr) continue;
            g_env_light.setLightTevColorType_MAJI(model, &tevStr);
            mDoExt_modelEntryDL(model);
        }
        if (mHands != nullptr) {
            g_env_light.setLightTevColorType_MAJI(mHands, &tevStr);
            mDoExt_modelEntryDL(mHands);
        }
    }
    if (mGroundY != -G_CM3D_F_INF && shadow > 0.0f) {
        dComIfGd_setSimpleShadow(&current.pos, mGroundY, shadow, mGndChk, shape_angle.y, 1.0f,
            dDlst_shadowControl_c::getSimpleTex());
    }
    return 1;
}

int Puppet::destroy() {
    releaseProp();
    if (mHeap != nullptr) {
        mHeap->destroy();
        mHeap = nullptr;
    }
    publish(false, 0.0f, false);
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

void manage(Slot& slot, bool want, int id, bool local, const cXyz& at, bool decoy = false,
    uint8_t kind = 0, uint8_t decoyId = 0, int16_t yaw = 0, bool rupee = false) {
    if (slot.actor != fpcM_ERROR_PROCESS_ID_e && fopAcM_SearchByID(slot.actor) == nullptr) {
        slot.actor = fpcM_ERROR_PROCESS_ID_e;
        slot.visible = false;
        slot.tracked = false;
    }
    if (want && slot.actor == fpcM_ERROR_PROCESS_ID_e) {
        ActorSpawnParams params{};
        params.parameters = static_cast<uint32_t>(id) | (local ? kLocalFlag : 0u) |
                            (decoy ? kDecoyFlag : 0u) |
                            (rupee ? kRupeeFlag : 0u) |
                            (static_cast<uint32_t>(kind) << kPropShift) |
                            (static_cast<uint32_t>(decoyId) << kDecoyIdShift);
        params.room_num = -1;
        params.position = {at.x, at.y, at.z};
        params.angle.y = yaw;
        params.scale = {1.0f, 1.0f, 1.0f};
        ActorId created = 0;
        if (svc_actor->create_actor(mod_ctx, s_procName, &params, &created) == MOD_OK) {
            slot.actor = created;
        }
    } else if (!want && slot.actor != fpcM_ERROR_PROCESS_ID_e) {
        if (svc_actor->delete_actor(mod_ctx, slot.actor) == MOD_OK) {
            slot.actor = fpcM_ERROR_PROCESS_ID_e;
            slot.visible = false;
            slot.tracked = false;
        }
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
    for (DecoySlot& s : s_decoys) s = DecoySlot{};
    for (auto& s : s_rupees) s = {};
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
                          !p.eliminated &&
                          same_stage_as_me(p.state) && now - p.stateAt < kStaleMs;
        manage(s_slots[id], want, id, false, cXyz(p.state.x, p.state.y, p.state.z));
    }
    fopAc_ac_c* player = dComIfGp_getPlayer(0);
    const bool wantLocal = online && settled && player != nullptr && !net::room_code().empty();
    manage(s_localProp, wantLocal, me != 0 ? me : 1, true,
        player != nullptr ? player->current.pos : cXyz(0.0f, 0.0f, 0.0f));

    const match::Match& game = match::get();
    const bool decoyWorld = online && settled && game.settings.mode == Mode::PropHunt &&
                            (game.phase == Phase::Hide || game.phase == Phase::Seek) &&
                            std::strncmp(local::stage(), map_info(game.map).stage, 8) == 0;
    for (int i = 0; i < match::kMaxActiveDecoys; ++i) {
        DecoySlot& live = s_decoys[i];
        const bool exists = i < match::decoy_count();
        const match::Decoy& d = match::decoy(i);
        const bool want = decoyWorld && exists;
        const bool changed = want && (live.id != d.id || live.kind != d.prop);
        if (changed) {
            manage(live.slot, false, i + 1, false, cXyz(0.0f, 0.0f, 0.0f));
            if (live.slot.actor != fpcM_ERROR_PROCESS_ID_e) continue;
        }
        if (want) {
            live.id = d.id;
            live.kind = d.prop;
            manage(live.slot, true, i + 1, false, cXyz(d.x, d.y, d.z), true, d.prop, d.id,
                d.yaw);
        } else {
            manage(live.slot, false, i + 1, false, cXyz(0.0f, 0.0f, 0.0f));
            live.id = 0;
            live.kind = 0;
        }
    }
    const bool treasureWorld = online && settled && game.phase == Phase::Seek && game.settings.treasure &&
        std::strncmp(local::stage(), map_info(game.map).stage, 8) == 0;
    for (int i = 0; i < match::kMaxRupees; ++i) {
        auto& live = s_rupees[i];
        const auto& r = game.rupees[i];
        const bool want = treasureWorld && i < game.rupeeCount && r.expiresAt > now;
        if (!want || live.id != r.id) {
            manage(live.slot, false, i + 1, false, cXyz(0, 0, 0));
            if (live.slot.actor != fpcM_ERROR_PROCESS_ID_e) continue;
            live.id = 0;
        }
        if (want) {
            live.id = r.id;
            manage(live.slot, true, i + 1, false, cXyz(r.x, r.y + 35, r.z), false, 0, 0, 0, true);
        }
    }
}

bool local_prop_visible() {
    return s_localProp.visible;
}

bool anchor(int id, cXyz& feet, float& height) {
    if (id < 1 || id > kMaxPlayers || !s_slots[id].tracked || !s_slots[id].visible) return false;
    feet = s_slots[id].feet;
    height = s_slots[id].height;
    return true;
}

}  // namespace hs::puppet
