// Runtime overrides for God of War (PAL, SCES-53133, SCES_531.33).
#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace
{
    // The game's own allocator (heap created at 0x138CB0) owns all RAM from the end of .bss up to
    // 0x1FF7FF8, so the runtime's guest data must not live at the default top of RAM or behind
    // SetupHeap. Kernel memory below 0x100000 is unused by the game: the only things it writes
    // there are its kernel patch blobs at 0x75xxx-0x7Fxxx.
    // The runtime keeps a fixed private heap in that range (same values as the USA override).
    constexpr uint32_t kPrivateHeapBase = 0x000A0000u;
    constexpr uint32_t kPrivateHeapLimit = 0x000FF000u;

    // First sector of layer 1 of the original DVD-9 (where the second layer starts).
    constexpr uint32_t kLayer1StartLbn = 2080544u;

    void writeGuest32(uint8_t *rdram, uint32_t addr, uint32_t value)
    {
        if (addr == 0u)
            return;
        std::memcpy(rdram + (addr & 0x01FFFFFFu), &value, sizeof(value));
    }

    // int sceCdReadDvdDualInfo(int *on_dual, unsigned int *layer1_start)
    // The stripped PAL ELF left this SDK stub unbound by signature; unbound it talks to cdvdfsv
    // (IOP RPC) and hangs. The disc is a DVD-9, so report dual layer and the real layer split.
    void gowCdReadDvdDualInfo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
    {
        writeGuest32(rdram, GPR_U32(ctx, 4), 1u);
        writeGuest32(rdram, GPR_U32(ctx, 5), kLayer1StartLbn);
        SET_GPR_U32(ctx, 2, 1u);
        ctx->pc = GPR_U32(ctx, 31);
    }

    // sceSifLoadStartModuleBuffer(iopAddr, argLen, args, int *result), used by sceChk01 (0x2841E0)
    // to run the embedded "ck01" IRX; the game halts at 0x186080 unless it loads and reports success.
    // The runtime cannot yet execute modules loaded from IOP memory, so answer like a retail console:
    // the module loads (valid id) and ends NO_RESIDENT_END (1), the value the game accepts.
    void gowLoadStartModuleBuffer(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
    {
        writeGuest32(rdram, GPR_U32(ctx, 7), 1u);
        SET_GPR_U32(ctx, 2, 0x40001000u);
        ctx->pc = GPR_U32(ctx, 31);
    }

    // GOW_ISO=<image> selects the original disc image for raw sector reads. Without it the runtime
    // reads the files extracted next to the ELF (.disc_layout), as on the PC build.
    void configureGowCdImage()
    {
        namespace fs = std::filesystem;
        PS2Runtime::IoPaths paths = PS2Runtime::getIoPaths();
        if (!paths.cdImage.empty())
            return;
        const char *env = std::getenv("GOW_ISO");
        if (env == nullptr || env[0] == '\0')
            return;
        const fs::path image = fs::u8path(env);
        std::error_code ec;
        if (!fs::is_regular_file(image, ec))
        {
            std::fprintf(stderr, "[gow-cd] GOW_ISO is not a file; using the extracted files\n");
            return;
        }
        paths.cdImage = image;
        PS2Runtime::setIoPaths(paths);
        std::fprintf(stderr, "[gow-cd] disc image (GOW_ISO): %s\n", env);
    }

    void applyGodOfWar(PS2Runtime &runtime)
    {
        configureGowCdImage();
        runtime.setPrivateGuestHeap(kPrivateHeapBase, kPrivateHeapLimit);

        // SDK functions the stripped-ELF analyzer could not bind by signature. Unbound, they talk to IOP
        // RPC services (cdvdfsv, loadfile) that the HLE IOP does not provide and hang or fail.
        if (!runtime.replaceFunction(0x0027B710u, gowCdReadDvdDualInfo))
            std::fprintf(stderr, "[gow-override] pal: sceCdReadDvdDualInfo address not mapped\n");
        if (!runtime.replaceFunction(0x00299928u, gowLoadStartModuleBuffer))
            std::fprintf(stderr, "[gow-override] pal: sceSifLoadStartModuleBuffer address not mapped\n");
    }
}

PS2_REGISTER_GAME_OVERRIDE("God of War (PAL)", "SCES_531.33", 0x00100008u, 0xAE7DC7F4u, applyGodOfWar);
