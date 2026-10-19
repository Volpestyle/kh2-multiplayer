// VUH-2480 crown prop: inflate, EGS package, model group decode, sprite, head read and clip
// triangles, over synthetic data only (no game files). The installed-asset decode was checked
// against the Python reference separately (docs/HOST_CROWN.md).
#include "CoopHudState.hpp"
#include "CrownInputs.hpp"
#include "kh2coop/CrownProp.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace kh2coop;
using namespace kh2coop::inject;

namespace {
int checks = 0, failures = 0;
void Check(bool yes, const char* name) {
    ++checks;
    if (!yes) ++failures;
    std::printf("%s %s\n", yes ? "PASS" : "FAIL", name);
}
std::vector<std::uint8_t> Hex(const std::string& hex) {
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) out.push_back(static_cast<std::uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    return out;
}
template <class T> void Put(std::vector<std::uint8_t>& b, std::size_t at, T v) {
    if (b.size() < at + sizeof(T)) b.resize(at + sizeof(T));
    std::memcpy(b.data() + at, &v, sizeof(T));
}
std::vector<std::uint8_t> Bytes(const char* s) { return {s, s + std::strlen(s)}; }

// zlib.compress(data, 9) in CPython 3.11: fixed-Huffman, dynamic-Huffman and stored blocks.
const char* kFixed = "78da4b2eca2fcf534846903a0a2519a90a19f9c5250ae5a98945c5602e58460f5919951503002ddd3121";
const char* kStored = "7801010c00f3ff73746f72656420626c6f636b1f8004bd";
const char* kDynamic =
    "78daedd4574c1451140660acd8c18e7db183d8b08b1decd8c10e8ae3acae65668555d68e1dec60c70e76b063073bd8b1831decd8c18e4f1a6f624e6e"
    "eecdffe4db26f334c9cdc9ff9f9ccfcebe48c9f2555ceb376ddda19bcfa0612673f0b4390b97456edeb1f770c2f92bb7ee3fcdf8f43d677ec7d215ab"
    "b93568deb6538fbe7ec347074e0c99b778c5bae85dfb8f9eba907cf7d1f3b7593f73172c56c650a34ea3169e5d7af51f621c6b993c336ce9aa0d5b77"
    "c79d387be946ea9397efbffcca5bb844b9ca2ef59ab46aefdd6760c0487dc2d4d90bc8a8f4d71fbfe5c8e750aa42d55aeecdda74ecee3b5819358e8c"
    "4aba76e7e1b337993f721528ea54a97aed861eed3af7ec4746c51e3c7ee6e2f594c72fde7dcece53a87859e79a751bb724a322d66cdabee750fcb9cb"
    "37efa5bdfaf0d5ee6f7232ca3a7deea2e56ba376ee3b7232f1eaed072c3919e5af8e099a342374c9caf55b620e1c3bcd9293515e5d7b0f183a421b3f"
    "65d6fcf0d51bb7b1e46494b064324a583219252c998c1296ccef932f99df275f32bf4fbe647e9f7cc9fc3ef992f97df225f3fbe44be6f7c997ccef93"
    "2f99df275f32bf4fbe64e1d190928547434a161e0d29597834a464e1d190928547434a161e0d29597834a464e1d190928547434a161e0d29592e134b"
    "2e97892597cbc492cb6562c9e532b1e472995872b94c2cb95c26965c2e134b2e97892597cbc49203fed301ff4980ff58c07f04e0df0af8f707fc7b01"
    "fe5d01ff8e80ff2cc07f2ae03f01f01f0df80f03fceb807f5fc0bf07e0df19f06f0ff8cf00fc2703fee300ff9180ff10c0bf11f0ef0df87707fc3b01"
    "feb301ff6980ff44c07f0ce03f1cf01f0cf8f703fc7b02fe5d00ff0e80ff4cc07f0ae03f1ef01f05f80f05fc6b807f1f994ce6405d371afe7c8aae69"
    "aa6231e99a9be1df4f4dd754ab29c8a26a8a4a7e9bd5004535d89eda9eda9eda9efeb7a7bf01be3c69ff";

void Inflate() {
    std::vector<std::uint8_t> out;
    std::string text;
    for (int i = 0; i < 3; ++i) text += "crown crown crown, the host wears the crown. ";
    Check(crownprop::inflate::Zlib(Hex(kFixed), text.size(), out) && std::string(out.begin(), out.end()) == text,
          "fixed-Huffman block inflates");
    Check(crownprop::inflate::Zlib(Hex(kStored), 12, out) && std::string(out.begin(), out.end()) == "stored block",
          "stored block inflates");
    std::vector<std::uint8_t> data;
    for (int i = 0; i < 3000; ++i) data.push_back(static_cast<std::uint8_t>((i * 7 + (i >> 5)) % 251));
    for (int i = 0; i < 20; ++i) {
        const auto phrase = Bytes("proof of connection, proof of nonexistence, proof of peace ");
        data.insert(data.end(), phrase.begin(), phrase.end());
    }
    auto dynamic = Hex(kDynamic);
    Check(crownprop::inflate::Zlib(dynamic, data.size(), out) && out == data, "dynamic-Huffman block inflates");
    Check(!crownprop::inflate::Zlib(dynamic, data.size() - 1, out), "a longer stream than expected fails");
    Check(!crownprop::inflate::Zlib(dynamic, data.size() + 1, out), "a shorter stream than expected fails");
    auto corrupt = dynamic;
    corrupt[300] ^= 0x10;
    Check(!crownprop::inflate::Zlib(corrupt, data.size(), out), "a corrupted stream fails (code or Adler-32)");
    auto badHeader = Hex(kFixed);
    badHeader[1] ^= 1;
    Check(!crownprop::inflate::Zlib(badHeader, text.size(), out), "a bad zlib header fails");
    auto truncated = Hex(kFixed);
    truncated.resize(truncated.size() - 6);
    Check(!crownprop::inflate::Zlib(truncated, text.size(), out), "a truncated stream fails");
}

std::array<std::uint8_t, 256> Table() {
    std::array<std::uint8_t, 256> t {};
    for (int i = 0; i < 256; ++i) t[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((i * 37 + 11) & 255);
    return t;
}
const std::array<std::uint8_t, 16> kSeed {0, 1, 2, 3, 0x40, 0, 0, 0, 0xFE, 0xFF, 0xFF, 0xFF, 9, 8, 7, 0};

void Egs() {
    const auto table = Table();
    const auto key = crownprop::ExpandKey(kSeed, table);
    // Expected from OpenKH's EgsEncryption.GenerateKey ported to Python, same table and seed.
    const auto round1 = Hex("320f345b720a325c8cf5cda385fdcaac"), round10 = Hex("c729bc213afd093c441e2316960783d6");
    Check(std::equal(round1.begin(), round1.end(), key.begin() + 16) && std::equal(round10.begin(), round10.end(), key.begin() + 160),
          "key expansion matches OpenKH");
    std::vector<std::uint8_t> data(0x140);
    for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<std::uint8_t>(i);
    auto enc = data;
    crownprop::Decrypt(enc, key);
    bool tail = true, changed = false;
    for (std::size_t i = 0x100; i < data.size(); ++i) tail = tail && enc[i] == data[i];
    for (std::size_t i = 0; i < 0x100; ++i) changed = changed || enc[i] != data[i];
    crownprop::Decrypt(enc, key);
    Check(changed && tail && enc == data, "only the first 0x100 bytes are XORed, and twice restores them");

    std::vector<std::uint8_t> hed(64);
    std::array<std::uint8_t, 16> md5 {};
    md5.fill(0xAB);
    std::memcpy(hed.data() + 32, md5.data(), 16);
    Put<std::uint64_t>(hed, 48, 0x1234567890ull);
    Put<std::int32_t>(hed, 56, 999);
    Put<std::int32_t>(hed, 60, 777);
    const auto entry = crownprop::FindHed(hed, md5);
    Check(entry && entry->offset == 0x1234567890ull && entry->packed == 999 && entry->actual == 777, "hed row found by md5");
    md5[0] = 0;
    Check(!crownprop::FindHed(hed, md5), "an absent md5 is not found");

    // A package asset: plain-but-encrypted original, one zlib remastered file.
    const auto original = Bytes("original asset bytes, sixteen+");
    const auto remastered = Hex(kStored);
    std::vector<std::uint8_t> blob(16 + 48);
    std::memcpy(blob.data(), kSeed.data(), 16);
    Put<std::int32_t>(blob, 0, static_cast<std::int32_t>(original.size()));
    Put<std::int32_t>(blob, 4, 1);
    Put<std::int32_t>(blob, 8, -1);
    const auto seed = std::span<const std::uint8_t>(blob).first<16>();
    std::array<std::uint8_t, 16> realSeed {};
    std::memcpy(realSeed.data(), seed.data(), 16);
    const auto assetKey = crownprop::ExpandKey(realSeed, table);
    std::memcpy(blob.data() + 16, "-0.dds", 6);
    Put<std::int32_t>(blob, 16 + 40, 12);
    Put<std::int32_t>(blob, 16 + 44, static_cast<std::int32_t>(remastered.size()));
    auto o = original;
    crownprop::Decrypt(o, assetKey);
    blob.insert(blob.end(), o.begin(), o.end());
    blob.resize((blob.size() + 15) / 16 * 16);
    auto r = remastered;
    crownprop::Decrypt(r, assetKey);
    blob.insert(blob.end(), r.begin(), r.end());
    blob.resize((blob.size() + 15) / 16 * 16);
    const auto asset = crownprop::ParseAsset(blob);
    std::vector<std::uint8_t> out;
    Check(asset && asset->remastered.size() == 1 && std::string(asset->remastered[0].name.data()) == "-0.dds",
          "asset header and remastered row parse");
    Check(asset && crownprop::Unpack(blob, asset->original, assetKey, out) && out == original, "original decrypts");
    Check(asset && crownprop::Unpack(blob, asset->remastered[0], assetKey, out) &&
          std::string(out.begin(), out.end()) == "stored block", "remastered file decrypts and inflates");
    blob.resize(blob.size() - 16);
    Check(!crownprop::ParseAsset(blob), "a truncated asset is refused");
}

// A model with a skipped group and a 2-triangle rigid group (mesh 1, part 1) on bone 1.
std::vector<std::uint8_t> Model(int weighted, bool twoBones) {
    constexpr std::size_t R = 0x90;
    std::vector<std::uint8_t> m(R + 32 + 64 + 0x120 + 128, 0);
    Put<std::int32_t>(m, R, 3);
    Put<std::uint16_t>(m, R + 16, 2);
    Put<std::int32_t>(m, R + 28, 2);
    std::size_t pos = m.size();
    // group 0: one NOP qword, start + end tag, matrices [0, -1]
    m.resize(pos + 16, 0);
    pos += 16;
    Put<std::int32_t>(m, R + 32 + 0, 0);
    Put<std::uint32_t>(m, R + 32 + 16, static_cast<std::uint32_t>(pos - R));
    Put<std::uint32_t>(m, R + 32 + 24, 2);
    Put<std::uint32_t>(m, pos, 1);
    Put<std::uint32_t>(m, pos + 16, 0);
    Put<std::int32_t>(m, pos + 32, 2);
    Put<std::int32_t>(m, pos + 36, 0);
    Put<std::int32_t>(m, pos + 40, -1);
    pos += 44;
    pos = (pos + 15) / 16 * 16;
    m.resize(pos, 0);
    // group 1: UNPACK V4-32 of a 13-qword VU image, then MSCAL
    std::vector<std::uint8_t> vu(13 * 16, 0);
    const std::int32_t header[16] {1, 0, 0, 0, 4, 4, 12, 0, 0, 0, weighted, 0, 4, 8, 0, 1};
    std::memcpy(vu.data(), header, sizeof(header));
    const std::int32_t nodes[4][4] {{0, 0, 0, 0x10}, {4096, 0, 1, 0x10}, {0, 4096, 2, 0x20}, {4096, 4096, 3, 0x30}};
    std::memcpy(vu.data() + 64, nodes, sizeof(nodes));
    const float verts[4][4] {{1, 2, 3, 0}, {4, 5, 6, 0}, {7, 8, 9, 0}, {10, 11, 12, 0}};
    std::memcpy(vu.data() + 128, verts, sizeof(verts));
    Put<std::int32_t>(vu, 192, 4);
    const std::size_t vif = pos;
    Put<std::uint32_t>(m, vif, (0x6Cu << 24) | (13u << 16));
    m.insert(m.end(), vu.begin(), vu.end());
    Put<std::uint32_t>(m, m.size(), 0x14u << 24);
    m.resize((m.size() + 15) / 16 * 16, 0);
    const std::size_t qwc = (m.size() - vif) / 16;
    const std::size_t dma = m.size();
    Put<std::int32_t>(m, R + 64 + 0, (1 << 11) | (1 << 6) | 2);
    Put<std::uint32_t>(m, R + 64 + 8, 2);
    Put<std::uint32_t>(m, R + 64 + 16, static_cast<std::uint32_t>(dma - R));
    Put<std::uint32_t>(m, R + 64 + 24, 2);
    Put<std::uint32_t>(m, dma, static_cast<std::uint32_t>(qwc));
    Put<std::uint32_t>(m, dma + 16, 0);
    Put<std::int32_t>(m, dma + 32, twoBones ? 3 : 2);
    Put<std::int32_t>(m, dma + 36, 1);
    Put<std::int32_t>(m, dma + 40, twoBones ? 5 : -1);
    if (twoBones) Put<std::int32_t>(m, dma + 44, -1);
    return m;
}

void Group() {
    const auto model = Model(0, false);
    const auto prop = crownprop::DecodeGroup(model, 1, 1, 2);
    Check(prop && prop->bone == 1 && prop->vertices.size() == 4 && prop->triangles.size() == 2,
          "the rigid group decodes past a skipped group");
    Check(prop && prop->vertices[3].x == 10.0f && prop->vertices[3].z == 12.0f && prop->vertices[1].u == 1.0f &&
          prop->vertices[2].v == 1.0f, "positions and 1/4096 UVs");
    Check(prop && prop->triangles[0] == std::array<std::uint16_t, 3> {2, 0, 1} &&
          prop->triangles[1] == std::array<std::uint16_t, 3> {3, 2, 1}, "strip functions give OpenKH's windings");
    Check(!crownprop::DecodeGroup(model, 1, 1, 3), "a polygon count that differs is refused");
    Check(!crownprop::DecodeGroup(model, 2, 1, 2), "an absent mesh/part is refused");
    Check(!crownprop::DecodeGroup(Model(1, false), 1, 1, 2), "a weighted group is refused");
    Check(!crownprop::DecodeGroup(Model(0, true), 1, 1, 2), "a group on two bones is refused");
    auto broken = model;
    broken.resize(broken.size() - 8);
    Check(!crownprop::DecodeGroup(broken, 1, 1, 2), "a truncated model is refused");
}

void TextureAndSprite() {
    std::vector<std::uint8_t> tex(16, 0);
    tex.insert(tex.end(), {'T', 'E', 'X', 'A'});
    Put<std::int32_t>(tex, tex.size(), 64);
    const std::uint16_t f[12] {3, 2, 0, 8, 154, 164, 4, 4, 0, 0, 64, 64};
    const std::size_t at = tex.size();
    tex.resize(at + 64, 0);
    std::memcpy(tex.data() + at, f, sizeof(f));
    Check(crownprop::HasCrownAnimation(tex), "the crown TEXA block is recognised");
    tex[at + 8] = 155;
    Check(!crownprop::HasCrownAnimation(tex), "another slot range is not the crown");

    std::vector<std::uint8_t> dds(128, 0);
    std::memcpy(dds.data(), "DDS ", 4);
    Put<std::uint32_t>(dds, 4, 124);
    Put<std::uint32_t>(dds, 12, 64);
    Put<std::uint32_t>(dds, 16, 16);
    Put<std::uint32_t>(dds, 76, 32);
    Put<std::uint32_t>(dds, 80, 0x41);
    Put<std::uint32_t>(dds, 88, 32);
    Put<std::uint32_t>(dds, 92, 0x00FF0000u);
    Put<std::uint32_t>(dds, 96, 0x0000FF00u);
    Put<std::uint32_t>(dds, 100, 0x000000FFu);
    Put<std::uint32_t>(dds, 104, 0xFF000000u);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 16; ++x) {
            const std::uint8_t sprite = static_cast<std::uint8_t>(y / 16);
            dds.insert(dds.end(), {static_cast<std::uint8_t>(10 + sprite), 20, 30, 40});  // B G R A
        }
    std::vector<std::uint8_t> rgba;
    int side = 0;
    Check(crownprop::DdsSprite(dds, 2, 4, rgba, side) && side == 16 && rgba.size() == 16 * 16 * 4 && rgba[0] == 30 &&
          rgba[1] == 20 && rgba[2] == 12 && rgba[3] == 40, "the gold sprite is cut out and swizzled to RGBA");
    Check(!crownprop::DdsSprite(dds, 4, 4, rgba, side), "a sprite past the sheet is refused");
    Put<std::uint32_t>(dds, 84, 0x31545844u);  // 'DXT1'
    Put<std::uint32_t>(dds, 80, 0x4);
    Check(!crownprop::DdsSprite(dds, 2, 4, rgba, side), "a compressed DDS is refused");
}

crownprop::Matrix Translation(float x, float y, float z) {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1};
}

