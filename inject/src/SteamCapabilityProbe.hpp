#pragma once
#include <Windows.h>

namespace kh2coop::steamprobe {
// Existing init worker only, outside loader lock. Default off. Stops on the
// existing shutdown event; never initializes/shuts down Steam or opens a peer.
void Run(HANDLE stopEvent) noexcept;
}
