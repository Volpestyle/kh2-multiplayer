#pragma once
// VUH-2480: KH2 Final Mix's own crown as a standalone prop, read from the player's installed
// game files at run time (we ship no game data). Pure: byte spans in, mesh and pixels out.
//
// Game data (docs/HOST_CROWN.md has the provenance):
//   obj/P_EX100.mdlx, BAR type 4 (model): skeletal group mesh 1 / part 1, 48 triangles, rigid on
//   bone 140 (Sora's PART_HEAD). Texture 2 is a 64x64 slot that the model's third TEXA block
//   fills with one of four sprites: bronze (slot 154), silver (155), gold (156), none (164).
//   The remastered asset carries those sprites as a 4w-tall uncompressed BGRA DDS.
//   The game picks the slot from how many of items 593-595 (the three Proofs) are held
//   (FUN_1403A9E20, table {164,154,155,156} at RVA 0x5C4CA0). We always draw the gold one.
// Packaging (OpenKH 7a3b945, MIT: OpenKh.Egs EgsHdAsset/EgsEncryption/Hed):
//   a .hed holds 32-byte rows {md5(name), u64 offset, i32 packed, i32 actual}; the .pkg asset at
//   offset starts with a 16-byte seed/header {decompressed, remastered count, compressed, date},
//   48-byte remastered rows {char name[32], offset, original offset, decompressed, compressed},
//   then the original data and each remastered file (16-aligned). compressed -2: plain;
//   -1: only the first 0x100 bytes XOR-encrypted; >=0: encrypted, then zlib.
//   The key expands the seed with a 256-byte table the game itself carries (RVA 0x713540).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace kh2coop::crownprop {

inline constexpr std::string_view AssetName = "obj/P_EX100.mdlx";
inline constexpr int CrownMesh = 1, CrownPart = 1, CrownPolygons = 48, CrownBone = 140;
inline constexpr int GoldSprite = 2;  // sprite order bronze, silver, gold, none
inline constexpr std::uint64_t EgsKeyTableRva = 0x713540;  // .data, build 15194255
inline constexpr std::array<std::uint8_t, 32> EgsKeyTableSha256 {
    0x75, 0x9b, 0x2f, 0x22, 0xf7, 0xe0, 0xbf, 0xd2, 0x57, 0x06, 0x3a, 0x01, 0xf3, 0xde, 0x0c, 0x08,
    0x35, 0x90, 0x52, 0x17, 0xaa, 0x92, 0x6d, 0xf6, 0xde, 0xf1, 0x34, 0xa1, 0x43, 0x13, 0x67, 0x2d};
inline constexpr std::uint64_t ViewProjRva = 0xAD6CF0;  // row-vector view x projection, this frame
inline constexpr std::uint64_t ActorModel = 0x7B0, ModelBones = 0x50, ActorWorld = 0x808;
inline constexpr std::uint64_t ActorObjentry = 0x918, ObjentryName = 0x08;

// ---- little-endian reads ------------------------------------------------------------------
template <class T> inline bool Get(std::span<const std::uint8_t> s, std::size_t at, T& out) noexcept {
    if (at > s.size() || s.size() - at < sizeof(T)) return false;
    std::memcpy(&out, s.data() + at, sizeof(T));
    return true;
}

