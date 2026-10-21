// Steam Global native field functions, independently decompiled for VUH-2276.
static constexpr std::uint8_t kFieldPuzzleUpdate[]={0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x50,0x48,0x8B,0x35,0x8F,0x58,0x5F,0x02,0x48,0x8B,0xFA,0x48,0x8B,0xCE,0xE8,0xD4,0xF9,0xF9,0xFF,0x84,0xC0,0x0F,0x84,0xF4,0x00,0x00,0x00,0x48,0x8B,0xCF,0x48,0x89,0x5C,0x24,0x60,0xE8,0xDF,0xAD,0xF9,0xFF,0x48,0x8B,0xCE,0x48,0x8B,0xD8,0xE8,0xD4,0xAD,0xF9,0xFF,0x48,0x8B,0xD3,0x48,0x8D,0x4C,0x24,0x40,0x0F,0x10,0x00,0x0F,0x11,0x44,0x24,0x40,0xE8,0x9F,0x4F,0xD2,0xFF,0x48,0x8B,0x5C,0x24,0x60,0x0F,0x10,0x00,0x0F,0x11,0x44,0x24,0x30,0x0F,0xC6,0xC0,0x55,0xF3,0x0F,0x5C,0x05,0x92,0xA5,0x19,0x00,0xF3,0x0F,0x5A,0xC0,0x0F,0x54,0x05,0xEB,0x90,0x20,0x00,0xF2,0x0F,0x5A,0xC0,0x0F,0x2F,0x05,0x30,0x8E,0x20,0x00,0x0F,0x87,0x92,0x00,0x00,0x00,0x0F,0x57,0xC0,0x48,0x8D,0x4C,0x24,0x30,0xF3,0x0F,0x11,0x44,0x24,0x34,0xE8,0x87,0x61,0xD2,0xFF,0xF3,0x0F,0x10,0x0D,0x77,0x8C,0x19,0x00,0x0F,0x2F,0xC8,0x72,0x72,0x48,0x8B,0x8F,0x00,0x0C,0x00,0x00,0x48,0x85,0xC9,0x74,0x1D,0xE8,0x89,0xC7,0xFC,0xFF,0x48,0x8B,0x87,0x00,0x0C,0x00,0x00,0x0F,0xB7,0x48,0x02,0xE8,0xA9,0x2B,0xF9,0xFF,0x48,0x8B,0xC8,0xE8,0xD1,0xE7,0xD3,0xFF,0xF3,0x0F,0x10,0x0D,0xA5,0x8D,0x20,0x00,0x48,0x8B,0x8F,0xF8,0x0B,0x00,0x00,0xE8,0x8D,0x38,0xEA,0xFF,0x33,0xC0,0xBA,0x01,0x00,0x00,0x00,0x44,0x8B,0xC2,0x48,0x89,0x87,0xF8,0x0B,0x00,0x00,0x45,0x33,0xC9,0x48,0x89,0x44,0x24,0x20,0x48,0x8B,0xCF,0xE8,0x3C,0xB8,0xF9,0xFF,0x8B,0x0F,0xE8,0x35,0x24,0x09,0x00,0x48,0x8B,0xD7,0x48,0x8B,0xC8,0x4C,0x8B,0x00,0x41,0xFF,0x50,0x38,0x48,0x8B,0x74,0x24,0x68,0x48,0x83,0xC4,0x50,0x5F,0xC3};
static constexpr std::uint8_t kFieldPuzzleLeave[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0x8A,0xF8,0x0B,0x00,0x00,0x48,0x8B,0xDA,0x48,0x85,0xC9,0x74,0x10,0xE8,0x06,0x3A,0xEA,0xFF,0x48,0xC7,0x83,0xF8,0x0B,0x00,0x00,0x00,0x00,0x00,0x00,0x48,0x8B,0xCB,0x48,0x83,0xC4,0x20,0x5B,0xE9,0x4E,0x98,0xF9,0xFF};
static constexpr std::uint8_t kFieldBaseLeave[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x8B,0x81,0x20,0x01,0x00,0x00,0x48,0x8B,0xD9,0xC1,0xE8,0x1C,0xA8,0x01,0x75,0x58,0x48,0x83,0xB9,0xB0,0x05,0x00,0x00,0x00,0x74,0x18,0x48,0x81,0xC1,0x90,0x03,0x00,0x00,0xE8,0x04,0x5E,0x01,0x00,0x48,0x8B,0x8B,0xB0,0x05,0x00,0x00,0xE8,0xC8,0xD2,0x02,0x00,0x48,0x8D,0x8B,0x58,0x01,0x00,0x00,0xE8,0xBC,0x2B,0x01,0x00,0x8B,0x4B,0x08,0xE8,0x24,0x8B,0x0F,0x00,0x0F,0xB6,0x48,0x54,0xF7,0xD9,0x66,0x0F,0x6E,0xC1,0x0F,0x5B,0xC0,0xE8,0x82,0xD7,0xFE,0xFF,0x81,0x8B,0x20,0x01,0x00,0x00,0x00,0x00,0x00,0x10,0x48,0x83,0xC4,0x20,0x5B,0xC3,0x81,0x89,0x20,0x01,0x00,0x00,0x00,0x00,0x00,0x10,0x48,0x83,0xC4,0x20,0x5B,0xC3};

