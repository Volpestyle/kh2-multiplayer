#pragma once
// VUH-2252 (D14 item 3): mirror the host's script calls on its mirrored enemies.
// A client runs no brain for a mirrored enemy (EnemyMirror.inl), so the calls its
// script makes on itself never happen there: a tilt, a texture animation, a shadow,
// and the effects it starts, moves, pauses and ends. The host sees each one at the
// bdscript trap table and sends it in a ScriptCall only when its value changed; the
// client replays it on its own copy.
// Pure rules only: no game memory, no Windows calls. The native adapter is
// ScriptMirror.inl (included inside EntityHook's namespace). See docs/SCRIPT_MIRROR.md.
//
// Each trap here was decompiled (Steam 9002b2de): it reads only its argument array
// (a u64 per argument, the handle in the low word), decodes its actor or effect through
// 0x4AD270, and calls or writes once. None reads the running script thread
// (exe+0x2A250B0) or its VM, so it can run from frame code.
// Trap names are OpenKH's BdxTraps.cs; the call list follows TCM-Murray's
// world-mirror research (GPL-3.0, docs/research/PRIOR_ART.md). No code is copied.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <unordered_map>
#include <utility>
#include <vector>
#include "ScriptMirrorPins.hpp"
#include "ScriptReaction.hpp"

