#pragma once
#include <Windows.h>
#include <intrin.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <type_traits>

namespace kh2coop::inject::tracefiber {
// Shared with the existing global MinHook retention adapter in NativeResourceTrace.
inline std::atomic<bool> retained{};
inline bool PrepareRequested() {
    const auto error=GetLastError();char value[2]{};
    const bool result=GetEnvironmentVariableA("KH2COOP_SURVIVING_PACK_PREPARE",value,2)==1 && value[0]=='1';
    SetLastError(error);return result;
}
template<class T,unsigned Capacity=64> class Store {
    static_assert(std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>);
    struct Cell { DWORD thread{};uintptr_t key{},high{};bool rejected{};T data{}; };
    std::array<Cell,Capacity> cells_{};
    std::atomic<unsigned> count_{};
    DWORD slot_=FLS_OUT_OF_INDEXES;
    bool ready_{};
public:
    struct Services {
        DWORD(WINAPI*alloc)(PFLS_CALLBACK_FUNCTION)=&FlsAlloc;
        PVOID(WINAPI*get)(DWORD)=&FlsGetValue;
        BOOL(WINAPI*set)(DWORD,PVOID)=&FlsSetValue;
        void(WINAPI*bounds)(PULONG_PTR,PULONG_PTR)=&GetCurrentThreadStackLimits;
    };
    Services services{}; // installation-only owned-test failure seam
    std::atomic<std::uint64_t> refused{};
    std::atomic<unsigned> firstReason{};
    bool Init(const void* moduleAddress) {
        const auto error=GetLastError();
        if(slot_!=FLS_OUT_OF_INDEXES){SetLastError(error);return ready_;}
        HMODULE module=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(moduleAddress),&module)){SetLastError(error);return false;}
        retained=true;slot_=services.alloc(nullptr);
        if(slot_!=FLS_OUT_OF_INDEXES)ready_=Current()!=nullptr;
        else Fail(1);
        SetLastError(error);return ready_;
    }
    void Fail(unsigned reason) {
        ++refused;unsigned empty=0;firstReason.compare_exchange_strong(empty,reason);
    }
    // Returns only retained storage; a value is range/alignment checked before access.
    T* Current() {
        const auto error=GetLastError();T* result=nullptr;
        if(slot_==FLS_OUT_OF_INDEXES){Fail(1);SetLastError(error);return nullptr;}
        auto* raw=services.get(slot_);
        const auto value=reinterpret_cast<uintptr_t>(raw),first=reinterpret_cast<uintptr_t>(cells_.data());
        if(raw && (value<first || value>=first+sizeof(cells_) || (value-first)%sizeof(Cell) ||
                (value-first)/sizeof(Cell)>=count_.load())){Fail(2);SetLastError(error);return nullptr;}
        auto* cell=static_cast<Cell*>(raw);
        ULONG_PTR low=0,high=0;services.bounds(&low,&high);
        const auto current=reinterpret_cast<uintptr_t>(_AddressOfReturnAddress());
        const auto key=IsThreadAFiber()?reinterpret_cast<uintptr_t>(GetCurrentFiber()):0;
        const auto thread=GetCurrentThreadId();
        if(high<=low || current<low || current>=high){if(cell)cell->rejected=true;Fail(3);SetLastError(error);return nullptr;}
        if(!cell) {
            auto n=count_.load();
            if(n>=Capacity || !count_.compare_exchange_strong(n,n+1)){Fail(4);SetLastError(error);return nullptr;}
            cell=&cells_[n];cell->thread=thread;cell->high=high;cell->key=key;
            // NULL on a fresh Windows fiber may require its initial FLS array allocation.
            if(!services.set(slot_,cell) || services.get(slot_)!=cell){cell->rejected=true;Fail(5);SetLastError(error);return nullptr;}
        }
        if(cell->thread!=thread){Fail(6);}
        else if(cell->rejected || cell->high!=high || (cell->key && key && cell->key!=key)) {
            cell->rejected=true;Fail(7);
        } else {cell->key=key;result=&cell->data;}
        SetLastError(error);return result;
    }
    // A failed return must not leave an abandoned frame reusable later. Validate
    // the numerical owned-pool address before touching it, without trusting FLS.
    void Abandon(const T* value) {
        const auto error=GetLastError();
        const auto address=reinterpret_cast<uintptr_t>(value);
        const auto first=reinterpret_cast<uintptr_t>(cells_.data())+offsetof(Cell,data);
        if(address>=first && address-first<sizeof(cells_) && (address-first)%sizeof(Cell)==0) {
            const auto index=(address-first)/sizeof(Cell);
            if(index<count_.load() && cells_[index].thread==GetCurrentThreadId())cells_[index].rejected=true;
        }
        Fail(8);SetLastError(error);
    }
    bool Same(const T* value) {
        const auto* current=Current();
        if(current && current==value)return true;
        Abandon(value);return false;
    }
    void Reject(T* value) {Abandon(value);}
    // Numerical validation only. A native stack is never retained/dereferenced.
    bool Anchor(uintptr_t anchor) {
        const auto error=GetLastError();ULONG_PTR low=0,high=0;services.bounds(&low,&high);
        const auto current=reinterpret_cast<uintptr_t>(_AddressOfReturnAddress());
        const bool ok=anchor>=current && anchor>=low && anchor<high;
        SetLastError(error);return ok;
    }
    bool Ready() const{return ready_;}
};
} // namespace kh2coop::inject::tracefiber