// Only puzzle's non-thunk +20 is pointer-patched. Originals remain valid for
// process lifetime, including callbacks loaded before restoration.
static std::atomic<Brain> g_fieldPuzzleOriginal{nullptr};
static spectate::CallbackGate g_fieldCallbacks;
static SRWLOCK g_fieldSlotLock=SRWLOCK_INIT;
static uintptr_t g_fieldPuzzleSlots[2]{};
struct FieldProtection {uintptr_t page{};DWORD original{};bool pending{};};
static FieldProtection g_fieldProtection[2]{};
static bool g_fieldPatchFailed=false,g_fieldClosed=false;
static std::uint64_t g_fieldBrainSkips=0;
bool EnterFieldCallback() noexcept {return g_fieldCallbacks.Enter();}
void LeaveFieldCallback() noexcept {g_fieldCallbacks.Leave();}
static bool FieldBrainDecision(uintptr_t actor) {
    if(!g_fieldCallbacks.Enter())return false;
    bool suppressed=false;
    __try {
        if(OwnerThread() && enemysync::SuppressClientDecision(actor)) {
            suppressed=true;++g_fieldBrainSkips;
            if(auto* d=Find(actor);d && DrivenNow(actor)) {
                ++d->skips;++g_mstats.skips;d->lastSkipFrame=g_frameCounter;
            }
        }
    } __finally {g_fieldCallbacks.Leave();}
    return suppressed;
}
static void __fastcall FieldPuzzleBrain(void* handler,void* actor) {
    const auto native=g_fieldPuzzleOriginal.load(std::memory_order_acquire);
    if(native && !FieldBrainDecision(reinterpret_cast<uintptr_t>(actor)))native(handler,actor);
}
static bool FieldSwapSlot(uintptr_t entry,void* expected,void* desired) noexcept {
    SYSTEM_INFO system{};GetSystemInfo(&system);
    const auto page=entry-(entry%system.dwPageSize);
    FieldProtection* protection=nullptr;
    for(auto& p:g_fieldProtection)if(p.page==page){protection=&p;break;}
    if(!protection)for(auto& p:g_fieldProtection)if(!p.page){protection=&p;break;}
    if(!protection)return false;
    DWORD prior=0,ignored=0;
    if(!VirtualProtect(reinterpret_cast<void*>(entry),sizeof(void*),PAGE_READWRITE,&prior))return false;
    if(!protection->page){protection->page=page;protection->original=prior;}
    protection->pending=true;
    const auto observed=InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(entry),desired,expected);
    const bool restored=VirtualProtect(reinterpret_cast<void*>(entry),sizeof(void*),protection->original,&ignored)!=FALSE;
    protection->pending=!restored;
    // A successful CAS remains ours even if permission restoration failed.
    if(!restored)Log("[field-mirror] slot-protection-fault entry=%llX",static_cast<unsigned long long>(entry));
    return observed==expected;
}
static bool EnsureFieldPuzzleHook(uintptr_t actor) {
    // Pin outside the slot lock: DLL shutdown can run under the loader lock.
    if(!g_fieldPuzzleOriginal.load(std::memory_order_acquire)) {
        // Only the native puzzle class requires a pointer patch.
        uintptr_t objentry=0;std::uint8_t type=0;
        if(!ReadHitTrace(actor+offsets::actor::OBJENTRY_PTR,objentry) || !objentry ||
           !ReadHitTrace(objentry+offsets::objentry::TYPE_FLAGS,type) || type!=24)return false;
        HMODULE pinned=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
              reinterpret_cast<LPCWSTR>(&FieldPuzzleBrain),&pinned))return false;
    }
    AcquireSRWLockExclusive(&g_fieldSlotLock);
    bool installed=false;
    __try {
        if(g_fieldPatchFailed || g_fieldClosed)return false;
        uintptr_t handler=0,vtable=0;std::uint32_t handle=0;
        if(!ReadHitTrace(actor,handle) || !handle || !g_resolveHandle)return false;
        handler=g_resolveHandle(handle);
        if(!handler || !ReadHitTrace(handler,vtable))return false;
        const uintptr_t tables[]={0x5D41A8,0x5D4358};
        unsigned slot=2;
        for(unsigned i=0;i<2;++i)if(vtable==g_exeBase+tables[i])slot=i;
        if(slot==2)return false;
        const auto entry=vtable+0x20;
        if(g_fieldPuzzleSlots[slot]==entry)return true;
        if(std::memcmp(reinterpret_cast<void*>(g_exeBase+0x41AD30),kFieldPuzzleUpdate,sizeof(kFieldPuzzleUpdate)))return false;
        g_fieldPuzzleOriginal.store(reinterpret_cast<Brain>(g_exeBase+0x41AD30),std::memory_order_release);
        if(!FieldSwapSlot(entry,reinterpret_cast<void*>(g_exeBase+0x41AD30),reinterpret_cast<void*>(&FieldPuzzleBrain))) {
            g_fieldPatchFailed=true;NoteLog("field-puzzle-hook-refused",actor,entry-g_exeBase,0);return false;
        }
        g_fieldPuzzleSlots[slot]=entry;installed=true;
        NoteLog("field-puzzle-hook",actor,entry-g_exeBase,0x41AD30);
    } __finally {ReleaseSRWLockExclusive(&g_fieldSlotLock);}
    return installed;
}
bool ShutdownFields() {
    AcquireSRWLockExclusive(&g_fieldSlotLock);
    g_fieldClosed=true;g_fieldCallbacks.Close();
    const auto native=g_fieldPuzzleOriginal.load(std::memory_order_acquire);
    bool restored=true;
    for(auto& entry:g_fieldPuzzleSlots)if(entry) {
        if(!FieldSwapSlot(entry,reinterpret_cast<void*>(&FieldPuzzleBrain),reinterpret_cast<void*>(native))) {
            Log("[field-mirror] restore-mismatch entry=%llX",static_cast<unsigned long long>(entry));
            uintptr_t current=0;
            if(!ReadHitTrace(entry,current) || current==reinterpret_cast<uintptr_t>(&FieldPuzzleBrain)) {restored=false;continue;}
        }
        entry=0;
    }
    // Protection belongs to the page even when a pointer CAS refused ownership.
    for(auto& p:g_fieldProtection)if(p.pending) {
        DWORD ignored=0;
        p.pending=VirtualProtect(reinterpret_cast<void*>(p.page),1,p.original,&ignored)==FALSE;
        if(p.pending){restored=false;Log("[field-mirror] slot-protection-fault entry=%llX",static_cast<unsigned long long>(p.page));}
    }
    ReleaseSRWLockExclusive(&g_fieldSlotLock);
    const auto until=GetTickCount64()+1000;
    while(!g_fieldCallbacks.Drained() && GetTickCount64()<until)Sleep(1);
    if(!g_fieldCallbacks.Drained()) {
        Log("[field-mirror] shutdown-held callbacks active; state retained");return false;
    }
    return restored;
}
// The native class owns effect unlinking and deferred actor destruction. Never
// copy host pointers, call a destructor early, or repeat an ambiguous leave.
static void LeaveFieldObject(uintptr_t actor) noexcept {
    __try {
        std::uint32_t handle=0;uintptr_t handler=0,vtable=0,target=0;
        if(!ReadHitTrace(actor,handle) || !handle || !g_resolveHandle)return;
        handler=g_resolveHandle(handle);
        if(!handler || !ReadHitTrace(handler,vtable) || !ReadHitTrace(vtable+0x38,target))return;
        const bool puzzle=target==g_exeBase+0x41AE80 &&
            std::memcmp(reinterpret_cast<void*>(target),kFieldPuzzleLeave,sizeof(kFieldPuzzleLeave))==0;
        std::uint8_t bytes[8]{};
        const bool ordinary=target>=g_exeBase && target<g_exeBase+0x3000000 &&
            ReadHitTraceMemory(target,bytes,sizeof(bytes)) && BrainThunk(bytes,target,g_exeBase+0x3B4700);
        if((!puzzle && !ordinary) ||
           std::memcmp(reinterpret_cast<void*>(g_exeBase+0x3B4700),kFieldBaseLeave,sizeof(kFieldBaseLeave)) ||
           !enemysync::ConsumeFieldLeave(actor))return;
        uintptr_t finalTarget=0;
        if(!ReadHitTrace(vtable+0x38,finalTarget) || finalTarget!=target)return;
        reinterpret_cast<Brain>(target)(reinterpret_cast<void*>(handler),reinterpret_cast<void*>(actor));
    } __except(EXCEPTION_EXECUTE_HANDLER) {Log("[field-mirror] leave-fault actor=%llX",static_cast<unsigned long long>(actor));}
}

static void PrepareFieldActor(uintptr_t actor) {
    if(!g_fieldCallbacks.Enter())return;
    __try {
        if(OwnerThread() && enemysync::SuppressClientDecision(actor)) {
            if(!EnsureFieldPuzzleHook(actor))(void)EnsureBrainHook(actor);
            LeaveFieldObject(actor);
        }
    } __finally {g_fieldCallbacks.Leave();}
}
