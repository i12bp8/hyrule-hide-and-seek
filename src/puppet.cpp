#include "puppet.hpp"

#include "common.hpp"
#include "linkkit.hpp"
#include "local.hpp"
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

#include <cstring>
#include <vector>

// actor.h has no extern declaration of its own; mod.cpp imports it.
extern const ActorService* svc_actor;

namespace hs::puppet {

namespace {

constexpr u32 kLocalFlag = 0x100;
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
    cXyz feet{0.0f, 0.0f, 0.0f};
    float height = 0.0f;
};
Slot s_slots[kSlots];
Slot s_localProp;
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
    Puppet() : mUnderCalc(3, mUnder), mUpperCalc(3, mUpper) {}

    int create();
    int execute();
    int draw();
    int destroy();

private:
    bool buildLink(uint8_t color);
    void freeLink();
    void poseLink(const PlayerState& s);
    bool updateProp(int kind, bool moving);
    void releaseProp();
    void updateGround();
    void armHitbox(const match::Player& p, bool disguised, int kind);
    void publish(bool visible, float height);
    Slot& slot() { return mLocal ? s_localProp : s_slots[mSlot]; }

    int mSlot = 0;
    bool mLocal = false;
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
    mDoExt_AnmRatioPack mUnder[3];
    mDoExt_AnmRatioPack mUpper[3];
    mDoExt_MtxCalcAnmBlendTbl mUnderCalc;
    mDoExt_MtxCalcAnmBlendTbl mUpperCalc;

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

    // Ground, shadow and the hunters' target
    dBgS_ObjGndChk mGndChk;
    f32 mGroundY = -G_CM3D_F_INF;
    dCcD_Stts mStts;
    dCcD_Cyl mCyl;
    bool mCylArmed = false;
};

int Puppet::create() {
    fopAcM_ct(this, Puppet);
    const u32 prm = fopAcM_GetParam(this);
    mSlot = static_cast<int>(prm & 0xFF);
    mLocal = (prm & kLocalFlag) != 0;
    if (mSlot < 1 || mSlot > kMaxPlayers || linkkit::heap() == nullptr) return cPhs_ERROR_e;
    mHeap = JKRExpHeap::create(kPuppetHeapSize, linkkit::heap(), false);
    if (mHeap == nullptr) {
        mods::log::warn("puppet {}: out of memory", mSlot);
        return cPhs_ERROR_e;
    }
    mStts.Init(0xFF, 0xFF, this);
    mCyl.Set(kCylSrc);
    mCyl.SetStts(&mStts);
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
    return true;
}

void Puppet::freeLink() {
    // Models were allocated in mHeap; freeing the heap's contents releases them all.
    if (mHeap != nullptr) mHeap->freeAll();
    mBody = mHead = mFace = mHands = mSword = mSheath = mShield = nullptr;
    mColor = 0xFF;
}

void Puppet::poseLink(const PlayerState& s) {
    J3DAnmTransform* idle = linkkit::anim(linkkit::kIdleAnim);
    const auto fill = [&](mDoExt_AnmRatioPack* packs, const AnimSlot* slots, J3DAnmTransform* base) {
        for (int i = 0; i < 3; ++i) {
            J3DAnmTransform* anm = linkkit::anim(slots[i].idx);
            if (i == 0 && anm == nullptr) anm = base;
            if (anm != nullptr) {
                const f32 maxFrame = static_cast<f32>(anm->getFrameMax());
                anm->setFrame(std::clamp(slots[i].frame, 0.0f, maxFrame > 0.0f ? maxFrame : 0.0f));
            }
            packs[i].setAnmTransform(anm);
            packs[i].setRatio(i == 0 ? 1.0f : slots[i].ratio / 255.0f);
        }
    };
    fill(mUnder, s.under, idle);
    fill(mUpper, s.upper, mUnder[0].getAnmTransform());
    if (mUnder[0].getAnmTransform() == nullptr) return;

    mDoMtx_stack_c::transS(current.pos.x, current.pos.y, current.pos.z);
    mDoMtx_stack_c::YrotM(shape_angle.y);
    mBody->setBaseTRMtx(mDoMtx_stack_c::get());
    // The actor system uses this matrix to transform our custom culling box. Without it, the
    // replacement can be culled even while standing directly in front of the camera.
    fopAcM_SetMtx(this, mBody->getBaseTRMtx());

    // Same split as daAlink_c::changeModelDataDirect: legs follow the lower body animation,
    // the torso the upper one. This model data is ours, so nothing else touches these.
    J3DModelData* data = mBody->getModelData();
    data->getJointNodePointer(0)->setMtxCalc(&mUnderCalc);
    data->getJointNodePointer(1)->setMtxCalc(&mUpperCalc);
    if (data->getJointNum() > kJointLegs) data->getJointNodePointer(kJointLegs)->setMtxCalc(&mUnderCalc);
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
}

