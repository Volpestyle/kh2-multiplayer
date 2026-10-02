#pragma once
// ============================================================================
// CaptureChannel — screenshot / clip / overlay requests from kh2ctl to the
// inject DLL's Present hook.
//
// Naming: "Local\kh2coop_capture_<KH2_PID>". The inject DLL creates the
// mapping at init; kh2ctl opens it, fills a request and bumps requestSeq.
// The DLL captures on its next presented frames, writes the files from a
// worker thread, then sets doneSeq = requestSeq. kh2ctl waits for that.
//
// One request at a time per instance (kh2ctl calls are serialized by the
// caller). Fields the DLL writes are marked [dll]; the rest are [client].
// ============================================================================

#include <cstdint>

namespace kh2coop {

constexpr const wchar_t* CAPTURE_NAME_PREFIX = L"Local\\kh2coop_capture_";
constexpr std::uint32_t CAPTURE_MAGIC = 0x50433248;  // "H2CP"
constexpr std::uint32_t CAPTURE_VERSION = 1;

enum class CaptureStatus : std::int32_t {
    Ok = 0,
    Busy = 1,           // a capture was already running
    Unsupported = 2,    // renderer or backbuffer format not supported
    GpuError = 3,       // a D3D call failed
    WriteError = 4,     // encoding or file write failed
};

#pragma pack(push, 4)
struct CaptureChannel {
    std::uint32_t magic;            // [dll] CAPTURE_MAGIC
    std::uint32_t version;          // [dll] CAPTURE_VERSION
    volatile long requestSeq;       // [client] bumped after the request is filled
    volatile long doneSeq;          // [dll] = requestSeq when the request finished
    std::int32_t status;            // [dll] CaptureStatus of the last request
    std::uint32_t frameCount;       // [client] frames to capture (1 = screenshot)
    std::uint32_t frameInterval;    // [client] capture every Nth presented frame
    std::uint32_t framesWritten;    // [dll] files written by the last request
    std::uint32_t width;            // [dll] captured image size
    std::uint32_t height;
    volatile long overlay;          // [client] 1 = draw the debug overlay
    volatile long presentCount;     // [dll] incremented every Present (fps probe)
    std::uint32_t renderer;         // [dll] 0 unknown, 11 = D3D11, 12 = D3D12
    std::uint32_t backbufferFormat; // [dll] DXGI_FORMAT of the backbuffer
    // [client] output path. For frameCount == 1 the exact .png path; otherwise
    // a printf pattern with one %u for the frame index, e.g. ...\f_%05u.bmp.
    wchar_t output[520];
    wchar_t error[256];             // [dll] message when status != Ok
};
#pragma pack(pop)

} // namespace kh2coop