namespace kh2coop::inject::scriptmirror::rules {

inline constexpr std::uint32_t kTagAddress = 0x52444140;  // '@ADR' (a handle)
inline constexpr std::uint32_t kTagInt = 0x544E4940;      // '@INT'
inline constexpr std::uint32_t kTagFloat = 0x544C4640;    // '@FLT'
inline constexpr std::uint32_t kTagWork = 0x3F3F3F40;     // '@???' (native BDX workspace value; never wire)
inline constexpr std::uintptr_t kTrapTables = 0x753490;   // *(exe + this + 8 * table) = table, 16-byte entries {fn, flags}

// Effect instance (0x2C6B30 makes them from the actor's own table at actor+0x80): +0xC flags
// (0x10000 = killed), +0x30 its part, freed and zeroed on a kill. The part holds pos +0x00,
// rot +0x10, scale +0x20, color +0x30 (the setters' getters 0x2BE970/0x2BE1B0/0x2BEA80/0x2BE1E0).
// The game never checks a handle: a freed effect still decodes (Murray's fault on a replayed
// fade-out), so an effect is acted on only while its part is still the one recorded.
inline constexpr std::uintptr_t kEffectFlags = 0xC, kEffectPart = 0x30;
inline constexpr std::uint32_t kEffectKilled = 0x10000;

enum class Kind : std::uint8_t {
    Actor,          // arg0 is the actor; the values are sent as the script passed them
    ActorEvent,     // the same, but every call counts (obj_effect_kill_all)
    EffectStart,    // arg0 is the actor; the trap returns a new effect's handle in args[0]
    EffectStartPos, // obj_effect_start_pos: a start plus its position, sent as 1/21 + 0/38
    EffectField,    // arg0 is an effect; the part field it set is read back and sent as a float4
    AttackStart, AttackField, AttackCall, AttackRc, ReactionOffer, ReactionOwner, Owner, OwnerAttackStart, AttackStrike,
    EffectCall,     // arg0 is an effect; the values are sent as passed
};
// How a client replays a wire record: through the native trap with its own actor's or effect's
// handle, obj_set_xyzrot's float4 through its helper, or a field write into the paired effect.
enum class Replay : std::uint8_t { Trap, XyzRotHelper, FieldWrite, AttackVector, AttackRc, AttackStrike, Owner, None };
// Calls in one group overwrite each other's effect, so only the latest value per group counts.
// Event calls each count: they are never compared, replaced or dropped as repeats.
enum class Group : std::uint8_t { Rotation, Texanm, Shadow, Scissoring, Event, EffectPos, EffectRot, EffectScale, EffectColor, EffectPause, AttackPos, AttackRadius, AttackOffset, OwnerPosition, OwnerMotion, Visibility };

struct Trap {
    std::uint8_t table;
    std::uint16_t index;
    std::uint32_t fnRva;        // the table entry's native function
    std::uint32_t flags;        // the entry's flags: low 16 bits = script argument count (bit 30: returns)
    Kind kind;
    std::uint8_t values;        // values on the wire after the actor or effect (a vector sends 4 floats)
    bool vector;                // the wire values are a float4
    Replay replay;              // how a client replays a record of this (table, index)
    Group group;
    bool keyedByValue;          // the first value names what it acts on (texanm id): one key per value
    std::uint8_t wireTable;     // what the host sends for it
    std::uint16_t wireIndex;
    std::uint16_t field;        // offset in a paired effect part or attack instance
    bool ends;                  // the effect is gone after this call
    const std::uint8_t* body;
    std::size_t bodySize;
    const char* name;
};
#define KH2_PIN(b) b, sizeof(b)
inline constexpr Trap kTraps[] = {
    // Actor calls (slice 1).
    {1, 1, 0x42BF60, 0x00000002, Kind::Actor, 1, false, Replay::Trap, Group::Rotation, false, 1, 1, 0, false, KH2_PIN(kObjSetRot), "obj_set_rot"},  // 0x3B6560(actor, angle)
    {8, 2, 0x537E70, 0x00000002, Kind::Actor, 4, true, Replay::XyzRotHelper, Group::Rotation, false, 8, 2, 0, false, KH2_PIN(kObjSetXyzRot), "obj_set_xyzrot"}, // 0x3B65F0(actor, float4*)
    {1, 85, 0x42F820, 0x00000002, Kind::Actor, 1, false, Replay::Trap, Group::Texanm, true, 1, 85, 0, false, KH2_PIN(kTexanmStart), "obj_texanm_start"},  // 0x3CD8A0(actor, id)
    {1, 86, 0x42F850, 0x00000002, Kind::Actor, 1, false, Replay::Trap, Group::Texanm, true, 1, 86, 0, false, KH2_PIN(kTexanmStop), "obj_texanm_stop"},   // 0x3CD8B0(actor, id)
    {6, 45, 0x53E960, 0x00000001, Kind::Actor, 0, false, Replay::Trap, Group::Shadow, false, 6, 45, 0, false, KH2_PIN(kHideShadow), "obj_hide_shadow"},  // actor+0x124 |= 0x200
    {6, 64, 0x53EE90, 0x00000001, Kind::Actor, 0, false, Replay::Trap, Group::Shadow, false, 6, 64, 0, false, KH2_PIN(kShowShadow), "obj_show_shadow"},  // 0x3CD820(actor)
    {6, 65, 0x53EEB0, 0x00000002, Kind::Actor, 1, false, Replay::Trap, Group::Scissoring, false, 6, 65, 0, false, KH2_PIN(kSetScissoring), "obj_set_scissoring"}, // actor+0x8C8 bit 0x20
    // Effects (slice 2). Starts: 0x3B6630/0x3B6670(actor, id, a2, a3) -> 0x2C6B30(actor+0x80, id, a2, 1, a3, bind).
    {1, 21, 0x42D440, 0x40000004, Kind::EffectStart, 3, false, Replay::Trap, Group::Event, false, 1, 21, 0, false, KH2_PIN(kObjEffectStart), "obj_effect_start"},
    {1, 87, 0x42D540, 0x40000004, Kind::EffectStart, 3, false, Replay::Trap, Group::Event, false, 1, 87, 0, false, KH2_PIN(kObjEffectStartBind), "obj_effect_start_bind"},
    // 0x3B66C0(actor, id, float4*, a3, a4) is 0x2C6B30(actor+0x80, id, a3, 1, a4, 0) plus a pos copy:
    // exactly obj_effect_start(id, a3, a4) followed by effect_set_pos.
    {1, 22, 0x42D4B0, 0x40000005, Kind::EffectStartPos, 3, false, Replay::None, Group::Event, false, 1, 21, 0, false, KH2_PIN(kObjEffectStartPos), "obj_effect_start_pos"},
    {0, 38, 0x41DF80, 0x00000002, Kind::EffectField, 4, true, Replay::FieldWrite, Group::EffectPos, false, 0, 38, 0x00, false, KH2_PIN(kEffectSetPos), "effect_set_pos"},
    {0, 39, 0x41E000, 0x00000002, Kind::EffectField, 4, true, Replay::FieldWrite, Group::EffectScale, false, 0, 39, 0x20, false, KH2_PIN(kEffectSetScale), "effect_set_scale"},
    {0, 40, 0x41E080, 0x00000002, Kind::EffectField, 4, true, Replay::FieldWrite, Group::EffectRot, false, 0, 40, 0x10, false, KH2_PIN(kEffectSetRot), "effect_set_rot"},
    // 0x3B7710 turns the direction into the rotation it writes: sent as the effect_set_rot it results in.
    {0, 41, 0x41E100, 0x00000002, Kind::EffectField, 4, true, Replay::None, Group::EffectRot, false, 0, 40, 0x10, false, KH2_PIN(kEffectSetDir), "effect_set_dir"},
    {0, 67, 0x41E520, 0x00000002, Kind::EffectField, 4, true, Replay::FieldWrite, Group::EffectColor, false, 0, 67, 0x30, false, KH2_PIN(kEffectSetColor), "effect_set_color"},
    {0, 66, 0x41E4F0, 0x00000002, Kind::EffectCall, 1, false, Replay::Trap, Group::EffectPause, false, 0, 66, 0, false, KH2_PIN(kEffectPause), "effect_pause"},  // 0x2BED00(effect, on)
    {0, 44, 0x41E190, 0x00000001, Kind::EffectCall, 0, false, Replay::Trap, Group::Event, false, 0, 44, 0, false, KH2_PIN(kEffectLoopEnd), "effect_loop_end"},
    {0, 51, 0x41E250, 0x00000001, Kind::EffectCall, 0, false, Replay::Trap, Group::Event, false, 0, 51, 0, true, KH2_PIN(kEffectKill), "effect_kill"},
    {0, 52, 0x41E270, 0x00000003, Kind::EffectCall, 2, false, Replay::Trap, Group::Event, false, 0, 52, 0, false, KH2_PIN(kEffectFadeout), "effect_fadeout"},  // (time, mode)
    {0, 85, 0x41E1C0, 0x00000001, Kind::EffectCall, 0, false, Replay::Trap, Group::Event, false, 0, 85, 0, true, KH2_PIN(kEffectLoopEndKill), "effect_loop_end_kill"},
    {1, 349, 0x42D690, 0x00000001, Kind::ActorEvent, 0, false, Replay::Trap, Group::Event, false, 1, 349, 0, false, KH2_PIN(kObjEffectKillAll), "obj_effect_kill_all"},
    // Script-created attacks: same host resource-ID, ordered-start and field machinery as effects.
    {2, 10, 0x432250, 0x40000004, Kind::AttackStart, 3, false, Replay::Trap, Group::Event, false, 2, 10, 0, false, KH2_PIN(kAttackNew), "attack_new"},
    {2, 11, 0x4322F0, 0x00000003, Kind::AttackCall, 2, false, Replay::Trap, Group::AttackRadius, false, 2, 11, 0, false, KH2_PIN(kAttackRadius), "attack_set_radius"},
    {2, 12, 0x432330, 0x00000002, Kind::AttackField, 4, true, Replay::AttackVector, Group::AttackPos, false, 2, 12, 0xE4, false, KH2_PIN(kAttackPos), "attack_set_pos"},
    {2, 13, 0x4323B0, 0x00000001, Kind::AttackCall, 0, false, Replay::Trap, Group::Event, false, 2, 13, 0, true, KH2_PIN(kAttackFree), "attack_free"},
    {7, 14, 0x53F300, 0x00000002, Kind::AttackField, 4, true, Replay::FieldWrite, Group::AttackOffset, false, 7, 14, 0x128, false, KH2_PIN(kAttackReactionOffset), "attack_set_reaction_offset"},
    {2, 51, 0x432790, 0x00000005, Kind::AttackRc, 3, false, Replay::AttackRc, Group::Event, false, 2, 51, 0, false, KH2_PIN(kAttackRc), "attack_set_rc"},
    // Normalized ScriptMirror operation. Never installed in a native trap table.
    {255, 1, 0, 0, Kind::ReactionOffer, 4, false, Replay::Owner, Group::Event, false, 255, 1, 0, false, nullptr, 0, "reaction_offer"},
    {255, 12, 0, 0, Kind::ReactionOwner, 3, false, Replay::Owner, Group::Event, false, 255, 12, 0, false, nullptr, 0, "reaction_owner"},
    {255,2,0,0,Kind::Owner,4,false,Replay::Owner,Group::Event,false,255,2,0,false,nullptr,0,"actor_owner_begin"},
    {255,3,0,0,Kind::Owner,2,false,Replay::Owner,Group::Event,false,255,3,0,false,nullptr,0,"actor_owner_capture"},
    {255,4,0,0,Kind::Owner,4,true,Replay::Owner,Group::OwnerPosition,false,255,4,0,false,nullptr,0,"actor_owner_position"},
    {255,5,0,0,Kind::Owner,2,false,Replay::Owner,Group::Event,false,255,5,0,false,nullptr,0,"actor_owner_camera"},
    {255,6,0,0,Kind::Owner,0,false,Replay::Owner,Group::Event,false,255,6,0,true,nullptr,0,"actor_owner_end"},
    {255,7,0,0,Kind::Owner,4,false,Replay::Owner,Group::Event,false,255,7,0,false,nullptr,0,"actor_owner_command"},
    {255,8,0,0,Kind::Owner,2,false,Replay::Owner,Group::OwnerMotion,false,255,8,0,false,nullptr,0,"actor_owner_motion"},
    {255,9,0,0,Kind::OwnerAttackStart,4,false,Replay::Owner,Group::Event,false,255,9,0,false,nullptr,0,"actor_owner_attack"},
    {255,11,0,0,Kind::Owner,1,false,Replay::Owner,Group::Event,false,255,11,0,false,nullptr,0,"actor_owner_heartbeat"},
    {2,17,0x4328E0,4,Kind::AttackStrike,2,false,Replay::AttackStrike,Group::Event,false,2,17,0,false,KH2_PIN(kAttackStrike),"attack_strike_source"},
    {255,13,0,0,Kind::Owner,2,false,Replay::Owner,Group::Event,false,255,13,0,false,nullptr,0,"actor_owner_attach"},
    {255,14,0,0,Kind::Owner,0,false,Replay::Owner,Group::Event,false,255,14,0,false,nullptr,0,"actor_owner_detach"},
    {1,133,0x42B640,1,Kind::Actor,0,false,Replay::Trap,Group::Visibility,false,1,133,0,false,KH2_PIN(kObjHide),"obj_hide"},
    {1,134,0x42B680,1,Kind::Actor,0,false,Replay::Trap,Group::Visibility,false,1,134,0,false,KH2_PIN(kObjShow),"obj_show"},

};
#undef KH2_PIN
inline constexpr std::size_t kTrapCount = sizeof(kTraps) / sizeof(kTraps[0]);
inline int FindTrap(std::uint8_t table, std::uint16_t index) noexcept {
    for (std::size_t i = 0; i < kTrapCount; ++i)
        if (kTraps[i].table == table && kTraps[i].index == index) return static_cast<int>(i);
    return -1;
}
// The trap a wire record replays as (its own entry: a converted trap is never on the wire).
inline int FindWire(std::uint8_t table, std::uint16_t index) noexcept {
    const int i = FindTrap(table, index);
    return i >= 0 && kTraps[i].replay != Replay::None ? i : -1;
}
inline bool IsAttackKind(Kind k) noexcept { return k == Kind::AttackStart || k == Kind::OwnerAttackStart || k == Kind::AttackStrike || k == Kind::AttackField || k == Kind::AttackCall || k == Kind::AttackRc || k == Kind::ReactionOffer || k == Kind::ReactionOwner; }
inline bool IsEffectKind(Kind k) noexcept { return k == Kind::EffectStart || k == Kind::EffectStartPos || k == Kind::EffectField || k == Kind::EffectCall; }
inline bool EffectRecord(std::size_t trap) noexcept { return trap < kTrapCount && (IsEffectKind(kTraps[trap].kind) || IsAttackKind(kTraps[trap].kind) || kTraps[trap].kind==Kind::Owner); }
inline std::uintptr_t EntryRva(std::uintptr_t tableRva, std::uint16_t index) noexcept { return tableRva + 16u * index; }
inline bool BodyMatches(std::size_t trap, const std::uint8_t* bytes) noexcept {
    return trap < kTrapCount && std::memcmp(bytes, kTraps[trap].body, kTraps[trap].bodySize) == 0;
}

inline std::uint64_t Tagged(std::uint32_t tag, std::uint32_t bits) noexcept {
    return (static_cast<std::uint64_t>(tag) << 32) | bits;
}
inline std::uint32_t TagOf(std::uint64_t v) noexcept { return static_cast<std::uint32_t>(v >> 32); }
inline std::uint32_t BitsOf(std::uint64_t v) noexcept { return static_cast<std::uint32_t>(v); }
inline float FloatOf(std::uint64_t v) noexcept { float f; const auto b = BitsOf(v); std::memcpy(&f, &b, sizeof(f)); return f; }

// The wire codec's value rules (Protocol.hpp validScriptCallEntry): every value the trap uses is
// '@INT' or a finite '@FLT' within kMaxFloat (a vector: four '@FLT'), every unused one is zero.
inline constexpr float kMaxFloat = 100000.0f;
inline bool ValidValues(std::size_t trap, const std::uint64_t* values) noexcept {
    if (trap >= kTrapCount) return false;
    const auto& t = kTraps[trap];
    for (unsigned k = 0; k < 4; ++k) {
        if (k >= t.values) { if (values[k] != 0) return false; continue; }
        const auto tag = TagOf(values[k]);
        const bool integers=t.kind==Kind::ReactionOffer || t.kind==Kind::ReactionOwner || t.kind==Kind::OwnerAttackStart ||
            (t.kind==Kind::Owner && (t.index==2 || t.index==5 || t.index==7 || t.index==11 || t.index==13));
        if(integers && tag!=kTagInt)return false;
        if (tag == kTagFloat) {
            const float f = FloatOf(values[k]);
            if (!std::isfinite(f) || std::fabs(f) > kMaxFloat) return false;
        } else if (tag != kTagInt || t.vector) return false;
    }
    if(t.kind==Kind::ReactionOffer &&
       (!(BitsOf(values[0]) || BitsOf(values[1])) || !BitsOf(values[2]) ||
        BitsOf(values[2])>UINT16_MAX || BitsOf(values[3])>=3))return false;
    if(t.kind==Kind::ReactionOwner && (BitsOf(values[0])>=3 || !(BitsOf(values[1]) || BitsOf(values[2]))))return false;
    if(t.kind==Kind::Owner) {
        if(t.index==2 && (BitsOf(values[0])>=3 || !(BitsOf(values[1]) || BitsOf(values[2])) || BitsOf(values[3])!=60))return false;
        if((t.index==3 || t.index==8) && (TagOf(values[0])!=kTagInt || BitsOf(values[0])>4095 ||
           TagOf(values[1])!=kTagFloat || FloatOf(values[1])<0))return false;
        if(t.index==5 && (!BitsOf(values[0]) || BitsOf(values[1])>1))return false;
        if(t.index==13 && (BitsOf(values[0])!=16386 || BitsOf(values[1])!=5))return false;
        if(t.index==7 && ((BitsOf(values[0])!=1 && BitsOf(values[0])!=2) || !BitsOf(values[1]) || BitsOf(values[1])>UINT16_MAX || BitsOf(values[3])>255))return false;
    }
    if(t.kind==Kind::OwnerAttackStart && (!(BitsOf(values[0])&0x80000000u) || !BitsOf(values[1]) || BitsOf(values[1])>UINT16_MAX))return false;
    return true;
}

// Native wrappers consume low32 scalar bits regardless of BDX workspace tag.
// Only these pinned wrapper positions are scalars; actor/resource/vector inputs
// never pass through here. This is a producer boundary, never a wire decoder.
inline bool NormalizeNativeScalars(std::size_t trap, const std::uint64_t* args,
                                   std::uint64_t* values) noexcept {
    if(trap>=kTrapCount || !args || !values)return false;
    const auto& t=kTraps[trap];
    std::uint8_t positions[3]{};std::uint32_t tags[3]{};unsigned count=0;
    const auto integers=[&](std::uint8_t first,unsigned n) {
        count=n;for(unsigned k=0;k<n;++k){positions[k]=static_cast<std::uint8_t>(first+k);tags[k]=kTagInt;}
    };
    if(t.kind==Kind::AttackStart || t.kind==Kind::EffectStart)integers(1,3);
    else if(t.kind==Kind::EffectStartPos){count=3;positions[0]=1;positions[1]=3;positions[2]=4;for(auto& tag:tags)tag=kTagInt;}
    else if(t.table==2 && t.index==11){count=2;positions[0]=1;positions[1]=2;tags[0]=tags[1]=kTagFloat;}
    else if(t.kind==Kind::AttackRc){count=3;positions[0]=2;positions[1]=3;positions[2]=4;tags[0]=kTagInt;tags[1]=tags[2]=kTagFloat;}
    else if(t.kind==Kind::AttackStrike)integers(2,2);
    else if(t.table==0 && t.index==66)integers(1,1);
    else if(t.table==0 && t.index==52){count=2;positions[0]=1;positions[1]=2;tags[0]=kTagFloat;tags[1]=kTagInt;}
    else if(t.values || (t.kind!=Kind::AttackCall && t.kind!=Kind::EffectCall))return false;
    for(unsigned k=0;k<4;++k)values[k]=k<count?Tagged(tags[k],BitsOf(args[positions[k]])):0;
    if(!ValidValues(trap,values))return false;
    if(t.kind==Kind::AttackRc && (!BitsOf(values[0]) || BitsOf(values[0])>UINT16_MAX ||
       FloatOf(values[1])<0 || FloatOf(values[2])<0))return false;
    if(t.table==2 && t.index==11 && (FloatOf(values[0])<0 || FloatOf(values[1])<0))return false;
    return true;
}

// One call reduced to its wire record: the trap it replays as, the values, the effect id.
struct Call {
    std::uint16_t netId = 0;
    std::uint32_t objectId = 0;
    std::uint8_t trap = 0;         // index into kTraps of the wire (table, index)
    std::uint32_t effect = 0;      // host effect id (effect records); 0 otherwise
    std::uint64_t values[4] {};
};
inline bool SameValue(const Call& a, const Call& b) noexcept {
    return a.trap == b.trap && a.effect == b.effect && std::memcmp(a.values, b.values, sizeof(a.values)) == 0;
}
inline bool IsEvent(const Call& c) noexcept { return c.trap < kTrapCount && kTraps[c.trap].group == Group::Event; }
// Actor records: netId, group and (texanm) the id the call acts on. Effect records: effect id and group.
inline std::uint64_t KeyOf(const Call& c) noexcept {
    const auto& t = kTraps[c.trap];
    if (c.effect)
        return (std::uint64_t{1} << 63) | (static_cast<std::uint64_t>(c.effect) << 16) |
               (static_cast<std::uint64_t>(t.group) << 8) | (IsAttackKind(t.kind) ? 1u : 0u);
    const std::uint32_t sub = t.keyedByValue ? BitsOf(c.values[0]) : 0;
    return (static_cast<std::uint64_t>(c.netId) << 48) | (static_cast<std::uint64_t>(t.group) << 40) | sub;
}

// Host: send on change. A call whose value equals the last sent (or still queued) one for its key
// is a repeat and costs nothing on the wire; a changed value replaces a still-queued one for the
// same key; everything else queues in call order. Events always queue and never set a baseline.
// Sent values become the new baseline only when the packet carrying them was accepted for
// sending. Reset on every room or session change.
class Sender {
public:
    enum class Outcome : std::uint8_t { Queued, Replaced, Repeat, Full };
    static constexpr std::size_t kPendingCap = 256, kKeyCap = 4096;

