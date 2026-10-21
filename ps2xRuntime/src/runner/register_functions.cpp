// Public placeholder for the generated recompiled-function table.
//
// The real file is produced by ps2xRecomp from the user's own game ELF and
// contains the dense address -> native function mapping. It is intentionally
// kept out of this repository so the public tree contains no game-derived
// data. The private engine builder replaces this file before compiling the
// release APK.
//
// An empty table lets the runtime and Android shell compile and launch; the
// runtime will report a missing function when it tries to execute guest code.

#include "ps2_runtime.h"

extern const uint32_t g_ps2RecompiledFunctionTableBase = 0u;
extern const uint32_t g_ps2RecompiledFunctionTableEnd = 0u;
extern const uint32_t g_ps2RecompiledFunctionTableSlotCount = 0u;
PS2Runtime::RecompiledFunction g_ps2RecompiledFunctionTable[1] = {};
