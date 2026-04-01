#pragma once
// ============================================================================
// AOB Pattern Scanner — header-only
//
// Scans the .text section of the loaded PE module for a byte pattern.
// Used to locate function addresses at runtime for version resilience.
//
// Usage:
//   auto addr = PatternScan(exeBase, "40 53 48 83 EC 30 ?? 8B D9");
//   if (addr) { /* found */ }
// ============================================================================

#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>

namespace kh2coop {
namespace inject {

struct SectionInfo {
    uintptr_t start;
    size_t    size;
};

// Find the .text section of a loaded PE module by parsing headers.
inline std::optional<SectionInfo> FindTextSection(uintptr_t moduleBase) {
    static constexpr unsigned char kTextName[IMAGE_SIZEOF_SHORT_NAME] = {
        '.', 't', 'e', 'x', 't', '\0', '\0', '\0'
    };

    auto dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(moduleBase);
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE)
        return std::nullopt;

    auto ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
        moduleBase + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE)
        return std::nullopt;
    if (ntHeaders->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return std::nullopt;

    auto section = IMAGE_FIRST_SECTION(ntHeaders);
    for (WORD i = 0; i < ntHeaders->FileHeader.NumberOfSections; ++i, ++section) {
        if (std::memcmp(section->Name, kTextName, sizeof(kTextName)) == 0) {
            const auto virtualSize = static_cast<size_t>(section->Misc.VirtualSize);
            const auto rawSize = static_cast<size_t>(section->SizeOfRawData);
            return SectionInfo{
                moduleBase + section->VirtualAddress,
                virtualSize != 0 ? std::max(virtualSize, rawSize) : rawSize
            };
        }
    }
    return std::nullopt;
}

// Scan .text section for a byte pattern with mask.
// mask[i] == 0xFF → must match; mask[i] == 0x00 → wildcard.
inline std::optional<uintptr_t> PatternScan(
    uintptr_t moduleBase,
    const uint8_t* pattern,
    const uint8_t* mask,
    size_t patternLen)
{
    auto textSection = FindTextSection(moduleBase);
    if (!textSection || patternLen == 0) return std::nullopt;
    if (textSection->size < patternLen) return std::nullopt;

    const uint8_t* start = reinterpret_cast<const uint8_t*>(textSection->start);
    const uint8_t* end   = start + textSection->size - patternLen;

    for (const uint8_t* p = start; p <= end; ++p) {
        bool match = true;
        for (size_t i = 0; i < patternLen; ++i) {
            if (mask[i] == 0xFF && p[i] != pattern[i]) {
                match = false;
                break;
            }
        }
        if (match) return reinterpret_cast<uintptr_t>(p);
    }
    return std::nullopt;
}

// Parse a hex string pattern like "40 53 48 ?? EC 30" into bytes + mask,
// then scan. '??' means wildcard.
inline std::optional<uintptr_t> PatternScan(
    uintptr_t moduleBase,
    const char* patternStr)
{
    uint8_t pattern[256];
    uint8_t mask[256];
    size_t  len = 0;

    auto hexVal = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
        if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(10 + c - 'A');
        if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(10 + c - 'a');
        return 0;
    };

    const char* p = patternStr;
    while (*p && len < sizeof(pattern)) {
        while (*p == ' ') ++p;
        if (*p == '\0') break;

        if (*p == '?') {
            pattern[len] = 0x00;
            mask[len]    = 0x00;
            ++p;
            if (*p == '?') ++p;
        } else {
            pattern[len] = static_cast<uint8_t>((hexVal(p[0]) << 4) | hexVal(p[1]));
            mask[len]    = 0xFF;
            p += 2;
        }
        ++len;
    }

    if (len == 0) return std::nullopt;
    return PatternScan(moduleBase, pattern, mask, len);
}

} // namespace inject
} // namespace kh2coop
