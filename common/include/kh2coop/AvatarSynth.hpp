#pragma once
#include "kh2coop/Types.hpp"
#include "kh2coop/PuppetProvenance.hpp"
#include <cmath>
namespace kh2coop::avatarsynth {
inline bool ValidRoomOptions(bool worldSpecified, bool roomSpecified, unsigned world, unsigned room) noexcept {
    return worldSpecified==roomSpecified && world<=255 && room<=255;
}
inline AvatarState Circle(float t, const Vec3& center, float radius, std::uint32_t motion) {
    constexpr float omega=2.0f;
    const float angle=omega*t;
    AvatarState a;
    a.position={center.x+radius*std::cos(angle),center.y,center.z+radius*std::sin(angle)};
    a.velocity={-radius*omega*std::sin(angle),0.0f,radius*omega*std::cos(angle)};
    a.rotationY=std::atan2(a.velocity.x,a.velocity.z);
    a.motionId=motion;a.motionTime=t*60.0f;a.motionSpeed=1.0f;
    return a;
}
// Actual synth producer construction. Explicit room is a stream fact, never
// proof of native room; native admission independently verifies both facts.
struct Sample { std::uint32_t active {}; AvatarState pose {}; PuppetProvenance provenance {}; };
inline Sample Puppet(float t, const Vec3& center, float radius, std::uint32_t motion,
                         std::uint16_t world=0, std::uint16_t room=0) {
    Sample p;
    p.active=1;p.provenance.producer=PuppetProducer::Standalone;
    p.pose=Circle(t,center,radius,motion);
    p.pose.worldId=world;p.pose.roomId=room;
    return p;
}
}
