// SceneCrown — see SceneCrown.hpp.
#include "SceneCrown.hpp"

#include <Windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>

#include <MinHook.h>

#include <array>
#include <atomic>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace kh2coop::inject::scenecrown {
namespace {

// ID3D12Device / ID3D12GraphicsCommandList vtable slots.
constexpr int kCreateGraphicsPipelineState = 10, kCreateDepthStencilView = 21;
constexpr int kReset = 10, kDrawInstanced = 12, kDrawIndexedInstanced = 13, kSetPipelineState = 25, kExecuteBundle = 27,
              kSetGraphicsRootSignature = 30, kSetGraphicsRootDescriptorTable = 32, kSetGraphicsRoot32BitConstant = 34,
              kSetGraphicsRoot32BitConstants = 36, kSetGraphicsRootConstantBufferView = 38,
              kSetGraphicsRootShaderResourceView = 40, kSetGraphicsRootUnorderedAccessView = 42, kOMSetRenderTargets = 46;

LogFn g_log = nullptr;
VerticesFn g_vertices = nullptr;
TextureFn g_texture = nullptr;
std::atomic<std::uint64_t> g_drawn {0};
bool g_active = false;  // every hook installed
static_assert(sizeof(crownprop::ClipVertex) == 24, "the shader reads float4 pos, float2 uv");
thread_local bool t_ours = false;  // our own calls into the game's list skip tracking

struct PsoInfo { bool depthTest, depthWrite; DXGI_FORMAT rtv, dsv; };
struct Arg { std::uint8_t kind; std::uint64_t value; };  // kind 1 descriptor table, 2 root CBV
constexpr UINT kMaxRoot = 32;
struct ListState {
    bool bundle = false;
    ID3D12RootSignature* root = nullptr;
    bool rootSet = false;
    std::array<Arg, kMaxRoot> args {};
    bool foreign = false;  // a root binding this file does not restore (constants, root SRV/UAV)
    ID3D12PipelineState* pso = nullptr;
    // Bundles: the pipeline of the first draw.
    bool drew = false;
    ID3D12PipelineState* firstPso = nullptr;
    // Direct lists.
    std::uint64_t dsv = 0;    // bound depth view
    std::uint64_t scene = 0;  // depth view of the first full-size depth-writing bundle
    DXGI_FORMAT sceneRtv = DXGI_FORMAT_UNKNOWN, sceneDsv = DXGI_FORMAT_UNKNOWN;
    bool inserted = false;
};
SRWLOCK g_lock = SRWLOCK_INIT;
std::unordered_map<const void*, PsoInfo> g_psos;
std::unordered_map<std::uint64_t, UINT64> g_dsvWidth;  // depth view CPU handle -> resource width
std::unordered_map<const void*, ListState> g_lists;
constexpr UINT64 kSceneMinWidth = 1024;  // the characters' 256x256 pass has its own depth

struct Lock {
    Lock() { AcquireSRWLockExclusive(&g_lock); }
    ~Lock() { ReleaseSRWLockExclusive(&g_lock); }
};

// ---- our draw -----------------------------------------------------------------------------------
constexpr char kShader[] = R"(
struct Vtx { float4 pos; float2 uv; };
StructuredBuffer<Vtx> verts : register(t0);
StructuredBuffer<uint> texels : register(t1);
cbuffer C : register(b0) { uint side; };
struct P { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
P VS(uint id : SV_VertexID) { P p; Vtx v = verts[id]; p.pos = v.pos; p.uv = v.uv; return p; }
float4 Texel(int2 i) {
    i = clamp(i, 0, int(side) - 1);
    uint c = texels[i.y * side + i.x];
    return float4(c & 255, (c >> 8) & 255, (c >> 16) & 255, c >> 24) / 255.0;
}
float4 PS(P p) : SV_Target {
    float2 t = p.uv * side - 0.5;
    int2 i = int2(floor(t));
    float2 f = t - floor(t);
    float4 c = lerp(lerp(Texel(i), Texel(i + int2(1, 0)), f.x), lerp(Texel(i + int2(0, 1)), Texel(i + int2(1, 1)), f.x), f.y);
    clip(c.a - 0.02);
    return c;
}
)";
constexpr std::size_t kMaxVertices = 3 * 64, kRingSlots = 8;
constexpr UINT64 kSlotBytes = (kMaxVertices * sizeof(crownprop::ClipVertex) + 255) & ~UINT64(255);
struct Gpu {
    ID3D12Device* device = nullptr;
    bool failed = false;
    std::vector<std::uint8_t> vs, ps;
    ID3D12RootSignature* root = nullptr;
    ID3D12Resource* texels = nullptr;
    UINT side = 0;
    ID3D12Resource* ring = nullptr;
    std::uint8_t* ringPtr = nullptr;
    std::uint64_t next = 0;
    struct Pipeline { DXGI_FORMAT rtv, dsv; ID3D12PipelineState* pso; };
    std::vector<Pipeline> pipelines;
};
Gpu g_gpu;  // guarded by g_lock

bool Fail(const char* step, HRESULT hr) {
    g_gpu.failed = true;
    if (g_log) g_log("[crown-scene] unavailable at %s (%08lX); the Present-time crown stays", step, hr);
    return false;
}

ID3D12Resource* UploadBuffer(UINT64 bytes) {
    D3D12_HEAP_PROPERTIES heap {};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC desc {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* out = nullptr;
    return SUCCEEDED(g_gpu.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                           D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&out)))
        ? out : nullptr;
}