    Outcome Observe(const Call& c) {
        if (IsEvent(c)) {
            if (pending_.size() >= kPendingCap) return Outcome::Full;
            pending_.push_back(c);
            return Outcome::Queued;
        }
        const auto key = KeyOf(c);
        for (auto& p : pending_) {
            if (IsEvent(p) || KeyOf(p) != key) continue;
            if (SameValue(p, c)) return Outcome::Repeat;
            p = c;
            return Outcome::Replaced;
        }
        const auto found = sent_.find(key);
        if (found != sent_.end() && SameValue(found->second, c)) return Outcome::Repeat;
        if (pending_.size() >= kPendingCap) return Outcome::Full;
        pending_.push_back(c);
        return Outcome::Queued;
    }
    // The oldest queued calls, at most max.
    std::size_t Peek(Call* out, std::size_t max) const noexcept {
        const std::size_t n = pending_.size() < max ? pending_.size() : max;
        for (std::size_t i = 0; i < n; ++i) out[i] = pending_[i];
        return n;
    }
    // The first n queued calls were accepted for sending: they become the baseline.
    void Commit(std::size_t n) {
        if (n > pending_.size()) n = pending_.size();
        for (std::size_t i = 0; i < n; ++i) {
            if (IsEvent(pending_[i])) continue;
            if (sent_.size() >= kKeyCap && !sent_.count(KeyOf(pending_[i]))) sent_.clear();  // bounded; resends are harmless
            sent_[KeyOf(pending_[i])] = pending_[i];
        }
        pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(n));
    }
    void Reset() { pending_.clear(); sent_.clear(); }
    std::size_t Pending() const noexcept { return pending_.size(); }

private:
    std::vector<Call> pending_;
    std::unordered_map<std::uint64_t, Call> sent_;
};