bool Puppet::updateProp(int kind, bool moving) {
    if (kind != mPropKind) {
        releaseProp();
        mPropKind = kind;
        mPropArc = prop_info(kind).arc;
    }
    if (mPropFailed) return false;
    if (mPropModel == nullptr) {
        const cPhs_Step step = static_cast<cPhs_Step>(dComIfG_resLoad(&mPropPhase, mPropArc));
        mPropRequested = true;
        if (step == cPhs_ERROR_e) {
            mods::log::warn("puppet: could not load {}", mPropArc);
            mPropFailed = true;
            return false;
        }
        if (step != cPhs_COMPLEATE_e) return false;
        const PropInfo& info = prop_info(kind);
        auto* data = static_cast<J3DModelData*>(dComIfG_getObjectRes(mPropArc, info.bmd));
        if (data == nullptr) {
            mods::log::warn("puppet: {} has no {}", mPropArc, info.bmd);
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
            mPropFailed = true;
            return false;
        }
    }

    const PropInfo& info = prop_info(kind);
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
    mCyl.SetC(current.pos);
    mCyl.SetR(r);
    mCyl.SetH(h);
    dComIfG_Ccsp()->Set(&mCyl);
}

void Puppet::publish(bool visible, float height) {
    Slot& s = slot();
    s.visible = visible;
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
    mCylArmed = false;

    if (mLocal) {
        fopAc_ac_c* player = dComIfGp_getPlayer(0);
        if (player == nullptr) {
            mVisible = false;
            publish(false, 0.0f);
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
        publish(mVisible, mVisible ? prop_info(local::prop()).height : 0.0f);
        return 1;
    }

    const match::Player& p = match::player(mSlot);
    const PlayerState& s = p.state;
    mVisible = p.present && p.hasState && same_stage_as_me(s) && now_ms() - p.stateAt < kStaleMs;
    if (!mVisible) {
        publish(false, 0.0f);
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
    if (mDisguised) {
        const bool moving = (current.pos - before).abs() > 1.0f;
        mVisible = updateProp(kind, moving);
    } else {
        if (mPropModel != nullptr || mPropRequested) releaseProp();
        const uint8_t color = p.color;
        if (mBody == nullptr || color != mColor) buildLink(color);
        mVisible = mBody != nullptr && !(s.flags & STATE_WOLF);
        if (mVisible) poseLink(s);
        mVisible = mVisible && mUnder[0].getAnmTransform() != nullptr;
    }
    armHitbox(p, mDisguised, kind);
    publish(mVisible, mDisguised ? prop_info(kind).height : kLinkHeight);
    return 1;
}

int Puppet::draw() {
    if (!mVisible) return 1;
    g_env_light.settingTevStruct(0, &current.pos, &tevStr);
    float shadow = kLinkRadius;
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
        shadow = prop_info(mPropKind).radius * 1.3f;
    } else {
        J3DModel* models[] = {mBody, mFace, mHead, mHands, mSheath, mSword, mShield};
        for (J3DModel* model : models) {
            if (model == nullptr) continue;
            g_env_light.setLightTevColorType_MAJI(model, &tevStr);
            mDoExt_modelEntryDL(model);
        }
    }
    if (mGroundY != -G_CM3D_F_INF) {
        dComIfGd_setSimpleShadow(&current.pos, mGroundY, shadow, mGndChk, 0, 1.0f,
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
    publish(false, 0.0f);
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
}

bool local_prop_visible() {
    return s_localProp.visible;
}

bool anchor(int id, cXyz& feet, float& height) {
    if (id < 1 || id > kMaxPlayers || !s_slots[id].visible) return false;
    feet = s_slots[id].feet;
    height = s_slots[id].height;
    return true;
}

}  // namespace hs::puppet