// Device objects, built once on the game's device (g_lock held).
bool EnsureGpu(ID3D12Device* device) {
    auto& g = g_gpu;
    if (g.failed) return false;
    if (g.device == device) return true;
    if (g.device) return false;  // a second device: not ours to follow
    g.device = device;
    using CompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT,
                                       ID3DBlob**, ID3DBlob**);
    HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
    const auto compile = compiler ? reinterpret_cast<CompileFn>(GetProcAddress(compiler, "D3DCompile")) : nullptr;
    if (!compile) return Fail("d3dcompiler_47", E_NOINTERFACE);
    for (int stage = 0; stage < 2; ++stage) {
        ID3DBlob* code = nullptr;
        ID3DBlob* errors = nullptr;
        const HRESULT hr = compile(kShader, sizeof(kShader) - 1, "crown-scene", nullptr, nullptr, stage ? "PS" : "VS",
                                   stage ? "ps_5_0" : "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
        if (errors) errors->Release();
        if (FAILED(hr) || !code) return Fail(stage ? "pixel shader" : "vertex shader", hr);
        const auto* bytes = static_cast<const std::uint8_t*>(code->GetBufferPointer());
        (stage ? g.ps : g.vs).assign(bytes, bytes + code->GetBufferSize());
        code->Release();
    }
    D3D12_ROOT_PARAMETER params[3] {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[1].Descriptor.ShaderRegister = 1;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[2].Constants.Num32BitValues = 1;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC desc {};
    desc.NumParameters = 3;
    desc.pParameters = params;
    ID3DBlob* blob = nullptr;
    ID3DBlob* errors = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    if (errors) errors->Release();
    if (FAILED(hr) || !blob) return Fail("root signature", hr);
    hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&g.root));
    blob->Release();
    if (FAILED(hr)) return Fail("CreateRootSignature", hr);

    Texture texture {};
    if (!g_texture || !g_texture(texture) || !texture.rgba || texture.side <= 0) return Fail("crown texture", E_FAIL);
    g.side = static_cast<UINT>(texture.side);
    const UINT64 texelBytes = UINT64(g.side) * g.side * 4;
    std::uint8_t* mapped = nullptr;
    const D3D12_RANGE none {0, 0};
    if (!(g.texels = UploadBuffer(texelBytes))) return Fail("texel buffer", E_OUTOFMEMORY);
    if (FAILED(hr = g.texels->Map(0, &none, reinterpret_cast<void**>(&mapped)))) return Fail("texel map", hr);
    std::memcpy(mapped, texture.rgba, static_cast<std::size_t>(texelBytes));
    g.texels->Unmap(0, nullptr);
    if (!(g.ring = UploadBuffer(kSlotBytes * kRingSlots))) return Fail("vertex ring", E_OUTOFMEMORY);
    if (FAILED(hr = g.ring->Map(0, &none, reinterpret_cast<void**>(&g.ringPtr)))) return Fail("vertex ring map", hr);
    if (g_log) g_log("[crown-scene] ready on device %p (texture %ux%u)", static_cast<void*>(device), g.side, g.side);
    return true;
}