void Clip() {
    const char sora[32] = "P_EX100";
    const char roxas[32] = "P_EX110";
    const char valor[32] = "P_EX100_BTLF";
    const char haloweenAnti[32] = "P_EX100_NM_HTLF";
    const char dualWield[32] = "P_EX110_BTLF";
    const char mickey[32] = "P_EX200";
    const char prefix[32] = "P_EX100_B";
    Check(crownprop::FindCharacter(sora)->headBone == 140 && crownprop::FindCharacter(roxas)->headBone == 113 &&
          crownprop::FindCharacter(valor)->headBone == 140 && crownprop::FindCharacter(haloweenAnti)->headBone == 140 &&
          crownprop::FindCharacter(dualWield)->headBone == 113, "Sora's forms and skins and both Roxases have rows");
    Check(!crownprop::FindCharacter(mickey) && !crownprop::FindCharacter(prefix), "other models match no row (marker)");

    // HeadWorld negates the bone's Y and Z columns, then applies the actor.
    const auto head = crownprop::HeadWorld(Translation(1, 2, 3), Translation(100, 0, 0));
    Check(head[12] == 101 && head[13] == -2 && head[14] == -3 && head[5] == -1 && head[10] == -1 && head[0] == 1,
          "head-bone world matrix follows the game's sign flip");

    // A view-projection whose w is z: two triangles at depths 50 and 200, one behind the camera.
    const crownprop::Matrix viewProj {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0};
    crownprop::Prop prop;
    prop.vertices = {{0, 0, 50, 0, 0}, {1, 0, 50, 1, 0}, {0, 1, 50, 0, 1},
                     {0, 0, 200, 0, 0}, {1, 0, 200, 1, 0}, {0, 1, 200, 0, 1},
                     {0, 0, -5, 0, 0}, {1, 0, -5, 1, 0}, {0, 1, -5, 0, 1}};
    prop.triangles = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}};
    const crownprop::Character who {"X", 0, 0, 0, 0};
    std::array<crownprop::ClipVertex, 12> out {};
    const auto n = crownprop::ClipTriangles(prop, who, Translation(0, 0, 0), viewProj, out);
    Check(n == 6, "a triangle behind the near plane is dropped");
    Check(out[0].w == 200 && out[3].w == 50, "far triangles come first");
    Check(out[5].y == -1 && out[5].z == 25 && out[5].v == 1, "clip y is flipped to D3D's up and z is half w");
    const crownprop::Character lifted {"X", 0, 0, 0, 100};
    std::array<crownprop::ClipVertex, 3> small {};
    Check(crownprop::ClipTriangles(prop, lifted, Translation(0, 0, 0), viewProj, small) == 3 && small[0].w == 300,
          "the character offset applies in bone space; output stops at capacity");
}