// ---- zlib inflate (RFC 1950/1951), after puff.c ----------------------------------------------
namespace inflate {
struct Bits {
    std::span<const std::uint8_t> in;
    std::size_t pos {};
    std::uint32_t buf {};
    int count {};
    bool fail {};
    int Need(int n) noexcept {
        std::uint32_t v = buf;
        while (count < n) {
            if (pos >= in.size()) { fail = true; return 0; }
            v |= static_cast<std::uint32_t>(in[pos++]) << count;
            count += 8;
        }
        buf = v >> n;
        count -= n;
        return static_cast<int>(v & ((1u << n) - 1u));
    }
};
struct Huffman {
    std::array<std::int16_t, 16> count {};
    std::array<std::int16_t, 288> symbol {};
};
// 0: complete, > 0: incomplete, < 0: over-subscribed.
inline int Build(Huffman& h, const std::int16_t* length, int n) noexcept {
    h.count.fill(0);
    for (int s = 0; s < n; ++s) h.count[static_cast<std::size_t>(length[s])]++;
    if (h.count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len < 16; ++len) {
        left <<= 1;
        left -= h.count[static_cast<std::size_t>(len)];
        if (left < 0) return left;
    }
    std::array<std::int16_t, 16> offs {};
    for (int len = 1; len < 15; ++len)
        offs[static_cast<std::size_t>(len + 1)] = static_cast<std::int16_t>(offs[static_cast<std::size_t>(len)] + h.count[static_cast<std::size_t>(len)]);
    for (int s = 0; s < n; ++s)
        if (length[s]) h.symbol[static_cast<std::size_t>(offs[static_cast<std::size_t>(length[s])]++)] = static_cast<std::int16_t>(s);
    return left;
}
inline int Decode(Bits& b, const Huffman& h) noexcept {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; ++len) {
        code |= b.Need(1);
        if (b.fail) return -1;
        const int count = h.count[static_cast<std::size_t>(len)];
        if (code - count < first) return h.symbol[static_cast<std::size_t>(index + (code - first))];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}
inline bool Codes(Bits& b, std::vector<std::uint8_t>& out, const Huffman& lens, const Huffman& dists,
                  std::size_t limit) noexcept {
    static constexpr std::int16_t base[29] {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                           35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static constexpr std::int16_t extra[29] {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                            3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static constexpr std::int16_t dbase[30] {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                            257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
                                            8193, 12289, 16385, 24577};
    static constexpr std::int16_t dextra[30] {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                             7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    for (;;) {
        int symbol = Decode(b, lens);
        if (symbol < 0) return false;
        if (symbol < 256) {
            if (out.size() >= limit) return false;
            out.push_back(static_cast<std::uint8_t>(symbol));
        } else if (symbol == 256) {
            return true;
        } else {
            symbol -= 257;
            if (symbol >= 29) return false;
            const std::size_t len = static_cast<std::size_t>(base[symbol] + b.Need(extra[symbol]));
            const int d = Decode(b, dists);
            if (d < 0 || d >= 30 || b.fail) return false;
            const std::size_t dist = static_cast<std::size_t>(dbase[d] + b.Need(dextra[d]));
            if (b.fail || dist > out.size() || out.size() + len > limit) return false;
            const std::size_t from = out.size() - dist;
            for (std::size_t i = 0; i < len; ++i) out.push_back(out[from + i]);
        }
    }
}
inline bool Raw(std::span<const std::uint8_t> in, std::vector<std::uint8_t>& out, std::size_t limit,
                std::size_t* consumed = nullptr) noexcept {
    Bits b {in};
    int last = 0;
    do {
        last = b.Need(1);
        const int type = b.Need(2);
        if (b.fail) return false;
        if (type == 0) {
            b.buf = 0;
            b.count = 0;
            if (b.in.size() - b.pos < 4) return false;
            const std::size_t len = b.in[b.pos] | (static_cast<std::size_t>(b.in[b.pos + 1]) << 8);
            const std::size_t nlen = b.in[b.pos + 2] | (static_cast<std::size_t>(b.in[b.pos + 3]) << 8);
            b.pos += 4;
            if (len != (~nlen & 0xFFFFu) || b.in.size() - b.pos < len || out.size() + len > limit) return false;
            out.insert(out.end(), b.in.begin() + static_cast<std::ptrdiff_t>(b.pos),
                       b.in.begin() + static_cast<std::ptrdiff_t>(b.pos + len));
            b.pos += len;
        } else if (type == 1) {
            static const auto fixed = [] {
                std::array<Huffman, 2> h {};
                std::int16_t lengths[288];
                int s = 0;
                for (; s < 144; ++s) lengths[s] = 8;
                for (; s < 256; ++s) lengths[s] = 9;
                for (; s < 280; ++s) lengths[s] = 7;
                for (; s < 288; ++s) lengths[s] = 8;
                Build(h[0], lengths, 288);
                for (s = 0; s < 30; ++s) lengths[s] = 5;
                Build(h[1], lengths, 30);
                return h;
            }();
            if (!Codes(b, out, fixed[0], fixed[1], limit)) return false;
        } else if (type == 2) {
            static constexpr std::int16_t order[19] {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            const int nlen = b.Need(5) + 257, ndist = b.Need(5) + 1, ncode = b.Need(4) + 4;
            if (b.fail || nlen > 286 || ndist > 30) return false;
            std::int16_t lengths[320] {};
            for (int i = 0; i < ncode; ++i) lengths[order[i]] = static_cast<std::int16_t>(b.Need(3));
            Huffman lencode, distcode;
            if (b.fail || Build(lencode, lengths, 19) != 0) return false;
            int index = 0;
            while (index < nlen + ndist) {
                int symbol = Decode(b, lencode);
                if (symbol < 0) return false;
                if (symbol < 16) {
                    lengths[index++] = static_cast<std::int16_t>(symbol);
                    continue;
                }
                std::int16_t len = 0;
                if (symbol == 16) {
                    if (index == 0) return false;
                    len = lengths[index - 1];
                    symbol = 3 + b.Need(2);
                } else if (symbol == 17) {
                    symbol = 3 + b.Need(3);
                } else {
                    symbol = 11 + b.Need(7);
                }
                if (b.fail || index + symbol > nlen + ndist) return false;
                while (symbol--) lengths[index++] = len;
            }
            if (lengths[256] == 0) return false;
            int err = Build(lencode, lengths, nlen);
            if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1)) return false;
            err = Build(distcode, lengths + nlen, ndist);
            if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1)) return false;
            if (!Codes(b, out, lencode, distcode, limit)) return false;
        } else {
            return false;
        }
    } while (!last);
    if (consumed) *consumed = b.pos;
    return true;
}
inline std::uint32_t Adler32(std::span<const std::uint8_t> data) noexcept {
    std::uint32_t a = 1, b = 0;
    for (const auto byte : data) {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}
// A zlib stream that must inflate to exactly `expected` bytes with a matching Adler-32.
inline bool Zlib(std::span<const std::uint8_t> in, std::size_t expected, std::vector<std::uint8_t>& out) noexcept {
    out.clear();
    if (in.size() < 6 || (in[0] & 0x0F) != 8 || (in[0] >> 4) > 7 || ((in[0] << 8) | in[1]) % 31 != 0 || (in[1] & 0x20))
        return false;
    out.reserve(expected);
    std::size_t used = 0;
    if (!Raw(in.subspan(2), out, expected, &used) || out.size() != expected) return false;
    const std::size_t at = 2 + used;
    if (in.size() < at + 4) return false;
    const std::uint32_t want = (static_cast<std::uint32_t>(in[at]) << 24) | (static_cast<std::uint32_t>(in[at + 1]) << 16) |
                               (static_cast<std::uint32_t>(in[at + 2]) << 8) | in[at + 3];
    return want == Adler32(out);
}
}  // namespace inflate

// ---- EGS package ------------------------------------------------------------------------------
using Key = std::array<std::uint8_t, 176>;
inline Key ExpandKey(std::span<const std::uint8_t, 16> seed, std::span<const std::uint8_t, 256> table) noexcept {
    static constexpr std::uint8_t rcon[10] {1, 2, 4, 8, 16, 32, 64, 128, 27, 54};
    Key key {};
    for (std::size_t i = 0; i < 16; ++i) key[i] = seed[i] ? seed[i] : static_cast<std::uint8_t>(i);
    for (std::size_t i = 0; i < 40; ++i) {
        std::uint8_t f[4] {key[12 + 4 * i], key[13 + 4 * i], key[14 + 4 * i], key[15 + 4 * i]};
        if (i % 4 == 0) {
            // OpenKH scrambles with a 40-byte array whose only non-zero bytes are rcon at multiples of 4.
            const std::uint8_t r = rcon[i / 4];
            const std::uint8_t g[4] {static_cast<std::uint8_t>(table[f[1]] ^ r), table[f[2]], table[f[3]], table[f[0]]};
            std::memcpy(f, g, 4);
        }
        for (std::size_t j = 0; j < 4; ++j) key[16 + 4 * i + j] = static_cast<std::uint8_t>(key[4 * i + j] ^ f[j]);
    }
    return key;
}
// XOR the first 0x100 bytes with all eleven round keys (its own inverse).
inline void Decrypt(std::span<std::uint8_t> data, const Key& key) noexcept {
    const std::size_t n = (std::min)(data.size(), std::size_t {0x100});
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < 11; ++j) data[i] ^= key[16 * j + i % 16];
}