ID3D12PipelineState* Pipeline(DXGI_FORMAT rtv, DXGI_FORMAT dsv) {
    auto& g = g_gpu;
    for (const auto& p : g.pipelines)
        if (p.rtv == rtv && p.dsv == dsv) return p.pso;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso {};
    pso.pRootSignature = g.root;
    pso.VS = {g.vs.data(), g.vs.size()};
    pso.PS = {g.ps.data(), g.ps.size()};
    auto& blend = pso.BlendState.RenderTarget[0];  // as the Present-time crown
    blend.BlendEnable = TRUE;
    blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ZERO;
    blend.DestBlendAlpha = D3D12_BLEND_ONE;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.LogicOp = D3D12_LOGIC_OP_NOOP;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN | D3D12_COLOR_WRITE_ENABLE_BLUE;
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;  // the strip carries both windings itself
    pso.RasterizerState.DepthClipEnable = TRUE;
    pso.DepthStencilState.DepthEnable = TRUE;
    pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;  // the effects after it test against it
    pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;  // the game's convention: 1.0 is far
    pso.DSVFormat = dsv;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = rtv;
    pso.SampleDesc.Count = 1;
    ID3D12PipelineState* out = nullptr;
    const HRESULT hr = g.device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&out));
    if (FAILED(hr)) { Fail("pipeline", hr); return nullptr; }
    g.pipelines.push_back({rtv, dsv, out});
    if (g_log) g_log("[crown-scene] pipeline rtv=%u dsv=%u", unsigned(rtv), unsigned(dsv));
    return out;
}

// Records the crown into the game's direct list `l` and puts the game's root bindings back (g_lock held).
void InsertOurs(ID3D12GraphicsCommandList* l, ListState& s, const char* where);
void Insert(ID3D12GraphicsCommandList* l, ListState& s, const char* where) {
    s.inserted = true;  // one attempt per recording
    if (!g_active || !g_vertices || !s.root || s.foreign || s.sceneRtv == DXGI_FORMAT_UNKNOWN) return;
    t_ours = true;  // our device and list calls below must not re-enter the tracking (g_lock is held)
    InsertOurs(l, s, where);
    t_ours = false;
}
void InsertOurs(ID3D12GraphicsCommandList* l, ListState& s, const char* where) {
    crownprop::ClipVertex vertices[kMaxVertices];
    const std::size_t n = g_vertices(vertices, kMaxVertices);
    if (!n) return;
    ID3D12Device* device = nullptr;
    if (FAILED(l->GetDevice(IID_PPV_ARGS(&device))) || !device) return;
    const bool ready = EnsureGpu(device);
    device->Release();
    ID3D12PipelineState* pso = ready ? Pipeline(s.sceneRtv, s.sceneDsv) : nullptr;
    if (!pso) return;
    auto& g = g_gpu;
    const UINT64 offset = (g.next++ % kRingSlots) * kSlotBytes;
    std::memcpy(g.ringPtr + offset, vertices, n * sizeof(crownprop::ClipVertex));
    l->SetGraphicsRootSignature(g.root);
    l->SetPipelineState(pso);
    l->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    l->SetGraphicsRootShaderResourceView(0, g.ring->GetGPUVirtualAddress() + offset);
    l->SetGraphicsRootShaderResourceView(1, g.texels->GetGPUVirtualAddress());
    l->SetGraphicsRoot32BitConstant(2, g.side, 0);
    l->DrawInstanced(static_cast<UINT>(n), 1, 0, 0);
    l->SetGraphicsRootSignature(s.root);
    for (UINT i = 0; i < kMaxRoot; ++i) {
        const auto& a = s.args[i];
        if (a.kind == 1) l->SetGraphicsRootDescriptorTable(i, D3D12_GPU_DESCRIPTOR_HANDLE {a.value});
        else if (a.kind == 2) l->SetGraphicsRootConstantBufferView(i, a.value);
    }
    if (s.pso) l->SetPipelineState(s.pso);
    // The topology stays a triangle list: KH2's bundles set their own before every draw.
    const auto drawn = g_drawn.fetch_add(1) + 1;
    if (drawn == 1 && g_log) g_log("[crown-scene] first crown in the game's frame (%s, %zu vertices)", where, n);
}

// ---- hooks ----------------------------------------------------------------------------------------
using CreatePsoFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, const D3D12_GRAPHICS_PIPELINE_STATE_DESC*, REFIID, void**);
using CreateDsvFn = void(STDMETHODCALLTYPE*)(ID3D12Device*, ID3D12Resource*, const D3D12_DEPTH_STENCIL_VIEW_DESC*,
                                             D3D12_CPU_DESCRIPTOR_HANDLE);
using ResetFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12CommandAllocator*, ID3D12PipelineState*);
using DrawFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, UINT, UINT, UINT);
using DrawIndexedFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, UINT, UINT, INT, UINT);
using SetPsoFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12PipelineState*);
using BundleFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12GraphicsCommandList*);
using SetRootFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12RootSignature*);
using SetTableFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_DESCRIPTOR_HANDLE);
using SetConstantFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, UINT, UINT);
using SetConstantsFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, UINT, const void*, UINT);
using SetViewFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
using SetTargetsFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, BOOL,
                                              const D3D12_CPU_DESCRIPTOR_HANDLE*);
CreatePsoFn o_createPso;
CreateDsvFn o_createDsv;
ResetFn o_reset;
DrawFn o_draw;
DrawIndexedFn o_drawIndexed;
SetPsoFn o_setPso;
BundleFn o_bundle;
SetRootFn o_setRoot;
SetTableFn o_setTable;
SetConstantFn o_setConstant;
SetConstantsFn o_setConstants;
SetViewFn o_setCbv, o_setSrv, o_setUav;
SetTargetsFn o_setTargets;

HRESULT STDMETHODCALLTYPE CreatePso(ID3D12Device* d, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, REFIID riid, void** out) {
    const HRESULT hr = o_createPso(d, desc, riid, out);
    if (!t_ours && SUCCEEDED(hr) && desc && out && *out) {
        Lock lock;
        g_psos[*out] = {desc->DepthStencilState.DepthEnable != FALSE,
                        desc->DepthStencilState.DepthEnable && desc->DepthStencilState.DepthWriteMask == D3D12_DEPTH_WRITE_MASK_ALL,
                        desc->NumRenderTargets ? desc->RTVFormats[0] : DXGI_FORMAT_UNKNOWN, desc->DSVFormat};
    }
    return hr;
}
void STDMETHODCALLTYPE CreateDsv(ID3D12Device* d, ID3D12Resource* r, const D3D12_DEPTH_STENCIL_VIEW_DESC* desc,
                                 D3D12_CPU_DESCRIPTOR_HANDLE h) {
    o_createDsv(d, r, desc, h);
    if (t_ours || !r) return;
    const UINT64 width = r->GetDesc().Width;
    Lock lock;
    g_dsvWidth[h.ptr] = width;
}
HRESULT STDMETHODCALLTYPE Reset(ID3D12GraphicsCommandList* l, ID3D12CommandAllocator* a, ID3D12PipelineState* p) {
    if (!t_ours) {
        const bool bundle = l->GetType() == D3D12_COMMAND_LIST_TYPE_BUNDLE;
        Lock lock;
        auto& s = g_lists[l];
        s = {};
        s.bundle = bundle;
        s.pso = p;
    }
    return o_reset(l, a, p);
}
void Drew(ID3D12GraphicsCommandList* l) {
    Lock lock;
    const auto it = g_lists.find(l);
    if (it != g_lists.end() && it->second.bundle && !it->second.drew) { it->second.drew = true; it->second.firstPso = it->second.pso; }
}
void STDMETHODCALLTYPE Draw(ID3D12GraphicsCommandList* l, UINT v, UINT n, UINT s, UINT i) {
    if (!t_ours) Drew(l);
    o_draw(l, v, n, s, i);
}
void STDMETHODCALLTYPE DrawIndexed(ID3D12GraphicsCommandList* l, UINT v, UINT n, UINT s, INT b, UINT i) {
    if (!t_ours) Drew(l);
    o_drawIndexed(l, v, n, s, b, i);
}
void STDMETHODCALLTYPE SetPso(ID3D12GraphicsCommandList* l, ID3D12PipelineState* p) {
    if (!t_ours) { Lock lock; g_lists[l].pso = p; }
    o_setPso(l, p);
}
void STDMETHODCALLTYPE SetRoot(ID3D12GraphicsCommandList* l, ID3D12RootSignature* r) {
    if (!t_ours) {
        Lock lock;
        auto& s = g_lists[l];
        if (r != s.root) s.args = {};  // a different signature drops the bindings
        s.root = r;
        s.rootSet = true;
    }
    o_setRoot(l, r);
}
void SetArg(ID3D12GraphicsCommandList* l, UINT index, std::uint8_t kind, std::uint64_t value) {
    Lock lock;
    auto& s = g_lists[l];
    if (index < kMaxRoot) s.args[index] = {kind, value};
    else s.foreign = true;
}
void STDMETHODCALLTYPE SetTable(ID3D12GraphicsCommandList* l, UINT i, D3D12_GPU_DESCRIPTOR_HANDLE h) {
    if (!t_ours) SetArg(l, i, 1, h.ptr);
    o_setTable(l, i, h);
}
void STDMETHODCALLTYPE SetCbv(ID3D12GraphicsCommandList* l, UINT i, D3D12_GPU_VIRTUAL_ADDRESS va) {
    if (!t_ours) SetArg(l, i, 2, va);
    o_setCbv(l, i, va);
}
void Foreign(ID3D12GraphicsCommandList* l) { Lock lock; g_lists[l].foreign = true; }
void STDMETHODCALLTYPE SetConstant(ID3D12GraphicsCommandList* l, UINT i, UINT v, UINT o) {
    if (!t_ours) Foreign(l);
    o_setConstant(l, i, v, o);
}
void STDMETHODCALLTYPE SetConstants(ID3D12GraphicsCommandList* l, UINT i, UINT n, const void* v, UINT o) {
    if (!t_ours) Foreign(l);
    o_setConstants(l, i, n, v, o);
}
void STDMETHODCALLTYPE SetSrv(ID3D12GraphicsCommandList* l, UINT i, D3D12_GPU_VIRTUAL_ADDRESS va) {
    if (!t_ours) Foreign(l);
    o_setSrv(l, i, va);
}
void STDMETHODCALLTYPE SetUav(ID3D12GraphicsCommandList* l, UINT i, D3D12_GPU_VIRTUAL_ADDRESS va) {
    if (!t_ours) Foreign(l);
    o_setUav(l, i, va);
}
void STDMETHODCALLTYPE SetTargets(ID3D12GraphicsCommandList* l, UINT n, const D3D12_CPU_DESCRIPTOR_HANDLE* rt, BOOL single,
                                  const D3D12_CPU_DESCRIPTOR_HANDLE* ds) {
    if (!t_ours) {
        Lock lock;
        auto& s = g_lists[l];
        const std::uint64_t next = ds ? ds->ptr : 0;
        // No effects pass in this frame: draw before the list leaves the scene depth.
        if (!s.bundle && s.scene && !s.inserted && s.dsv == s.scene && next != s.scene) Insert(l, s, "leaving the scene");
        s.dsv = next;
    }
    o_setTargets(l, n, rt, single, ds);
}
void STDMETHODCALLTYPE ExecuteBundle(ID3D12GraphicsCommandList* l, ID3D12GraphicsCommandList* b) {
    if (t_ours || !b) { o_bundle(l, b); return; }
    {
        Lock lock;
        auto& s = g_lists[l];
        const auto bundle = g_lists.find(b);
        PsoInfo info {};
        bool known = false;
        if (bundle != g_lists.end() && bundle->second.firstPso)
            if (const auto it = g_psos.find(bundle->second.firstPso); it != g_psos.end()) { info = it->second; known = true; }
        if (!s.bundle && known && s.dsv) {
            if (!s.scene && info.depthWrite) {
                const auto width = g_dsvWidth.find(s.dsv);
                if (width != g_dsvWidth.end() && width->second >= kSceneMinWidth) {
                    s.scene = s.dsv;
                    s.sceneRtv = info.rtv;
                    s.sceneDsv = info.dsv;
                }
            }
            if (s.scene && s.dsv == s.scene && !s.inserted && info.depthTest && !info.depthWrite && info.dsv == s.sceneDsv)
                Insert(l, s, "before the effects");
        }
    }
    o_bundle(l, b);
    Lock lock;
    auto& s = g_lists[l];  // before the find: inserting may rehash
    const auto bundle = g_lists.find(b);
    if (bundle == g_lists.end()) return;
    // The bundle's root bindings carry over to the calling list.
    const auto& from = bundle->second;
    if (from.rootSet) {
        if (from.root != s.root) s.args = {};
        s.root = from.root;
    }
    for (UINT i = 0; i < kMaxRoot; ++i)
        if (from.args[i].kind) s.args[i] = from.args[i];
    s.foreign = s.foreign || from.foreign;
}

