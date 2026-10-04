#pragma once
#include "kh2coop/NativeRecordContentTypes.hpp"
#include "kh2coop/Codec.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace kh2coop {


namespace native_record_detail {
// NoMatch is an output of a complete search, never an input coverage claim.
inline NativeRecordContentStatus inputFailure(NativeRecordContentStatus status) {
    switch (status) {
    case NativeRecordContentStatus::Unsupported:
    case NativeRecordContentStatus::Partial:
    case NativeRecordContentStatus::Unavailable:
    case NativeRecordContentStatus::Ambiguous: return status;
    default: return NativeRecordContentStatus::Unavailable;
    }
}
inline std::uint16_t u16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(std::uint16_t{p[0]} | (std::uint16_t{p[1]} << 8));
}
inline void bytes(ByteWriter& w, std::span<const std::uint8_t> value) {
    for (const auto b : value) w.writeU8(b);
}
template<std::size_t N> inline void domain(ByteWriter& w, const char (&value)[N]) {
    // Includes exactly the string literal's terminating NUL.
    for (const auto c : value) w.writeU8(static_cast<std::uint8_t>(c));
}
inline NativeRecordContentStatus validate(const NativeRecordContentDefinition& d) {
    if (d.records.size() > NativeRecordContentMaxRecords ||
        u16(d.header.data() + 4) > NativeRecordContentMaxRecords)
        return NativeRecordContentStatus::Unsupported;
    if (d.records.size() < u16(d.header.data() + 4)) return NativeRecordContentStatus::Partial;
    if (d.records.size() != u16(d.header.data() + 4) ||
        std::all_of(d.layoutSha256.begin(), d.layoutSha256.end(), [](auto b) { return b == 0; }))
        return NativeRecordContentStatus::Unavailable;
    return NativeRecordContentStatus::Complete;
}
inline std::array<std::uint8_t, 43> projection(const NativeRecordContentDefinition& d) {
    std::array<std::uint8_t, 43> result{};
    std::copy_n(d.header.begin(), 14, result.begin());
    std::copy(d.header.begin() + 15, d.header.end(), result.begin() + 14);
    return result;
}
// Descriptor equality uses exact ordered bytes, never caller-supplied hashes.
inline bool descriptorEqual(const NativeRecordContentDefinition& a,
                            const NativeRecordContentDefinition& b) {
    return a.layoutSha256 == b.layoutSha256 && a.location == b.location &&
        a.groupKey == b.groupKey && a.header[0] == b.header[0] &&
        u16(a.header.data() + 2) == u16(b.header.data() + 2) &&
        u16(a.header.data() + 4) == u16(b.header.data() + 4) && a.records == b.records;
}
} // namespace native_record_detail