struct HedEntry {
    std::uint64_t offset {};
    std::int32_t packed {}, actual {};
};
inline std::optional<HedEntry> FindHed(std::span<const std::uint8_t> hed, std::span<const std::uint8_t, 16> md5) noexcept {
    for (std::size_t at = 0; at + 32 <= hed.size(); at += 32) {
        if (std::memcmp(hed.data() + at, md5.data(), 16) != 0) continue;
        HedEntry e;
        Get(hed, at + 16, e.offset);
        Get(hed, at + 24, e.packed);
        Get(hed, at + 28, e.actual);
        if (e.packed <= 16 || e.actual <= 0) return std::nullopt;
        return e;
    }
    return std::nullopt;
}

struct Section {
    std::array<char, 33> name {};  // "" for the original asset
    std::int32_t size {}, compressed {};
    std::size_t at {}, length {};
};
struct Asset {
    std::array<std::uint8_t, 16> seed {};
    Section original;
    std::vector<Section> remastered;
};
inline std::optional<Asset> ParseAsset(std::span<const std::uint8_t> blob) {
    Asset a;
    if (blob.size() < 16) return std::nullopt;
    std::memcpy(a.seed.data(), blob.data(), 16);
    std::int32_t count = 0;
    Get(blob, 0, a.original.size);
    Get(blob, 4, count);
    Get(blob, 8, a.original.compressed);
    if (a.original.size <= 0 || count < 0 || count > 64 || a.original.compressed < -2) return std::nullopt;
    std::size_t pos = 16 + 48 * static_cast<std::size_t>(count);
    a.original.at = pos;
    a.original.length = static_cast<std::size_t>(a.original.compressed >= 0 ? a.original.compressed : a.original.size);
    if (pos > blob.size() || blob.size() - pos < a.original.length) return std::nullopt;
    pos += a.original.length;
    for (std::int32_t k = 0; k < count; ++k) {
        Section s;
        const std::size_t row = 16 + 48 * static_cast<std::size_t>(k);
        std::memcpy(s.name.data(), blob.data() + row, 32);
        Get(blob, row + 40, s.size);
        Get(blob, row + 44, s.compressed);
        if (s.size <= 0 || s.compressed < -2) return std::nullopt;
        std::size_t n = static_cast<std::size_t>(s.compressed >= 0 ? s.compressed : s.size);
        n += (16 - n % 16) % 16;
        pos += (16 - pos % 16) % 16;
        if (pos > blob.size() || blob.size() - pos < n) return std::nullopt;
        s.at = pos;
        s.length = n;
        pos += n;
        a.remastered.push_back(s);
    }
    return a;
}
inline bool Unpack(std::span<const std::uint8_t> blob, const Section& s, const Key& key, std::vector<std::uint8_t>& out) {
    std::vector<std::uint8_t> data(blob.begin() + static_cast<std::ptrdiff_t>(s.at),
                                   blob.begin() + static_cast<std::ptrdiff_t>(s.at + s.length));
    if (s.compressed > -2) Decrypt(data, key);
    if (s.compressed >= 0) return inflate::Zlib(std::span(data).first(static_cast<std::size_t>(s.compressed)),
                                                static_cast<std::size_t>(s.size), out);
    if (data.size() < static_cast<std::size_t>(s.size)) return false;
    out.assign(data.begin(), data.begin() + s.size);
    return true;
}

