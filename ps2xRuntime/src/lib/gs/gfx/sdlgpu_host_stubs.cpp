// Host-side glue for the SDL3-GPU GS backend.
//
// The DQ8 backend expects a handful of process-wide counters that its own
// runtime publishes. GoW's runtime does not have all of them; zero-valued
// atomics keep the optional profiling path linkable and inert.
#include "gfx/backends/sdlgpu/sdlgpu_window.h"

#include <atomic>
#include <cstdint>

namespace dq8::gfx
{
    // The Android build has no desktop window and therefore no icon to set.
    void setWindowIcon(SDL_Window *) {}
}

extern "C++"
{
    std::atomic<bool> g_gsFrontendTiming{false};
    std::atomic<uint64_t> g_gsFrontendPacketNanos{0};
    std::atomic<uint64_t> g_gsFrontendPacketCount{0};
    std::atomic<uint64_t> g_gsUploadNativeNanos{0};
    std::atomic<uint64_t> g_gsUploadNativeCount{0};

    std::atomic<uint64_t> g_mpegGetPictureNanos{0};
    std::atomic<uint64_t> g_mpegGetPictureCount{0};
    std::atomic<uint64_t> g_mpegWriteFrameNanos{0};
    std::atomic<uint64_t> g_mpegWriteFrameCount{0};
    std::atomic<uint64_t> g_mpegDemuxNanos{0};
    std::atomic<uint64_t> g_mpegDemuxCount{0};
    std::atomic<uint64_t> g_mpegDemuxRefusedCount{0};
    std::atomic<uint64_t> g_mpegPendingEsPeakBytes{0};
    std::atomic<uint64_t> g_eeGuestDispatchCount{0};
    std::atomic<uint64_t> g_eeTransferThrowCount{0};
    std::atomic<uint64_t> g_eeTransferSuspendCount{0};
    std::atomic<uint64_t> g_eeRunLoopIterations{0};
    std::atomic<uint64_t> g_eeRunLoopResumeCount{0};
    std::atomic<uint64_t> g_eeProcessPendingEventsCount{0};
    std::atomic<uint64_t> g_eeEnterGuestCount{0};
    std::atomic<uint32_t> g_mpegDemuxThreadId{0};
    std::atomic<uint32_t> g_mpegGetPictureThreadId{0};
}
