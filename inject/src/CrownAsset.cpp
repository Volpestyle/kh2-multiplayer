// VUH-2480 — see CrownAsset.hpp.
#include "CrownAsset.hpp"

#include <Windows.h>
#include <bcrypt.h>

#include <atomic>
#include <cstring>
#include <fstream>
#include <thread>

namespace kh2coop::inject::crownasset {
namespace {

std::atomic<const Loaded*> g_ready {nullptr};
std::atomic<bool> g_started {false};

bool Hash(const wchar_t* algorithm, std::span<const std::uint8_t> data, std::uint8_t* out, ULONG size) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, algorithm, nullptr, 0) != 0) return false;
    const bool ok = BCryptHash(alg, nullptr, 0, const_cast<PUCHAR>(data.data()), static_cast<ULONG>(data.size()), out, size) == 0;
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

bool ReadFile(const std::wstring& path, std::uint64_t offset, std::size_t size, std::vector<std::uint8_t>& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    if (size == 0) {
        file.seekg(0, std::ios::end);
        const auto end = file.tellg();
        if (end <= 0 || end > (16 << 20)) return false;
        size = static_cast<std::size_t>(end);
    }
    out.resize(size);
    file.seekg(static_cast<std::streamoff>(offset));
    file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
    return static_cast<std::size_t>(file.gcount()) == size;
}

// The game's own EGS key table, read in place; a fault returns false.
bool ReadKeyTable(std::uintptr_t exeBase, std::uint8_t* table) {
    __try {
        std::memcpy(table, reinterpret_cast<const void*>(exeBase + crownprop::EgsKeyTableRva), 256);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

bool LoadFromInstall(const std::wstring& imageDir, std::span<const std::uint8_t, 256> keyTable, Loaded& out,
                     std::string& why) {
    std::uint8_t md5[16] {};
    const auto name = crownprop::AssetName;
    if (!Hash(BCRYPT_MD5_ALGORITHM, {reinterpret_cast<const std::uint8_t*>(name.data()), name.size()}, md5, 16)) {
        why = "md5 unavailable";
        return false;
    }
    WIN32_FIND_DATAW found {};
    HANDLE search = FindFirstFileW((imageDir + L"\\kh2_*.hed").c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) {
        why = "no kh2_*.hed";
        return false;
    }
    why = "asset not found";
    do {
        const std::wstring hedPath = imageDir + L"\\" + found.cFileName;
        std::vector<std::uint8_t> hed, blob, original, sheet;
        if (!ReadFile(hedPath, 0, 0, hed)) continue;
        const auto entry = crownprop::FindHed(hed, std::span<const std::uint8_t, 16>(md5, 16));
        if (!entry) continue;
        std::wstring pkgPath = hedPath.substr(0, hedPath.size() - 4) + L".pkg";
        if (entry->packed > (32 << 20) || !ReadFile(pkgPath, entry->offset, static_cast<std::size_t>(entry->packed), blob)) {
            why = "pkg read failed";
            break;
        }
        const auto asset = crownprop::ParseAsset(blob);
        if (!asset) { why = "asset header"; break; }
        const auto key = crownprop::ExpandKey(std::span<const std::uint8_t, 16>(asset->seed), keyTable);
        if (!crownprop::Unpack(blob, asset->original, key, original) ||
            original.size() != static_cast<std::size_t>(entry->actual)) { why = "original unpack"; break; }
        const auto model = crownprop::BarEntry(original, 4);
        const auto texture = crownprop::BarEntry(original, 7);
        if (!model || !texture) { why = "bar entries"; break; }
        auto prop = crownprop::DecodeGroup(*model, crownprop::CrownMesh, crownprop::CrownPart, crownprop::CrownPolygons);
        if (!prop || prop->bone != crownprop::CrownBone) { why = "crown group"; break; }
        if (!crownprop::HasCrownAnimation(*texture)) { why = "crown texture animation"; break; }
        bool haveSprite = false;
        for (const auto& section : asset->remastered) {
            std::vector<std::uint8_t> dds;
            if (!crownprop::Unpack(blob, section, key, dds)) continue;
            if (crownprop::DdsSprite(dds, crownprop::GoldSprite, 4, out.rgba, out.side)) { haveSprite = true; break; }
        }
        if (!haveSprite) { why = "crown sprites"; break; }
        out.prop = std::move(*prop);
        char narrow[MAX_PATH] {};
        WideCharToMultiByte(CP_UTF8, 0, found.cFileName, -1, narrow, sizeof(narrow), nullptr, nullptr);
        out.source = narrow;
        why.clear();
        FindClose(search);
        return true;
    } while (FindNextFileW(search, &found));
    FindClose(search);
    return false;
}

void Start(std::uintptr_t exeBase, LogFn log) {
    if (!exeBase || g_started.exchange(true)) return;
    std::thread([exeBase, log] {
        std::uint8_t table[256] {};
        std::uint8_t sha[32] {};
        if (!ReadKeyTable(exeBase, table) || !Hash(BCRYPT_SHA256_ALGORITHM, table, sha, 32) ||
            std::memcmp(sha, crownprop::EgsKeyTableSha256.data(), 32) != 0) {
            if (log) log("[crown-asset] unavailable: game key table not at the expected RVA; painted marker stays");
            return;
        }
        wchar_t exe[MAX_PATH] {};
        const DWORD n = GetModuleFileNameW(reinterpret_cast<HMODULE>(exeBase), exe, MAX_PATH);
        std::wstring dir(exe, n);
        dir = dir.substr(0, dir.find_last_of(L"\\/")) + L"\\Image\\dt";
        auto* loaded = new Loaded;  // published once, kept for the process lifetime
        std::string why;
        const ULONGLONG t0 = GetTickCount64();
        if (!LoadFromInstall(dir, table, *loaded, why)) {
            delete loaded;
            if (log) log("[crown-asset] unavailable: %s; painted marker stays", why.c_str());
            return;
        }
        if (log) log("[crown-asset] ready from %s: %zu vertices, %zu triangles, bone %d, sprite %dpx, %llums",
                     loaded->source.c_str(), loaded->prop.vertices.size(), loaded->prop.triangles.size(),
                     loaded->prop.bone, loaded->side, GetTickCount64() - t0);
        g_ready.store(loaded, std::memory_order_release);
    }).detach();
}

const Loaded* Ready() noexcept { return g_ready.load(std::memory_order_acquire); }

}  // namespace kh2coop::inject::crownasset