// ---- BAR ----------------------------------------------------------------------------------------
inline std::optional<std::span<const std::uint8_t>> BarEntry(std::span<const std::uint8_t> bar, std::uint16_t type) noexcept {
    std::int32_t count = 0;
    if (bar.size() < 16 || std::memcmp(bar.data(), "BAR\x01", 4) != 0 || !Get(bar, 4, count) || count < 0) return std::nullopt;
    for (std::int32_t i = 0; i < count; ++i) {
        std::uint16_t t = 0;
        std::int32_t offset = 0, size = 0;
        const std::size_t row = 16 + 16 * static_cast<std::size_t>(i);
        if (!Get(bar, row, t) || !Get(bar, row + 8, offset) || !Get(bar, row + 12, size)) return std::nullopt;
        if (t != type) continue;
        if (offset < 0 || size <= 0 || static_cast<std::size_t>(offset) > bar.size() ||
            bar.size() - static_cast<std::size_t>(offset) < static_cast<std::size_t>(size)) return std::nullopt;
        return bar.subspan(static_cast<std::size_t>(offset), static_cast<std::size_t>(size));
    }
    return std::nullopt;
}

// ---- skeletal model group (OpenKH ModelSkeletal, VifUnpacker, VpuPacket) ----------------------
struct Vertex {
    float x {}, y {}, z {};  // bone-local
    float u {}, v {};
};
struct Prop {
    std::vector<Vertex> vertices;  // one per strip node
    std::vector<std::array<std::uint16_t, 3>> triangles;
    int bone {-1};
};

