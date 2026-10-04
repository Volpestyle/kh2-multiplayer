#include "NativeOwnedEmitterInstall.hpp"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <new>

namespace kh2coop::inject::ownedemitter::installation {
namespace {
using U = std::uintptr_t;
constexpr U EmitterRva = 0x3fe6f0, UnwindRva = 0x6ec7e0;
constexpr SIZE_T Page = 0x1000, AllocationBytes = 5 * Page;
// Saved PE9002B2DE...5849ED: [3FE6F0,3FEA1A) and native UNWIND_INFO6EC7E0.
constexpr std::uint8_t kBody[] = {
    0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x70,0x10,0x48,0x89,0x78,0x18,0x55,
    0x41,0x56,0x41,0x57,0x48,0x8d,0x68,0xd8,0x48,0x81,0xec,0x10,0x01,0x00,0x00,0xf3,
    0x0f,0x10,0x05,0x69,0x53,0x22,0x00,0x48,0x8b,0xf9,0x0f,0x29,0x70,0xd8,0x45,0x33,
    0xff,0x0f,0x29,0x78,0xc8,0x0f,0x57,0xff,0x44,0x0f,0x29,0x40,0xb8,0xf3,0x44,0x0f,
    0x10,0x05,0xba,0xb7,0x1c,0x00,0x44,0x0f,0x29,0x48,0xa8,0xf3,0x44,0x0f,0x10,0x0d,
    0x34,0x6e,0x1b,0x00,0x44,0x0f,0x29,0x50,0x98,0xf3,0x44,0x0f,0x10,0x15,0xa2,0x3f,
    0x1b,0x00,0x44,0x0f,0x29,0x58,0x88,0xf3,0x44,0x0f,0x10,0x1d,0x84,0x54,0x22,0x00,
    0x44,0x0f,0x29,0xa0,0x78,0xff,0xff,0xff,0xf3,0x44,0x0f,0x10,0x25,0x77,0x3b,0x1b,
    0x00,0x44,0x0f,0x29,0xa8,0x68,0xff,0xff,0xff,0xf3,0x44,0x0f,0x10,0x2d,0x16,0x55,
    0x22,0x00,0xf3,0x0f,0x11,0x44,0x24,0x4c,0x48,0xc7,0x44,0x24,0x44,0x00,0x00,0x00,
    0x00,0xc7,0x44,0x24,0x40,0x00,0x00,0x00,0x00,0x0f,0x1f,0x80,0x00,0x00,0x00,0x00,
    0x48,0x8b,0x4f,0x08,0x45,0x8b,0xf7,0x66,0x44,0x3b,0x79,0x04,0x0f,0x83,0x09,0x02,
    0x00,0x00,0x49,0x8b,0xf7,0x66,0x66,0x66,0x0f,0x1f,0x84,0x00,0x00,0x00,0x00,0x00,
    0xf3,0x0f,0x10,0x47,0x20,0x0f,0x2f,0xc7,0x0f,0x87,0x07,0x02,0x00,0x00,0x80,0x7c,
    0x0e,0x48,0x02,0x0f,0x85,0xca,0x01,0x00,0x00,0x0f,0xb7,0x5c,0x0e,0x4a,0x8b,0xcb,
    0xe8,0xfb,0x59,0xfb,0xff,0x48,0x85,0xc0,0x0f,0x85,0xb5,0x01,0x00,0x00,0x8b,0xd3,
    0x48,0x8d,0x0d,0x69,0x76,0x6e,0x02,0xe8,0x04,0x71,0xff,0xff,0x84,0xc0,0x0f,0x85,
    0x9f,0x01,0x00,0x00,0x48,0x8b,0x4f,0x08,0x0f,0xb6,0x47,0x2c,0x38,0x44,0x0e,0x5c,
    0x0f,0x87,0x8d,0x01,0x00,0x00,0x0f,0xb6,0x54,0x0e,0x49,0x83,0xea,0x01,0x0f,0x84,
    0xb0,0x00,0x00,0x00,0x83,0xfa,0x01,0x74,0x1b,0x48,0x83,0xc1,0x2c,0x49,0x63,0xc6,
    0x48,0xc1,0xe0,0x06,0x48,0x8b,0xd7,0x48,0x03,0xc8,0xe8,0x51,0xfd,0xff,0xff,0xe9,
    0x45,0x01,0x00,0x00,0xf3,0x0f,0x10,0x44,0x0e,0x30,0xf3,0x0f,0x11,0x44,0x24,0x40,
    0xf3,0x0f,0x10,0x4c,0x0e,0x34,0xf3,0x0f,0x11,0x4c,0x24,0x44,0xf3,0x0f,0x10,0x44,
    0x0e,0x38,0xf3,0x0f,0x11,0x44,0x24,0x48,0x0f,0xb7,0x44,0x0e,0x5a,0x8b,0x4c,0x0e,
    0x2c,0x66,0x0f,0x6e,0xf0,0x0f,0x5b,0xf6,0xe8,0x33,0x16,0xfe,0xff,0x48,0x8d,0x54,
    0x24,0x40,0x0f,0xb6,0x48,0x48,0x0f,0x28,0xde,0xc0,0xe9,0x03,0x0f,0x28,0xd7,0xf6,
    0xc1,0x01,0x48,0x8d,0x4c,0x24,0x60,0x74,0x24,0xc7,0x44,0x24,0x30,0x02,0x00,0x00,
    0x00,0xf3,0x0f,0x11,0x7c,0x24,0x28,0xf3,0x44,0x0f,0x11,0x54,0x24,0x20,0xe8,0xcd,
    0x57,0xff,0xff,0x48,0x8d,0x54,0x24,0x60,0xe9,0xa3,0x00,0x00,0x00,0xc7,0x44,0x24,
    0x20,0x01,0x00,0x00,0x00,0xe8,0xd6,0x56,0xff,0xff,0x48,0x8d,0x54,0x24,0x60,0xe9,
    0x8c,0x00,0x00,0x00,0x8b,0x5c,0x0e,0x2c,0x48,0x8b,0x0d,0xf1,0x1c,0x61,0x02,0xe8,
    0x5c,0x72,0xfb,0xff,0x8b,0xcb,0x0f,0x10,0x00,0x0f,0x11,0x44,0x24,0x50,0x0f,0xc6,
    0xc0,0x55,0xf3,0x41,0x0f,0x58,0xc5,0xf3,0x0f,0x11,0x44,0x24,0x54,0xe8,0xae,0x15,
    0xfe,0xff,0x41,0x0f,0x28,0xd8,0x48,0x8d,0x54,0x24,0x50,0x41,0x0f,0x28,0xd1,0x0f,
    0xb6,0x48,0x48,0xc0,0xe9,0x03,0xf6,0xc1,0x01,0x74,0x22,0xc7,0x44,0x24,0x30,0x02,
    0x00,0x01,0x00,0x48,0x8d,0x4c,0x24,0x70,0xf3,0x44,0x0f,0x11,0x5c,0x24,0x28,0xf3,
    0x44,0x0f,0x11,0x64,0x24,0x20,0xe8,0x45,0x57,0xff,0xff,0xeb,0x11,0x48,0x8d,0x4d,
    0x80,0xc7,0x44,0x24,0x20,0x01,0x00,0x01,0x00,0xe8,0x52,0x56,0xff,0xff,0x48,0x8b,
    0xd0,0x48,0x8d,0x4c,0x24,0x50,0xe8,0x05,0xa5,0xda,0xff,0x48,0x8d,0x54,0x24,0x50,
    0x48,0x8d,0x4c,0x24,0x40,0xe8,0xf6,0xa4,0xda,0xff,0x48,0x8b,0x47,0x08,0x4c,0x8d,
    0x44,0x24,0x40,0x48,0x83,0xc0,0x2c,0x49,0x63,0xce,0x48,0xc1,0xe1,0x06,0x48,0x8b,
    0xd7,0x48,0x03,0xc8,0xe8,0xc7,0xfc,0xff,0xff,0x48,0x85,0xc0,0x74,0x15,0x48,0x8b,
    0x47,0x08,0x0f,0xb7,0x4c,0x06,0x56,0x66,0x0f,0x6e,0xc1,0x0f,0x5b,0xc0,0xf3,0x0f,
    0x11,0x47,0x20,0x48,0x8b,0x4f,0x08,0x41,0xff,0xc6,0x48,0x83,0xc6,0x40,0x0f,0xb7,
    0x41,0x04,0x44,0x3b,0xf0,0x0f,0x8c,0x05,0xfe,0xff,0xff,0x0f,0x2f,0x7f,0x20,0x0f,
    0x82,0xdb,0xfd,0xff,0xff,0x48,0x8b,0xcf,0xe8,0x83,0x01,0x00,0x00,0x84,0xc0,0x0f,
    0x85,0xcb,0xfd,0xff,0xff,0x4c,0x8d,0x9c,0x24,0x10,0x01,0x00,0x00,0x49,0x8b,0x5b,
    0x20,0x49,0x8b,0x73,0x28,0x49,0x8b,0x7b,0x30,0x41,0x0f,0x28,0x73,0xf0,0x41,0x0f,
    0x28,0x7b,0xe0,0x45,0x0f,0x28,0x43,0xd0,0x45,0x0f,0x28,0x4b,0xc0,0x45,0x0f,0x28,
    0x53,0xb0,0x45,0x0f,0x28,0x5b,0xa0,0x45,0x0f,0x28,0x63,0x90,0x45,0x0f,0x28,0x6b,
    0x80,0x49,0x8b,0xe3,0x41,0x5f,0x41,0x5e,0x5d,0xc3,
};
constexpr std::uint8_t kUnwind[] = {
    0x01,0x89,0x1b,0x00,0x89,0xd8,0x09,0x00,0x78,0xc8,0x0a,0x00,0x67,0xb8,0x0b,0x00,
    0x59,0xa8,0x0c,0x00,0x4b,0x98,0x0d,0x00,0x3d,0x88,0x0e,0x00,0x35,0x78,0x0f,0x00,
    0x2e,0x68,0x10,0x00,0x1f,0x74,0x28,0x00,0x1f,0x64,0x27,0x00,0x1f,0x34,0x26,0x00,
    0x1f,0x01,0x22,0x00,0x14,0xf0,0x12,0xe0,0x10,0x50,0x00,0x00,
};
static_assert(sizeof(kBody) == 0x32a && sizeof(kUnwind) == 60);
struct NativeBinding { U imageBase{}, imageBytes{}, begin{}, unwind{}; };
struct Context {
    Snapshot snapshot{};
    CodePlan plan{};
    PrimitiveState* state{};
    RUNTIME_FUNCTION* table{};
    HMODULE pinnedModule{};
};
SRWLOCK g_lock = SRWLOCK_INIT;
Context g_context{};
struct Lock {
    Lock() noexcept { AcquireSRWLockExclusive(&g_lock); }
    ~Lock() { ReleaseSRWLockExclusive(&g_lock); }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
};
bool Extent(U address, SIZE_T size) noexcept {
    return address >= 0x10000 && size && address < 0x800000000000ULL &&
           size <= 0x800000000000ULL - address;
}
bool Read(U address, void* out, SIZE_T size) noexcept {
    SIZE_T actual{};
    return Extent(address, size) && ReadProcessMemory(GetCurrentProcess(),
        reinterpret_cast<const void*>(address), out, size, &actual) && actual == size;
}
bool Executable(U address, SIZE_T size, bool image) noexcept {
    if (!Extent(address, size)) return false;
    const U end = address + size;
    while (address < end) {
        MEMORY_BASIC_INFORMATION m{};
        if (!VirtualQuery(reinterpret_cast<void*>(address), &m, sizeof m) || m.State != MEM_COMMIT ||
            (m.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || (image && m.Type != MEM_IMAGE)) return false;
        const auto protect = m.Protect & 0xff;
        if (protect != PAGE_EXECUTE && protect != PAGE_EXECUTE_READ &&
            protect != PAGE_EXECUTE_READWRITE && protect != PAGE_EXECUTE_WRITECOPY) return false;
        const U start = reinterpret_cast<U>(m.BaseAddress);
        if (m.RegionSize > std::numeric_limits<U>::max() - start || start + m.RegionSize <= address) return false;
        address = start + m.RegionSize;
    }
    return true;
}
bool MainImage(U base, NativeBinding& out) noexcept {
    out = {};
    if (base != reinterpret_cast<U>(GetModuleHandleW(nullptr))) return false;
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    MEMORY_BASIC_INFORMATION m{};
    if (!VirtualQuery(reinterpret_cast<void*>(base), &m, sizeof m) || m.Type != MEM_IMAGE ||
        reinterpret_cast<U>(m.AllocationBase) != base || !Read(base, &dos, sizeof dos) ||
        dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 || dos.e_lfanew > 0x10000 ||
        !Read(base + static_cast<U>(dos.e_lfanew), &nt, sizeof nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER64) ||
        nt.OptionalHeader.SizeOfImage < UnwindRva + sizeof(kUnwind) ||
        !Extent(base, nt.OptionalHeader.SizeOfImage) || !Executable(base + EmitterRva, sizeof(kBody), true)) return false;
    out = {base, nt.OptionalHeader.SizeOfImage, base + EmitterRva, base + UnwindRva};
    return true;
}
Status Validate(const NativeBinding& n) noexcept {
    if (!Extent(n.imageBase, n.imageBytes) || !Extent(n.begin, sizeof(kBody)) ||
        !Extent(n.unwind, sizeof(kUnwind)) || n.begin < n.imageBase || n.unwind < n.imageBase ||
        n.begin + sizeof(kBody) > n.imageBase + n.imageBytes ||
        n.unwind + sizeof(kUnwind) > n.imageBase + n.imageBytes ||
        n.begin - n.imageBase > UINT32_MAX - sizeof(kBody) || n.unwind - n.imageBase > UINT32_MAX - sizeof(kUnwind))
        return Status::IdentityUnavailable;
    std::array<std::uint8_t, sizeof(kBody)> body{};
    std::array<std::uint8_t, sizeof(kUnwind)> unwind{};
    if (!Read(n.begin, body.data(), body.size()) || !Read(n.unwind, unwind.data(), unwind.size()))
        return Status::IdentityUnavailable;
    if (std::memcmp(body.data(), kBody, body.size())) return Status::IdentityMismatch;
    if (std::memcmp(unwind.data(), kUnwind, unwind.size())) return Status::UnwindMismatch;
    // Lookup at all three replacement sites and the function entry. No cached
    // caller tuple can substitute for the actual Windows lookup at preparation.
    for (const U offset : {U(0), U(0x139), U(0x299), U(0x2b3)}) {
        DWORD64 image{};
        auto* f = RtlLookupFunctionEntry(n.begin + offset, &image, nullptr);
        RUNTIME_FUNCTION actual{};
        if (!f || !Read(reinterpret_cast<U>(f), &actual, sizeof actual) || image != n.imageBase ||
            actual.BeginAddress != n.begin - n.imageBase || actual.EndAddress != n.begin + sizeof(kBody) - n.imageBase ||
            actual.UnwindData != n.unwind - n.imageBase) return Status::UnwindMismatch;
    }
    return Status::PreparedCommitHeld;
}
using Alloc2Fn = PVOID(WINAPI*)(HANDLE, PVOID, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER*, ULONG);
std::uint8_t* Allocate(const NativeBinding& n) noexcept {
    const auto p = GetProcAddress(GetModuleHandleW(L"KernelBase.dll"), "VirtualAlloc2");
    Alloc2Fn allocate{}; static_assert(sizeof allocate == sizeof p); std::memcpy(&allocate, &p, sizeof p);
    if (!allocate) return nullptr;
    SYSTEM_INFO info{}; GetSystemInfo(&info);
    if (info.dwPageSize != Page || !info.dwAllocationGranularity ||
        !Extent(n.imageBase, 0x80000000ULL) || !Extent(n.imageBase, n.imageBytes)) return nullptr;
    const U gran = info.dwAllocationGranularity;
    const U low = (n.imageBase + n.imageBytes + gran - 1) / gran * gran;
    const U high = std::min(n.imageBase + 0x7fffffffULL, reinterpret_cast<U>(info.lpMaximumApplicationAddress));
    if (low > high || high - low + 1 < AllocationBytes) return nullptr;
    MEM_ADDRESS_REQUIREMENTS requirements{reinterpret_cast<void*>(low), reinterpret_cast<void*>(high), 0};
    MEM_EXTENDED_PARAMETER parameter{}; parameter.Type = MemExtendedParameterAddressRequirements;
    parameter.Pointer = &requirements;
    return static_cast<std::uint8_t*>(allocate(nullptr, nullptr, AllocationBytes, MEM_RESERVE | MEM_COMMIT,
        PAGE_READWRITE, &parameter, 1));
}
bool Pin(HMODULE& module) noexcept {
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&Prepare), &module) != FALSE;
}
bool Protect(void* address, SIZE_T size, DWORD protection) noexcept {
    DWORD previous{}; return VirtualProtect(address, size, protection, &previous) != FALSE;
}
bool Flush(void* address, SIZE_T size) noexcept {
    return FlushInstructionCache(GetCurrentProcess(), address, size) != FALSE;
}
bool Register(RUNTIME_FUNCTION* table, DWORD64 base) noexcept {
    return RtlAddFunctionTable(table, static_cast<DWORD>(GateCount), base) != FALSE;
}
// Install-only failure seams. Public Prepare always supplies the real APIs;
// emitted instructions/gateway do not access this context or any service here.
struct Ops {
    std::uint8_t*(*allocate)(const NativeBinding&) noexcept = &Allocate;
    bool(*pin)(HMODULE&) noexcept = &Pin;
    bool(*protect)(void*, SIZE_T, DWORD) noexcept = &Protect;
    bool(*flush)(void*, SIZE_T) noexcept = &Flush;
    bool(*registerTable)(RUNTIME_FUNCTION*, DWORD64) noexcept = &Register;
};
bool Fail(Context& c, Status status) noexcept {
    c.snapshot.status = status; c.snapshot.lastError = GetLastError(); return false;
}
bool PrepareBinding(Context& c, const NativeBinding& native, const Ops& ops) noexcept {
    if (c.snapshot.attempted || c.snapshot.stopped) { c.snapshot.reinitializationRejected = true; return false; }
    c.snapshot.attempted = true; c.snapshot.imageBase = native.imageBase;
    const auto identity = Validate(native);
    if (identity != Status::PreparedCommitHeld) return Fail(c, identity);
    c.snapshot.identityVerified = true;
    if (!ops.pin(c.pinnedModule)) return Fail(c, Status::PinFailed);
    c.snapshot.modulePinned = true;
    // Pinning is irreversible; even a later failure forbids unload/reinit.
    auto* block = ops.allocate(native);
    if (!block) return Fail(c, Status::AllocationUnavailable);
    const U address = reinterpret_cast<U>(block);
    c.snapshot.allocation = address;
    c.snapshot.resourcesMayBeReferenced = true;
    // No freeing paths exist after allocation, including ambiguous API failures.
    // At most one 20KiB block per requested production lifetime is retained.
    if (!Extent(address, AllocationBytes) || address % Page || address < native.imageBase + native.imageBytes ||
        address - native.imageBase > 0x7fffffffULL - AllocationBytes)
        return Fail(c, Status::PlanRejected);
    c.state = reinterpret_cast<PrimitiveState*>(block + 4 * Page);
    CodeRequest request{};
    request.stateAddress = reinterpret_cast<U>(c.state);
    request.emitterBegin = native.begin; request.emitterEnd = native.begin + sizeof(kBody);
    request.emitterUnwind = native.unwind; request.emitterUnwindCapacity = sizeof(kUnwind);
    request.aContinuation = native.begin + 0x140; request.bNullContinuation = native.begin + 0x2b3;
    request.bNonNullContinuation = native.begin + 0x29e; request.cContinuation = native.begin + 0x2ba;
    request.epilogue = native.begin + 0x2e5;
    request.unwindAddressBase = native.imageBase; request.chainInfoAddress = address + 3 * Page;
    request.chainInfoCapacity = 16;
    // Deliberately allocate gate order B/A/C so actual sorting is indispensable.
    constexpr std::array<SIZE_T, GateCount> offsets{Page, 0, 2 * Page};
    for (std::size_t i = 0; i < GateCount; ++i) {
        request.islandAddresses[i] = address + offsets[i]; request.islandCapacities[i] = Page;
    }
    CodePlan plan{};
    c.snapshot.codeStatus = BuildCodePlan(request, plan);
    if (c.snapshot.codeStatus != CodeStatus::Complete) return Fail(c, Status::PlanRejected);
    std::memset(block, 0, AllocationBytes);
    c.state = new (block + 4 * Page) PrimitiveState{};
    for (std::size_t i = 0; i < GateCount; ++i)
        std::memcpy(block + offsets[i], plan.islands[i].bytes.data(), plan.islands[i].size);
    std::memcpy(block + 3 * Page, plan.chainInfo.data(), plan.chainInfo.size());
    c.table = reinterpret_cast<RUNTIME_FUNCTION*>(block + 3 * Page + 0x40);
    for (std::size_t i = 0; i < GateCount; ++i) {
        const auto& f = plan.functions[i];
        new (c.table + i) RUNTIME_FUNCTION{f.begin, f.end, f.unwindData};
    }
    std::sort(c.table, c.table + GateCount, [](const auto& a, const auto& b) { return a.BeginAddress < b.BeginAddress; });
    for (std::size_t i = 0; i < GateCount; ++i) {
        if (!ops.protect(block + offsets[i], Page, PAGE_EXECUTE_READ)) return Fail(c, Status::ProtectionFailedRetained);
        if (!ops.flush(block + offsets[i], Page)) return Fail(c, Status::CacheFlushFailedRetained);
    }
    if (!ops.protect(block + 3 * Page, Page, PAGE_READONLY)) return Fail(c, Status::ProtectionFailedRetained);
    // Registration is the first OS-visible publication. The retained latch and
    // module pin are already set even if registration returns an ambiguous false.
    if (!ops.registerTable(c.table, native.imageBase)) return Fail(c, Status::RegistrationFailedRetained);
    c.snapshot.registered = true; c.snapshot.unwindAddressBase = native.imageBase;
    // Identity remains sampled. A future commit must revalidate after obtaining
    // real quiescence; preparation cannot establish a race-free patch interval.
    if (Validate(native) != Status::PreparedCommitHeld) return Fail(c, Status::ReadbackFailedRetained);
    for (std::size_t i = 0; i < GateCount; ++i) {
        const auto& code = plan.islands[i];
        std::array<std::uint8_t, CodeCapacity> actual{};
        DWORD64 base{};
        const U pc = request.islandAddresses[i];
        auto* f = RtlLookupFunctionEntry(pc, &base, nullptr);
        if (!Read(pc, actual.data(), code.size) || std::memcmp(actual.data(), code.bytes.data(), code.size) ||
            !Executable(pc, code.size, false) || !f || base != native.imageBase ||
            f->BeginAddress != plan.functions[i].begin || f->EndAddress != plan.functions[i].end ||
            f->UnwindData != plan.functions[i].unwindData) return Fail(c, Status::ReadbackFailedRetained);
    }
    std::array<std::uint8_t,16> chain{};
    if (!Read(request.chainInfoAddress, chain.data(), chain.size()) || chain != plan.chainInfo)
        return Fail(c, Status::ReadbackFailedRetained);
    c.plan = plan; c.snapshot.status = Status::PreparedCommitHeld;
    return true;
}
} // namespace

