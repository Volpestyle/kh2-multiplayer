#pragma once
#include "AvatarPositionDiagnostic.hpp"
#include "kh2coop/AvatarPositionFault.hpp"
namespace kh2coop::inject::avatarposition {
inline avatarfault::Binding FaultBinding(const Scope& s) {
    return {s.actor,s.provenance.ownerConnectionId,s.provenance.localConnectionId,s.provenance.hostConnectionId,
        s.handle,s.transition,s.load,s.provenance.generation,s.world,s.room,s.owner,s.character,s.puppetIndex,
        static_cast<std::uint8_t>(s.provenance.producer),s.provenance.localSlot};
}
}