// VIF1 unpack into a VU memory image (skipping writes for masked lanes, as OpenKH does).
inline bool VifUnpack(std::span<const std::uint8_t> code, std::vector<std::uint8_t>& mem) noexcept {
    mem.assign(16 * 1024, 0);
    std::size_t pc = 0;
    std::uint8_t mask[4] {};
    auto read = [&](int bytes, bool sign, std::uint32_t& v) {
        if (pc > code.size() || code.size() - pc < static_cast<std::size_t>(bytes)) return false;
        std::uint32_t raw = 0;
        std::memcpy(&raw, code.data() + pc, static_cast<std::size_t>(bytes));
        pc += static_cast<std::size_t>(bytes);
        if (sign && bytes == 1) raw = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(raw)));
        if (sign && bytes == 2) raw = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(raw)));
        v = raw;
        return true;
    };
    while (pc < code.size()) {
        std::uint32_t op = 0;
        if (!read(4, false, op)) return false;
        const std::uint32_t cmd = (op >> 24) & 0x7F, num = (op >> 16) & 0xFF;
        if ((cmd & 0x60) == 0x60) {
            std::size_t dest = (op & 0x1FF) * 16;
            const bool sign = (op & 0x400) != 0;
            const int vl = static_cast<int>((op >> 24) & 3), vn = static_cast<int>((op >> 26) & 3);
            const bool useMask = ((op >> 28) & 1) != 0;
            const int bytes = vl == 0 ? 4 : (vl == 2 ? 1 : 2);
            for (std::uint32_t n = 0; n < num; ++n) {
                const std::uint8_t m = useMask ? mask[n & 3] : 0;
                std::uint32_t vals[4] {};
                bool write[4] {};
                for (int k = 0; k < 4; ++k) write[k] = ((m >> (2 * k)) & 3) == 0;
                if (vn == 0) {
                    if (!read(bytes, sign, vals[0])) return false;
                    vals[1] = vals[2] = vals[3] = vals[0];
                } else if (vn == 1) {
                    if (!read(bytes, sign, vals[0]) || !read(bytes, sign, vals[1])) return false;
                    vals[2] = vals[0];
                    vals[3] = vals[1];
                } else if (vn == 2) {
                    for (int k = 0; k < 3; ++k)
                        if (write[k] && !read(bytes, sign, vals[k])) return false;
                    if (write[3]) {
                        const std::size_t save = pc;
                        if (!read(bytes, sign, vals[3])) return false;
                        pc = save;
                    }
                } else {
                    for (int k = 0; k < 4; ++k)
                        if (write[k] && !read(bytes, sign, vals[k])) return false;
                }
                for (int k = 0; k < 4; ++k) {
                    if (write[k]) {
                        if (dest + 4 > mem.size()) return false;
                        std::memcpy(mem.data() + dest, &vals[k], 4);
                    }
                    dest += 4;
                }
            }
            pc = (pc + 3) & ~std::size_t {3};
        } else if (cmd == 0x00 || cmd == 0x01) {
        } else if (cmd == 0x14 || cmd == 0x17) {
            return true;  // MSCAL / MSCNT: the packet's data is complete
        } else if (cmd == 0x20) {
            std::uint32_t v = 0;
            if (!read(4, false, v)) return false;
            for (int k = 0; k < 4; ++k) mask[k] = static_cast<std::uint8_t>(v >> (8 * k));
        } else if (cmd == 0x30 || cmd == 0x31) {
            if (code.size() - pc < 16) return false;
            pc += 16;
        } else {
            return false;
        }
    }
    return true;
}