bool Prepare(std::uintptr_t base, bool requested) noexcept {
    const DWORD error = GetLastError(); bool ok = false;
    {
        Lock lock;
        if (g_context.snapshot.attempted || g_context.snapshot.stopped) g_context.snapshot.reinitializationRejected = true;
        else if (!requested) ok = false;
        else {
            NativeBinding native{};
            if (!MainImage(base, native)) {
                g_context.snapshot.attempted = true; g_context.snapshot.imageBase = base;
                Fail(g_context, Status::IdentityUnavailable);
            } else ok = PrepareBinding(g_context, native, Ops{});
        }
    }
    SetLastError(error); return ok;
}
Snapshot GetSnapshot() noexcept { Lock lock; return g_context.snapshot; }
bool GetPreparedView(PreparedView& out) noexcept {
    Lock lock; out = {};
    if (g_context.snapshot.status != Status::PreparedCommitHeld || g_context.snapshot.stopped) return false;
    out = {g_context.state, g_context.plan, g_context.snapshot.unwindAddressBase}; return true;
}
void Stop() noexcept { Lock lock; g_context.snapshot.stopped = true; }
bool RequiresProcessLifetimeRetention() noexcept {
    Lock lock; return g_context.snapshot.modulePinned || g_context.snapshot.resourcesMayBeReferenced;
}
} // namespace kh2coop::inject::ownedemitter::installation