void HostHead() {
    // An owned image reaching the view-projection, an actor, its objentry, model and bone array.
    const std::size_t span = crownprop::ViewProjRva + 64;
    auto* image = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, span, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    std::vector<std::uint8_t> actor(0x1000), objentry(0x40), model(0x60), bones(0x40 * 141);
    Check(image != nullptr, "owned image allocated");
    if (!image) return;
    const crownprop::Matrix viewProj {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0};
    std::memcpy(image + crownprop::ViewProjRva, viewProj.data(), 64);
    const auto a = reinterpret_cast<std::uintptr_t>(actor.data());
    Put<std::uintptr_t>(actor, crownprop::ActorObjentry, reinterpret_cast<std::uintptr_t>(objentry.data()));
    Put<std::uintptr_t>(actor, crownprop::ActorModel, reinterpret_cast<std::uintptr_t>(model.data()));
    Put<std::uintptr_t>(model, crownprop::ModelBones, reinterpret_cast<std::uintptr_t>(bones.data()));
    const auto world = Translation(500, 0, 200);
    std::memcpy(actor.data() + crownprop::ActorWorld, world.data(), 64);
    const auto bone = Translation(0, 141, 0);
    std::memcpy(bones.data() + 0x40 * 113, bone.data(), 64);
    std::memcpy(objentry.data() + crownprop::ObjentryName, "P_EX110", 8);
    const auto base = reinterpret_cast<std::uintptr_t>(image);
    hud::Snapshot snap;
    Check(crown::ReadHostHead(base, a, snap) && snap.crownHeadValid && crownprop::Characters[snap.crownCharacter].model == "P_EX110" &&
          snap.crownHead[12] == 500 && snap.crownHead[13] == -141 && snap.crownViewProj[11] == 1,
          "Roxas's head bone 113 is read and placed above his feet (+Y down)");
    std::memcpy(objentry.data() + crownprop::ObjentryName, "B_EX100", 8);
    Check(!crown::ReadHostHead(base, a, snap) && !snap.crownHeadValid, "an unknown model has no crown mesh");
    std::memcpy(objentry.data() + crownprop::ObjentryName, "P_EX110", 8);
    const auto distant = Translation(0, 900, 0);
    std::memcpy(bones.data() + 0x40 * 113, distant.data(), 64);
    Check(!crown::ReadHostHead(base, a, snap), "a head far from the actor is refused");
    std::memcpy(bones.data() + 0x40 * 113, bone.data(), 64);
    auto gameLike = viewProj;  // the game's view x projection: w also carries the view translation
    gameLike[15] = 956.5f;
    std::memcpy(image + crownprop::ViewProjRva, gameLike.data(), 64);
    Check(crown::ReadHostHead(base, a, snap) && snap.crownViewProj[15] == 956.5f, "a view translation in w is accepted");
    std::memset(image + crownprop::ViewProjRva, 0, 64);
    Check(!crown::ReadHostHead(base, a, snap), "an empty view-projection is refused");
    Check(!crown::ReadHostHead(base, 0, snap), "no actor, no crown mesh");
    auto* reserved = VirtualAlloc(nullptr, 0x2000, MEM_RESERVE, PAGE_NOACCESS);
    Check(!crown::ReadHostHead(base, reinterpret_cast<std::uintptr_t>(reserved), snap) && !snap.crownHeadValid,
          "a faulting actor read returns normally without the mesh");
    VirtualFree(reserved, 0, MEM_RELEASE);
    VirtualFree(image, 0, MEM_RELEASE);
}