// The rigid single-bone group with this mesh/part. Fails on weighted vertices or several bones.
inline std::optional<Prop> DecodeGroup(std::span<const std::uint8_t> model, int mesh, int part, int polygons) {
    constexpr std::size_t R = 0x90;
    std::int32_t type = 0, groupCount = 0;
    std::uint16_t boneCount = 0;
    if (!Get(model, R, type) || type != 3 || !Get(model, R + 16, boneCount) || !Get(model, R + 28, groupCount) ||
        groupCount <= 0 || groupCount > 64) return std::nullopt;
    std::size_t pos = R + 32 + 32 * static_cast<std::size_t>(groupCount) + 0x120 + 64 * static_cast<std::size_t>(boneCount);
    for (std::int32_t g = 0; g < groupCount; ++g) {
        std::int32_t attributes = 0;
        std::uint32_t polys = 0, dmaOffset = 0, packets = 0;
        const std::size_t row = R + 32 + 32 * static_cast<std::size_t>(g);
        if (!Get(model, row, attributes) || !Get(model, row + 8, polys) || !Get(model, row + 16, dmaOffset) ||
            !Get(model, row + 24, packets) || packets > 4096) return std::nullopt;
        pos = (pos + 15) & ~std::size_t {15};
        const std::size_t dma = R + dmaOffset;
        if (dma < pos || dma > model.size()) return std::nullopt;
        const auto vif = model.subspan(pos, dma - pos);
        pos = dma + 16 * static_cast<std::size_t>(packets);
        std::int32_t matrixCount = 0;
        if (!Get(model, pos, matrixCount) || matrixCount < 0 || matrixCount > 4096) return std::nullopt;
        const std::size_t matrices = pos + 4;
        pos = matrices + 4 * static_cast<std::size_t>(matrixCount);
        if (pos > model.size()) return std::nullopt;
        if (((attributes >> 11) & 31) != mesh || ((attributes >> 6) & 31) != part) continue;
        if (static_cast<int>(polys) != polygons) return std::nullopt;

        // DMA: each packet is a start tag (qwc = VIF length) followed by an end tag (qwc 0).
        std::vector<std::size_t> qwc;
        bool open = false;
        for (std::uint32_t i = 0; i < packets; ++i) {
            std::uint32_t tag = 0;
            Get(model, dma + 16 * i, tag);
            if (!open) { qwc.push_back(tag & 0xFFFF); open = true; }
            else if ((tag & 0xFFFF) == 0) open = false;
        }
        std::vector<std::vector<int>> bones(qwc.size());
        std::size_t packet = 0;
        for (std::int32_t i = 0; i < matrixCount; ++i) {
            std::int32_t b = 0;
            Get(model, matrices + 4 * static_cast<std::size_t>(i), b);
            if (b == -1) { ++packet; continue; }
            if (packet >= bones.size()) return std::nullopt;
            bones[packet].push_back(b);
        }
        Prop prop;
        std::size_t off = 0;
        std::vector<std::uint8_t> mem;
        for (std::size_t p = 0; p < qwc.size(); ++p) {
            if (off + qwc[p] * 16 > vif.size() || !VifUnpack(vif.subspan(off, qwc[p] * 16), mem)) return std::nullopt;
            off += qwc[p] * 16;
            const std::span<const std::uint8_t> vu(mem);
            std::int32_t h[16] {};
            for (int i = 0; i < 16; ++i) Get(vu, 4 * static_cast<std::size_t>(i), h[i]);
            const std::int32_t nodes = h[4], nodeOff = h[5], weighted = h[10], vcount = h[12],
                               vOff = h[13], ranges = h[15];
            if (weighted != 0 || nodes < 0 || vcount <= 0 || ranges <= 0 || bones[p].size() != static_cast<std::size_t>(ranges))
                return std::nullopt;
            for (const int b : bones[p]) {
                if (prop.bone == -1) prop.bone = b;
                if (b != prop.bone) return std::nullopt;
            }
            for (std::int32_t i = 0; i < nodes; ++i) {
                std::int32_t n[4] {};
                for (int k = 0; k < 4; ++k) Get(vu, 16 * static_cast<std::size_t>(nodeOff + i) + 4 * static_cast<std::size_t>(k), n[k]);
                if (n[2] < 0 || n[2] >= vcount) return std::nullopt;
                float xyz[3] {};
                for (int k = 0; k < 3; ++k) Get(vu, 16 * static_cast<std::size_t>(vOff + n[2]) + 4 * static_cast<std::size_t>(k), xyz[k]);
                prop.vertices.push_back({xyz[0], xyz[1], xyz[2], static_cast<float>(n[0]) / 4096.0f, static_cast<float>(n[1]) / 4096.0f});
                const std::size_t at = prop.vertices.size() - 1;
                if (at >= 2) {  // strips run on across packets, as in OpenKH's mesh builder
                    const auto a = static_cast<std::uint16_t>(at), b = static_cast<std::uint16_t>(at - 1),
                               c = static_cast<std::uint16_t>(at - 2);
                    if (n[3] == 0x20 || n[3] == 0x00) prop.triangles.push_back({a, c, b});
                    if (n[3] == 0x30 || n[3] == 0x00) prop.triangles.push_back({a, b, c});
                }
            }
        }
        if (prop.triangles.size() != static_cast<std::size_t>(polygons)) return std::nullopt;
        for (const auto& v : prop.vertices)
            if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) || std::fabs(v.x) > 1000.0f ||
                std::fabs(v.y) > 1000.0f || std::fabs(v.z) > 1000.0f) return std::nullopt;
        return prop;
    }
    return std::nullopt;
}

// The crown's TEXA block in the model texture (BAR type 7): texture 2, slots 154..164, four 64x64 sprites.
inline bool HasCrownAnimation(std::span<const std::uint8_t> texture) noexcept {
    for (std::size_t at = 0; at + 8 + 40 <= texture.size(); ++at) {
        if (std::memcmp(texture.data() + at, "TEXA", 4) != 0) continue;
        std::uint16_t f[12] {};
        for (int i = 0; i < 12; ++i) Get(texture, at + 8 + 2 * static_cast<std::size_t>(i), f[i]);
        if (f[1] == 2 && f[4] == 154 && f[5] == 164 && f[7] == 4 && f[10] == 64 && f[11] == 64) return true;
    }
    return false;
}