// Collision ends cannot be discarded when the normal event queue is full.
// The adapter admits at most kCap live + deferred resources in total.
class Retirements {
public:
    static constexpr std::size_t kCap = 512;
    bool Put(const Call& c) {
        for (const auto& old : calls_) if (old.effect==c.effect && old.netId==c.netId) return true;
        if (calls_.size()>=kCap) return false;
        calls_.push_back(c); return true;
    }
    void Drain(Sender& sender) {
        while (!calls_.empty()) {
            if (sender.Observe(calls_.front())==Sender::Outcome::Full) return;
            calls_.erase(calls_.begin());
        }
    }
    std::size_t Size() const noexcept { return calls_.size(); }
    void Reset() { calls_.clear(); }
private:
    std::vector<Call> calls_;
};

// Host: the effects mirrored enemies' scripts started, by the game's handle. Each gets a fresh id
// (never 0, never reused) so a handle the game reuses can't be confused with the effect before it.
struct HostEffect {
    std::uint32_t id = 0;
    std::uintptr_t effect = 0, part = 0;  // the instance and its part when it was started
    std::uint16_t netId = 0;
    std::uint32_t objectId = 0;
    std::uint8_t ownerSlot = 0xFF;
    std::uint32_t ownerRole = 0;
};
enum class NativeResourceStatus : std::uint8_t { Paired, WrongTag, Unpaired, NotCurrent };
struct NativeResourceReference {
    NativeResourceStatus status{NativeResourceStatus::Unpaired};
    std::uint32_t handle{};
    HostEffect value{}; // snapshot: a native call may retire or replace the map entry
};
class HostEffects {
public:
    static constexpr std::size_t kCap = 512;
    std::uint32_t Add(std::uint32_t handle, std::uintptr_t effect, std::uintptr_t part, std::uint16_t netId, std::uint32_t objectId, bool evict = true, std::uint8_t ownerSlot=0xFF, std::uint32_t ownerRole=0) {
        if (!handle) return 0;
        if (next_ == 0x7FFFFFFFu) return 0;
        if (byHandle_.size() >= kCap && !byHandle_.count(handle)) {
            if (!evict) return 0;
            EvictOldest();
        }
        ++next_;
        byHandle_[handle] = {next_, effect, part, netId, objectId,ownerSlot,ownerRole};
        return next_;
    }
    template <typename Visit> void Each(Visit&& visit) const {
        for (const auto& [handle,e] : byHandle_) visit(handle,e);
    }
    const HostEffect* Find(std::uint32_t handle) const {
        const auto found = byHandle_.find(handle);
        return found == byHandle_.end() ? nullptr : &found->second;
    }
    // Native BDX stores returned handles in 32-bit workspace fields and pushes
    // them back as @??? (and sometimes @INT). A scalar is only a lookup key: never resolve an unknown
    // handle, and require the adapter's allocation/part-or-owner validation.
    template <typename Current>
    NativeResourceReference CaptureNative(std::uint64_t argument, Current&& current) const {
        NativeResourceReference out;out.handle=BitsOf(argument);
        const auto tag=TagOf(argument);
        if(tag!=kTagAddress && tag!=kTagInt && tag!=kTagWork){out.status=NativeResourceStatus::WrongTag;return out;}
        const auto* paired=Find(out.handle);
        if(!paired)return out;
        out.value=*paired;
        out.status=current(out.handle,out.value)?NativeResourceStatus::Paired:NativeResourceStatus::NotCurrent;
        return out;
    }
    bool StillPaired(const NativeResourceReference& captured) const {
        const auto* now=Find(captured.handle);
        return captured.value.id && now && now->id==captured.value.id &&
            now->effect==captured.value.effect && now->part==captured.value.part;
    }
    const HostEffect* FindAllocation(std::uintptr_t allocation) const {
        for (const auto& [handle,e] : byHandle_) {
            (void)handle;
            if(e.effect==allocation)return &e;
        }
        return nullptr;
    }
    void Erase(std::uint32_t handle) { byHandle_.erase(handle); }
    void ForgetAllocation(std::uintptr_t allocation) {
        for (auto it=byHandle_.begin();it!=byHandle_.end();)
            it=it->second.effect==allocation?byHandle_.erase(it):std::next(it);
    }
    void EraseOwner(std::uint16_t netId) {
        for (auto it = byHandle_.begin(); it != byHandle_.end();) it = it->second.netId == netId ? byHandle_.erase(it) : std::next(it);
    }
    void Reset() { byHandle_.clear(); }
    std::size_t Size() const noexcept { return byHandle_.size(); }

private:
    void EvictOldest() {
        auto oldest = byHandle_.begin();
        for (auto it = byHandle_.begin(); it != byHandle_.end(); ++it) if (it->second.id < oldest->second.id) oldest = it;
        if (oldest != byHandle_.end()) byHandle_.erase(oldest);
    }
    std::unordered_map<std::uint32_t, HostEffect> byHandle_;
    std::uint32_t next_ = 0;
};