void Source() {
    hud::Input in;
    in.before = {PuppetAuthorityMode::Network, 1, 7, {101, 102, 103}};
    in.after = in.before;
    in.localAvailable = in.gameplayCurrent = in.fieldClear = in.cameraValid = in.cameraFollowsLocal = true;
    in.clearFrames = hud::CrownSettleFrames;
    in.local.worldId = 2;
    in.local.roomId = 7;
    auto& remote = in.remote[0];
    remote.active = true;
    remote.avatar = in.local;
    remote.avatar.ownerSlot = static_cast<SlotType>(0);
    remote.provenance = {PuppetProducer::Network, 1, 7, 101, 102, 101};
    auto snap = hud::Project(in, 1, 1);
    Check(snap.crownCurrent && snap.crownSource == 1, "a friend's crown comes from the host's remote (index 0)");
    in.before.localSlot = in.after.localSlot = 0;
    remote.active = false;
    snap = hud::Project(in, 1, 1);
    Check(snap.crownCurrent && snap.crownSource == 0, "the host's crown is their own actor");
    in.fieldClear = false;
    snap = hud::Project(in, 1, 1);
    Check(!snap.crownCurrent && snap.crownSource == -1, "no crown, no wearer");
}
}  // namespace

int main() {
    Inflate();
    Egs();
    Group();
    TextureAndSprite();
    Clip();
    HostHead();
    Source();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