struct NativeRecordContentRecord {
    std::uint16_t nativeIndex{}, rawId{};
    std::array<std::uint8_t, 32> recordSha256{}, comparisonSha256{};
    std::vector<std::uint8_t> encoding;
};
struct NativeRecordContentResult {
    NativeRecordContentStatus status{NativeRecordContentStatus::Unavailable};
    std::array<std::uint8_t, 43> headerProjection{};
    std::array<std::uint8_t, 32> arraySha256{}, comparisonSha256{};
    std::vector<std::uint8_t> encoding;
    std::vector<NativeRecordContentRecord> records;
};
// Caller retains the complete input header/records as evidence. This function
// neither edits nor removes the excluded header byte. Allocation errors follow
// the existing Codec exception convention; no partial result is returned.
inline NativeRecordContentResult BuildNativeRecordContent(const NativeRecordContentDefinition& d) {
    using namespace native_record_detail;
    NativeRecordContentResult result;
    result.status = validate(d);
    if (result.status != NativeRecordContentStatus::Complete) return result;
    result.headerProjection = projection(d);
    ByteWriter array;
    for (const auto& record : d.records) bytes(array, record);
    result.arraySha256 = desyncSha256(array.data());
    ByteWriter definition;
    domain(definition, "KH2OrdinaryArrayCompare/v1");
    bytes(definition, d.layoutSha256);
    definition.writeU16(d.location.world); definition.writeU16(d.location.room);
    definition.writeU16(d.location.door); definition.writeU16(d.location.mapProgram);
    definition.writeU16(d.location.battleProgram); definition.writeU16(d.location.eventProgram);
    definition.writeU32(d.groupKey); definition.writeU8(d.header[0]);
    definition.writeU16(u16(d.header.data() + 2)); definition.writeU16(u16(d.header.data() + 4));
    bytes(definition, result.arraySha256);
    result.encoding = definition.take();
    result.comparisonSha256 = desyncSha256(result.encoding);
    result.records.reserve(d.records.size());
    for (std::size_t i = 0; i < d.records.size(); ++i) {
        NativeRecordContentRecord record;
        record.nativeIndex = static_cast<std::uint16_t>(i); // validated <=256
        record.rawId = u16(d.records[i].data() + 0x1e);
        record.recordSha256 = desyncSha256(d.records[i]);
        ByteWriter encoded;
        domain(encoded, "KH2OrdinaryRecordCompare/v1");
        bytes(encoded, result.comparisonSha256); encoded.writeU16(record.nativeIndex);
        encoded.writeU16(record.rawId); bytes(encoded, record.recordSha256);
        record.encoding = encoded.take(); record.comparisonSha256 = desyncSha256(record.encoding);
        result.records.push_back(std::move(record));
    }
    return result;
}
struct NativeRecordContentComparison {
    NativeRecordContentStatus status{NativeRecordContentStatus::Unavailable};
    bool equal{};
};
inline NativeRecordContentComparison CompareNativeRecordContent(
    const NativeRecordContentDefinition& a, const NativeRecordContentDefinition& b) {
    for (const auto* d : {&a, &b}) {
        const auto status = native_record_detail::validate(*d);
        if (status != NativeRecordContentStatus::Complete) return {status, false};
    }
    return {NativeRecordContentStatus::Complete,
        native_record_detail::descriptorEqual(a, b) &&
        native_record_detail::projection(a) == native_record_detail::projection(b)};
}
struct NativeRecordContentCandidate {
    const NativeRecordContentDefinition* definition{}; // owned C++ evidence, not native memory
    NativeRecordContentStatus status{NativeRecordContentStatus::Unavailable};
    // Native layer must establish unique ordinary membership AND controller/array
    // association across its whole bounded scope. Content cannot establish this.
    bool associationUnique{};
};
struct NativeRecordContentResolution {
    NativeRecordContentStatus status{NativeRecordContentStatus::Unavailable};
    std::optional<std::size_t> definitionIndex;
    std::optional<std::uint16_t> recordIndex;
};
inline NativeRecordContentResolution ResolveNativeRecordContent(
    const NativeRecordContentDefinition& query,
    std::span<const NativeRecordContentCandidate> candidates,
    NativeRecordContentStatus catalogStatus) {
    using S = NativeRecordContentStatus;
    if (catalogStatus != S::Complete) return {native_record_detail::inputFailure(catalogStatus), {}, {}};
    const auto queryStatus = native_record_detail::validate(query);
    if (queryStatus != S::Complete) return {queryStatus, {}, {}};
    if (candidates.size() > NativeRecordContentMaxDefinitions) return {S::Unsupported, {}, {}};
    std::size_t total = 0;
    for (const auto& candidate : candidates) {
        if (candidate.status != S::Complete) return {native_record_detail::inputFailure(candidate.status), {}, {}};
        if (!candidate.definition) return {S::Unavailable, {}, {}};
        if (!candidate.associationUnique) return {S::Ambiguous, {}, {}};
        const auto status = native_record_detail::validate(*candidate.definition);
        if (status != S::Complete) return {status, {}, {}};
        if (candidate.definition->records.size() > NativeRecordContentMaxTotalRecords - total)
            return {S::Unsupported, {}, {}};
        total += candidate.definition->records.size();
    }
    // Complete-scope ambiguity is never reduced to the first match. No native
    // address is compared here; alias evidence must be supplied by the reader.
    std::vector<std::uint16_t> ids;
    ids.reserve(total);
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const auto& d = *candidates[i].definition;
        for (std::size_t j = 0; j < i; ++j)
            if (candidates[i].definition == candidates[j].definition ||
                native_record_detail::descriptorEqual(d, *candidates[j].definition))
                return {S::Ambiguous, {}, {}};
        for (const auto& record : d.records) ids.push_back(native_record_detail::u16(record.data() + 0x1e));
    }
    std::sort(ids.begin(), ids.end());
    if (std::adjacent_find(ids.begin(), ids.end()) != ids.end()) return {S::Ambiguous, {}, {}};
    std::optional<std::size_t> match;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (CompareNativeRecordContent(query, *candidates[i].definition).equal) {
            if (match) return {S::Ambiguous, {}, {}};
            match = i;
        }
    }
    if (!match) return {S::NoMatch, {}, {}};
    return {S::Complete, match, {}};
}
inline NativeRecordContentResolution ResolveNativeRecordContentRecord(
    const NativeRecordContentDefinition& query, std::size_t nativeRecordIndex,
    std::span<const NativeRecordContentCandidate> candidates,
    NativeRecordContentStatus catalogStatus) {
    if (nativeRecordIndex >= query.records.size() || nativeRecordIndex > 0xffffu)
        return {NativeRecordContentStatus::Unavailable, {}, {}};
    auto result = ResolveNativeRecordContent(query, candidates, catalogStatus);
    if (result.status == NativeRecordContentStatus::Complete)
        result.recordIndex = static_cast<std::uint16_t>(nativeRecordIndex);
    return result;
}
} // namespace kh2coop