// One square sprite (RGBA8) from an uncompressed 32-bit BGRA DDS stacked w x (count*w).
inline bool DdsSprite(std::span<const std::uint8_t> dds, int index, int count, std::vector<std::uint8_t>& rgba, int& side) {
    std::uint32_t size = 0, height = 0, width = 0, flags = 0, bits = 0, r = 0, g = 0, b = 0, a = 0;
    if (dds.size() < 128 || std::memcmp(dds.data(), "DDS ", 4) != 0 || !Get(dds, 4, size) || size != 124 ||
        !Get(dds, 12, height) || !Get(dds, 16, width) || !Get(dds, 80, flags) || !Get(dds, 88, bits) ||
        !Get(dds, 92, r) || !Get(dds, 96, g) || !Get(dds, 100, b) || !Get(dds, 104, a)) return false;
    if (!(flags & 0x40) || (flags & 0x4) || bits != 32 || r != 0x00FF0000u || g != 0x0000FF00u || b != 0x000000FFu ||
        a != 0xFF000000u || width < 16 || width > 2048 || height != width * static_cast<std::uint32_t>(count) ||
        index < 0 || index >= count) return false;
    const std::size_t rowBytes = static_cast<std::size_t>(width) * 4, start = 128 + rowBytes * width * static_cast<std::size_t>(index);
    if (dds.size() < 128 + rowBytes * height) return false;
    side = static_cast<int>(width);
    rgba.resize(rowBytes * width);
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        rgba[i + 0] = dds[start + i + 2];
        rgba[i + 1] = dds[start + i + 1];
        rgba[i + 2] = dds[start + i + 0];
        rgba[i + 3] = dds[start + i + 3];
    }
    return true;
}

// ---- per character ------------------------------------------------------------------------------
// The crown is authored in Sora's head-bone space. A row maps a gameplay model (objentry name)
// to its PART_HEAD bone and an offset in that bone's space. Every row below has exactly Sora's
// bind head frame (0, 141.2, -3.7, same axes): Sora's drive forms (Valor, Wisdom, Master, Final,
// Anti, Limit) and his Halloween Town, Space Paranoids, Timeless River and Christmas Town skins
// and their forms on bone 140 (the forms carry this crown group themselves), Roxas and
// dual-wield Roxas on bone 113. So every offset is zero. Other models (Mickey P_EX200 has a
// different, smaller head frame) keep the painted marker until a row is verified.
struct Character {
    std::string_view model;
    std::uint16_t headBone;
    float dx, dy, dz;
};
inline constexpr std::array<Character, 37> Characters {{
    {"P_EX100", 140, 0.0f, 0.0f, 0.0f},
    {"P_EX100_BTLF", 140, 0.0f, 0.0f, 0.0f},
    {"P_EX100_MAGF", 140, 0.0f, 0.0f, 0.0f},
    {"P_EX100_TRIF", 140, 0.0f, 0.0f, 0.0f},
    {"P_EX100_ULTF", 140, 0.0f, 0.0f, 0.0f},
    {"P_EX100_HTLF", 140, 0.0f, 0.0f, 0.0f},
    {"P_EX100_KH1F", 140, 0.0f, 0.0f, 0.0f},
    {"P_EX100_NM", 140, 0.0f, 0.0f, 0.0f},
    {"P_EX100_NM_BTLF", 140, 0.0f, 0.0f, 0.0f},
    {"P_EX100_NM_MAGF", 140, 0.0f, 0.0f, 0.0f},
    {"P_EX100_NM_TRIF", 140, 0.0f, 0.0f, 0.0f},
    {"P_EX100_NM_ULTF", 140, 0.0f, 0.0f, 0.0f},
    {"P_EX100_NM_HTLF", 140, 0.0f, 0.0f, 0.0f},
    {"P_EX100_NM_KH1F", 140, 0.0f, 0.0f, 0.0f},
    {"P_EX100_TR", 140, 0.0f, 0.0f, 0.0f},
    {"P_EX100_TR_BTLF", 140, 0.0f, 0.0f, 0.0f},
    {"P_EX100_TR_MAGF", 140, 0.0f, 0.0f, 0.0f},
    {"P_EX100_TR_TRIF", 140, 0.0f, 0.0f, 0.0f},
    {"P_EX100_TR_ULTF", 140, 0.0f, 0.0f, 0.0f},
    {"P_EX100_TR_HTLF", 140, 0.0f, 0.0f, 0.0f},
    {"P_EX100_TR_KH1F", 140, 0.0f, 0.0f, 0.0f},
    {"P_EX100_WI", 140, 0.0f, 0.0f, 0.0f},
    {"P_EX100_WI_BTLF", 140, 0.0f, 0.0f, 0.0f},
    {"P_EX100_WI_MAGF", 140, 0.0f, 0.0f, 0.0f},
    {"P_EX100_WI_TRIF", 140, 0.0f, 0.0f, 0.0f},
    {"P_EX100_WI_ULTF", 140, 0.0f, 0.0f, 0.0f},
    {"P_EX100_WI_HTLF", 140, 0.0f, 0.0f, 0.0f},
    {"P_EX100_WI_KH1F", 140, 0.0f, 0.0f, 0.0f},
    {"P_EX100_XM", 140, 0.0f, 0.0f, 0.0f},
    {"P_EX100_XM_BTLF", 140, 0.0f, 0.0f, 0.0f},
    {"P_EX100_XM_MAGF", 140, 0.0f, 0.0f, 0.0f},
    {"P_EX100_XM_TRIF", 140, 0.0f, 0.0f, 0.0f},
    {"P_EX100_XM_ULTF", 140, 0.0f, 0.0f, 0.0f},
    {"P_EX100_XM_HTLF", 140, 0.0f, 0.0f, 0.0f},
    {"P_EX100_XM_KH1F", 140, 0.0f, 0.0f, 0.0f},
    {"P_EX110", 113, 0.0f, 0.0f, 0.0f},
    {"P_EX110_BTLF", 113, 0.0f, 0.0f, 0.0f},
}};
inline const Character* FindCharacter(std::span<const char> name) noexcept {
    const auto end = std::find(name.begin(), name.end(), '\0');
    const std::string_view model(name.data(), static_cast<std::size_t>(end - name.begin()));
    for (const auto& c : Characters)
        if (c.model == model) return &c;
    return nullptr;
}

