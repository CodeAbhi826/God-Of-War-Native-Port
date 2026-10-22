#pragma once

#include "runtime/gs/gs_types.h"

#include <cstdint>
#include <memory>
#include <vector>

struct GSPreparedPresentation
{
    virtual ~GSPreparedPresentation() = default;
    uint64_t sourceVsyncTick = 0u;
};
using GSPresentationTicket = std::shared_ptr<const GSPreparedPresentation>;

class GSRasterBackend
{
public:
    virtual ~GSRasterBackend() = default;

    virtual void Initialize(uint8_t *vram, uint32_t vramSize) = 0;
    virtual void Reset() = 0;

    virtual void Submit(const GSPrimitiveBatch &batch) = 0;
    // A run of primitives in order. Backends with per-call costs can amortise
    // them; the default preserves the old one-at-a-time behaviour.
    virtual void SubmitMany(const GSPrimitiveBatch *const *batches, size_t count)
    {
        for (size_t i = 0; i < count; ++i)
            Submit(*batches[i]);
    }
    virtual void LoadClut(const GSTex0Reg &tex0, const GSTexClutReg &texclut) = 0;

    virtual void BeginTransfer(const GSTransferCommand &command) = 0;
    virtual void UploadImage(const uint8_t *data, uint32_t sizeBytes) = 0;

    virtual void Flush() = 0;
    virtual void TextureFlush() = 0;
    virtual void Sync(GSSyncReason reason) = 0;
    virtual PresentationFrame Present(const GSPresentationRequest &request) = 0;

    // Optional two-phase presentation for backends that can hand the frontend
    // a prepared ticket and display it on a later thread. The default keeps
    // the single Present() path.
    virtual bool SupportsPreparedPresentation() const { return false; }
    virtual GSPresentationTicket PreparePresentation(const GSPresentationRequest &) { return {}; }
    virtual PresentationFrame DisplayPreparedPresentation(const GSPresentationTicket &) { return {}; }
    virtual void CancelPreparedPresentations() noexcept {}

    // GOW-Port: capacidad efectiva por instancia; consultar no necesita un contexto GL.
    virtual bool UsesSharedPresentation() const { return false; }

    virtual bool ClearFramebuffer(const GSContext &context, uint32_t rgba) = 0;
    virtual uint32_t ConsumeLocalToHostBytes(uint8_t *dst, uint32_t maxBytes) = 0;

    virtual uint32_t ReadVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const = 0;
    virtual void WriteVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value) = 0;
    virtual void SnapshotVram(std::vector<uint8_t> &out) const = 0;
    virtual GSTransferSnapshot GetTransferSnapshot() const = 0;
};