std::vector<void*> g_hooked;
bool Hook(void* target, void* detour, void** original, const char* label) {
    MH_STATUS status = MH_CreateHook(target, detour, original);
    if (status == MH_OK) {
        g_hooked.push_back(target);
        status = MH_EnableHook(target);
    }
    if (status != MH_OK) {
        if (g_log) g_log("[crown-scene] hooking %s failed: %d (%s)", label, status, MH_StatusToString(status));
        return false;
    }
    return true;
}

}  // namespace

bool Install(LogFn log, VerticesFn vertices, TextureFn texture) {
    g_log = log;
    g_vertices = vertices;
    g_texture = texture;
    ID3D12Device* device = nullptr;
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    bool ok = SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))) &&
              SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) &&
              SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&list)));
    if (ok) {
        void** dv = *reinterpret_cast<void***>(device);
        void** lv = *reinterpret_cast<void***>(list);
        const struct { void* target; void* detour; void** original; const char* label; } hooks[] = {
            {dv[kCreateGraphicsPipelineState], reinterpret_cast<void*>(&CreatePso), reinterpret_cast<void**>(&o_createPso), "CreateGraphicsPipelineState"},
            {dv[kCreateDepthStencilView], reinterpret_cast<void*>(&CreateDsv), reinterpret_cast<void**>(&o_createDsv), "CreateDepthStencilView"},
            {lv[kReset], reinterpret_cast<void*>(&Reset), reinterpret_cast<void**>(&o_reset), "Reset"},
            {lv[kDrawInstanced], reinterpret_cast<void*>(&Draw), reinterpret_cast<void**>(&o_draw), "DrawInstanced"},
            {lv[kDrawIndexedInstanced], reinterpret_cast<void*>(&DrawIndexed), reinterpret_cast<void**>(&o_drawIndexed), "DrawIndexedInstanced"},
            {lv[kSetPipelineState], reinterpret_cast<void*>(&SetPso), reinterpret_cast<void**>(&o_setPso), "SetPipelineState"},
            {lv[kExecuteBundle], reinterpret_cast<void*>(&ExecuteBundle), reinterpret_cast<void**>(&o_bundle), "ExecuteBundle"},
            {lv[kSetGraphicsRootSignature], reinterpret_cast<void*>(&SetRoot), reinterpret_cast<void**>(&o_setRoot), "SetGraphicsRootSignature"},
            {lv[kSetGraphicsRootDescriptorTable], reinterpret_cast<void*>(&SetTable), reinterpret_cast<void**>(&o_setTable), "SetGraphicsRootDescriptorTable"},
            {lv[kSetGraphicsRoot32BitConstant], reinterpret_cast<void*>(&SetConstant), reinterpret_cast<void**>(&o_setConstant), "SetGraphicsRoot32BitConstant"},
            {lv[kSetGraphicsRoot32BitConstants], reinterpret_cast<void*>(&SetConstants), reinterpret_cast<void**>(&o_setConstants), "SetGraphicsRoot32BitConstants"},
            {lv[kSetGraphicsRootConstantBufferView], reinterpret_cast<void*>(&SetCbv), reinterpret_cast<void**>(&o_setCbv), "SetGraphicsRootConstantBufferView"},
            {lv[kSetGraphicsRootShaderResourceView], reinterpret_cast<void*>(&SetSrv), reinterpret_cast<void**>(&o_setSrv), "SetGraphicsRootShaderResourceView"},
            {lv[kSetGraphicsRootUnorderedAccessView], reinterpret_cast<void*>(&SetUav), reinterpret_cast<void**>(&o_setUav), "SetGraphicsRootUnorderedAccessView"},
            {lv[kOMSetRenderTargets], reinterpret_cast<void*>(&SetTargets), reinterpret_cast<void**>(&o_setTargets), "OMSetRenderTargets"},
        };
        for (const auto& h : hooks) ok = ok && Hook(h.target, h.detour, h.original, h.label);
        if (!ok)
            for (void* target : g_hooked) { MH_DisableHook(target); MH_RemoveHook(target); }
    }
    g_active = ok;
    if (list) list->Release();
    if (allocator) allocator->Release();
    if (device) device->Release();
    if (g_log) g_log("[crown-scene] %s", ok ? "hooks installed" : "not installed; the Present-time crown stays");
    return ok;
}

std::uint64_t Drawn() noexcept { return g_drawn.load(); }

}  // namespace kh2coop::inject::scenecrown
