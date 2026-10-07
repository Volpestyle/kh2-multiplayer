#include "kh2coop/Codec.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>

using namespace kh2coop;

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    std::cout << (ok ? "PASS: " : "FAIL: ") << what << '\n';
    if (!ok) ++failures;
}

StateHash roundTrip(const StateHash& state) {
    const auto packet = encode(state);
    const std::uint8_t* payload = nullptr;
    std::size_t size = 0;
    check(decodePacketHeader(packet.data(), packet.size(), payload, size) == PacketType::StateHash &&
          size == 25 && packet.size() == 28, "StateHash v10 has exact 25-byte payload");
    ByteReader reader(payload, size);
    StateHash decoded;
    read(reader, decoded);
    check(reader.atEnd(), "roundtrip consumes every field");
    return decoded;
}
}

int main() {
    check(PROTOCOL_VERSION >= 10 && DesyncMissingEnemies == 8,
          "new wire version and missing-enemies bit are explicit");
    const StateHash legacyAggregate{7, 4, 26, 123, 456};
    check(!legacyAggregate.nativeCensusComplete && legacyAggregate.nativeLivingCount == 0 &&
          legacyAggregate.nativeCombatCount == 0,
          "five-field aggregate remains unavailable, not complete-empty");
    const auto defaults = roundTrip(legacyAggregate);
    check(!defaults.nativeCensusComplete && defaults.nativeLivingCount == 0 && defaults.nativeCombatCount == 0,
          "default completeness stays unavailable on the wire");

    const auto nonzero = roundTrip({7, 4, 26, 123, 456, true, 0x12345678, 0x23456789});
    check(nonzero.epoch == 7 && nonzero.worldId == 4 && nonzero.roomId == 26 &&
          nonzero.enemiesHash == 123 && nonzero.progressHash == 456 &&
          nonzero.nativeCensusComplete && nonzero.nativeLivingCount == 0x12345678 &&
          nonzero.nativeCombatCount == 0x23456789,
          "nonzero uint32 living/combat counts and existing fields roundtrip");
    const auto empty = roundTrip({7, 4, 26, 123, 456, true, 0, 0});
    check(empty.nativeCensusComplete && empty.nativeLivingCount == 0 && empty.nativeCombatCount == 0,
          "complete-empty is distinct from absent/default summary");
    const auto dead = roundTrip({7, 4, 26, 123, 456, true, 0, 1});
    check(dead.nativeCensusComplete && dead.nativeLivingCount == 0 && dead.nativeCombatCount == 1,
          "dead/dying native row is distinct from complete combat-empty");
    const auto unavailable = roundTrip({7, 4, 26, 123, 456, false, 5, 6});
    check(!unavailable.nativeCensusComplete && unavailable.nativeLivingCount == 5 && unavailable.nativeCombatCount == 6,
          "counts alone never imply completeness");
    const auto largest = roundTrip({7, 4, 26, 123, 456, true,
        std::numeric_limits<std::uint32_t>::max(), std::numeric_limits<std::uint32_t>::max()});
    check(largest.nativeLivingCount == std::numeric_limits<std::uint32_t>::max() &&
          largest.nativeCombatCount == std::numeric_limits<std::uint32_t>::max(),
          "codec preserves full uint32 counts without truncation");

    ByteWriter writer;
    write(writer, StateHash{7, 4, 26, 123, 456, true, 5, 6});
    auto payload = writer.data();
    check(payload[16] == 1 && payload[17] == 5 && payload[18] == 0 &&
          payload[19] == 0 && payload[20] == 0 && payload[21] == 6 && payload[22] == 0 &&
          payload[23] == 0 && payload[24] == 0,
          "summary is one canonical bool plus two little-endian uint32 counts");
    for (std::size_t length = 0; length < payload.size(); ++length) {
        StateHash decoded;
        bool rejected = false;
        try { ByteReader reader(payload.data(), length); read(reader, decoded); }
        catch (const std::runtime_error&) { rejected = true; }
        check(rejected && !decoded.nativeCensusComplete && decoded.nativeLivingCount == 0 &&
              decoded.nativeCombatCount == 0 && decoded.epoch == 0,
              "truncated/old payload cannot publish a partial or complete summary");
    }
    payload[16] = 2;
    StateHash decoded;
    bool rejected = false;
    try { ByteReader reader(payload); read(reader, decoded); }
    catch (const std::runtime_error&) { rejected = true; }
    check(rejected && !decoded.nativeCensusComplete && decoded.nativeLivingCount == 0 && decoded.nativeCombatCount == 0,
          "noncanonical completeness byte fails closed");

    ByteWriter noticeWriter;
    write(noticeWriter, DesyncNotice{SlotType::Friend1, 7, DesyncMissingEnemies});
    ByteReader noticeReader(noticeWriter.data());
    DesyncNotice notice;
    read(noticeReader, notice);
    check(noticeReader.atEnd() && notice.fields == 8 && notice.slot == SlotType::Friend1,
          "missing-enemies bit survives notice codec");
    std::cout << "failures=" << failures << '\n';
    return failures ? 1 : 0;
}