// Client: the effect each host effect id became here (its handle, instance and part as started).
struct ClientEffect {
    std::uint32_t handle = 0;
    std::uintptr_t effect = 0, part = 0;
    std::uint16_t netId = 0;
    std::uint8_t ownerSlot = 0xFF;
    std::uint32_t ownerRole = 0;
};
class ClientEffects {
public:
    static constexpr std::size_t kCap = 512;
    bool Put(std::uint32_t id, const ClientEffect& e, bool evict = true) {
        if (!id) return false;
        if (byId_.size() >= kCap && !byId_.count(id)) {
            if (!evict) return false;
            byId_.erase(OldestId());
        }
        byId_[id] = e;
        return true;
    }
    template <typename Visit> void Each(Visit&& visit) const {
        for (const auto& [id, e] : byId_) visit(id, e);
    }
    const ClientEffect* Find(std::uint32_t id) const {
        const auto found = byId_.find(id);
        return found == byId_.end() ? nullptr : &found->second;
    }
    void Erase(std::uint32_t id) { byId_.erase(id); }
    void ForgetAllocation(std::uintptr_t allocation) {
        for (auto it=byId_.begin();it!=byId_.end();)
            it=it->second.effect==allocation?byId_.erase(it):std::next(it);
    }
    void EraseOwner(std::uint16_t netId) {
        for (auto it = byId_.begin(); it != byId_.end();) it = it->second.netId == netId ? byId_.erase(it) : std::next(it);
    }
    void Reset() { byId_.clear(); }
    std::size_t Size() const noexcept { return byId_.size(); }

private:
    std::uint32_t OldestId() const {  // ids grow, so the smallest is the oldest
        std::uint32_t oldest = 0;
        for (const auto& [id, e] : byId_) if (!oldest || id < oldest) oldest = id;
        return oldest;
    }
    std::unordered_map<std::uint32_t, ClientEffect> byId_;
};

