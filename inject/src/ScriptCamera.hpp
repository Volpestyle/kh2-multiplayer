#pragma once
#include <cstdint>
namespace kh2coop::inject::scriptmirror::rules {
// Kind11 is an entity camera, not a story scene. Exact installed evtp record:
// 0B 00 6C 00 00 00 00 00. The adapter additionally validates the admitted role.
struct EntityCameraIdentity {
    std::uintptr_t context{},sourceAnchor{},asset{};
    std::uint64_t kindRecord{};
};
inline bool SameEntityCamera(const EntityCameraIdentity& opened,
                             const EntityCameraIdentity& current) noexcept {
    return opened.context && opened.sourceAnchor && opened.asset &&
        opened.kindRecord==0x6C000Bull && opened.context==current.context &&
        opened.sourceAnchor==current.sourceAnchor && opened.asset==current.asset &&
        current.kindRecord==opened.kindRecord;
}
}