// ---- drawing --------------------------------------------------------------------------------------
// Row-vector 4x4 (m[r*4+c]), as the game stores matrices.
using Matrix = std::array<float, 16>;
inline Matrix Multiply(const Matrix& a, const Matrix& b) noexcept {
    Matrix out {};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += a[static_cast<std::size_t>(r * 4 + k)] * b[static_cast<std::size_t>(k * 4 + c)];
            out[static_cast<std::size_t>(r * 4 + c)] = sum;
        }
    return out;
}
// The game's head-bone world matrix: model-space bone matrix with its Y and Z columns negated
// (FUN_1401A7AF0 scales them by -1.0) times the actor's world matrix (FUN_1403B50F0).
inline Matrix HeadWorld(Matrix bone, const Matrix& actorWorld) noexcept {
    for (int r = 0; r < 4; ++r) {
        bone[static_cast<std::size_t>(r * 4 + 1)] = -bone[static_cast<std::size_t>(r * 4 + 1)];
        bone[static_cast<std::size_t>(r * 4 + 2)] = -bone[static_cast<std::size_t>(r * 4 + 2)];
    }
    return Multiply(bone, actorWorld);
}
inline bool Finite(const Matrix& m) noexcept {
    for (const float f : m)
        if (!std::isfinite(f)) return false;
    return true;
}

struct ClipVertex {
    float x {}, y {}, z {}, w {};
    float u {}, v {};
};
inline constexpr float NearW = 10.0f;
// Triangles in D3D clip space (y up), far to near for unsorted alpha blending. Triangles with a
// vertex at or behind the near plane are dropped. Returns the vertex count written (3 per triangle).
inline std::size_t ClipTriangles(const Prop& prop, const Character& who, const Matrix& head, const Matrix& viewProj,
                                 std::span<ClipVertex> out) {
    const Matrix toClip = Multiply(head, viewProj);
    std::vector<ClipVertex> clip(prop.vertices.size());
    for (std::size_t i = 0; i < prop.vertices.size(); ++i) {
        const auto& v = prop.vertices[i];
        const float p[4] {v.x + who.dx, v.y + who.dy, v.z + who.dz, 1.0f};
        float q[4] {};
        for (int c = 0; c < 4; ++c)
            for (int k = 0; k < 4; ++k) q[c] += p[k] * toClip[static_cast<std::size_t>(k * 4 + c)];
        // The game's projection has screen y growing with world +Y (down) and z equal to w.
        clip[i] = {q[0], -q[1], 0.5f * q[3], q[3], v.u, v.v};
    }
    struct Order {
        float depth;
        std::size_t index;
    };
    std::vector<Order> order;
    for (std::size_t t = 0; t < prop.triangles.size(); ++t) {
        const auto& tri = prop.triangles[t];
        bool ok = true;
        float depth = 0.0f;
        for (const auto i : tri) {
            const auto& c = clip[i];
            ok = ok && std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.w) && c.w > NearW;
            depth += c.w;
        }
        if (ok) order.push_back({depth, t});
    }
    std::stable_sort(order.begin(), order.end(), [](const Order& a, const Order& b) { return a.depth > b.depth; });
    std::size_t n = 0;
    for (const auto& o : order) {
        if (n + 3 > out.size()) break;
        for (const auto i : prop.triangles[o.index]) out[n++] = clip[i];
    }
    return n;
}

}  // namespace kh2coop::crownprop