// Client: a call for a netId whose copy is not driven yet (spawn race) waits here, latest value per
// key, and is replayed once the copy is driven; it is dropped after kHoldFrames. An effect record
// waits only behind its own held start; events keep their order and are never replaced.
class Hold {
public:
    static constexpr std::size_t kCap = 64;
    static constexpr std::uint32_t kHoldFrames = 180;
    struct Item { Call call; std::uint32_t frame; };

    bool Put(const Call& c, std::uint32_t frame) {
        if (!IsEvent(c)) {
            const auto key = KeyOf(c);
            for (auto& i : items_) if (!IsEvent(i.call) && KeyOf(i.call) == key) { i = {c, frame}; return true; }
        }
        if (items_.size() >= kCap) return false;
        items_.push_back({c, frame});
        return true;
    }
    // Whether a start for this effect id is waiting here.
    bool HoldsStart(std::uint32_t effect, bool attack = false) const noexcept {
        for (const auto& i : items_)
            if (i.call.effect == effect && (kTraps[i.call.trap].kind == (attack ? Kind::AttackStart : Kind::EffectStart) ||
                (attack && kTraps[i.call.trap].kind==Kind::OwnerAttackStart))) return true;
        return false;
    }
    void CancelResource(std::uint16_t netId,std::uint32_t resource,bool attack) {
        for(auto it=items_.begin();it!=items_.end();) {
            const auto& c=it->call;
            const bool same=c.netId==netId && c.effect==resource && IsAttackKind(kTraps[c.trap].kind)==attack;
            it=same?items_.erase(it):std::next(it);
        }
    }
    void CancelOwner(std::uint16_t netId,std::uint32_t resource) {
        for(auto it=items_.begin();it!=items_.end();)
            it=it->call.netId==netId && it->call.effect==resource && kTraps[it->call.trap].kind==Kind::Owner
                ?items_.erase(it):std::next(it);
    }
    // Drops expired items; hands the rest to ready(call) in arrival order and removes those it took.
    // An item that isn't ready blocks the later items for the same netId, so their order holds.
    template <typename Ready> std::size_t Drain(std::uint32_t frame, Ready&& ready, std::size_t& expired) {
        std::size_t taken = 0;
        expired = 0;
        std::vector<std::uint16_t> blocked;
        for (std::size_t i = 0; i < items_.size();) {
            const auto& c = items_[i].call;
            if (frame - items_[i].frame > kHoldFrames) { items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(i)); ++expired; continue; }
            bool isBlocked = false;
            for (auto n : blocked) isBlocked = isBlocked || n == c.netId;
            if (!isBlocked && ready(c)) { items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(i)); ++taken; continue; }
            if (!isBlocked) blocked.push_back(c.netId);
            ++i;
        }
        return taken;
    }
    void Erase(std::uint64_t key) {
        for (std::size_t i = 0; i < items_.size(); ++i)
            if (!IsEvent(items_[i].call) && KeyOf(items_[i].call) == key) { items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(i)); return; }
    }
    void Reset() { items_.clear(); }
    std::size_t Size() const noexcept { return items_.size(); }
    // Whether anything for this netId is waiting: a later record must wait behind it.
    bool HoldsNetId(std::uint16_t netId) const noexcept {
        for (const auto& i : items_) if (i.call.netId == netId) return true;
        return false;
    }

private:
    std::vector<Item> items_;
};

// Per-second counters (both roles); a line is logged for every second with any activity.
struct Second {
    std::uint32_t raw = 0, offThread = 0;                                     // host: detour entries, before any filter
    std::uint32_t calls = 0, unmapped = 0, notMirrored = 0, untracked = 0;     // untracked: an effect the host didn't see start
    std::uint32_t repeats = 0, replaced = 0, queued = 0, full = 0;
    std::uint32_t packets = 0, records = 0, sendFailed = 0;                   // host
    std::uint32_t received = 0, applied = 0, held = 0, heldApplied = 0, expired = 0, refused = 0, unpaired = 0, stale = 0; // client
    std::uint32_t rawPerTrap[kTrapCount] {};                                  // host detour entries by trap
    std::uint32_t perTrap[kTrapCount] {};                                     // host sent calls / client records by trap
    bool Any() const noexcept {
        return raw || offThread || calls || unmapped || notMirrored || untracked || packets || received || refused ||
               expired || held || heldApplied || unpaired || stale;
    }
};

} // namespace kh2coop::inject::scriptmirror::rules
