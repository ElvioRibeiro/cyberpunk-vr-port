// Integration regression: real Afw.cpp/Osr.cpp on D3D12 WARP, including native reset propagation.
#include "Core/Paths.hpp"
#include "Framegen/Inputs.hpp"
#include "Render/Afw.hpp"
#include "Render/Osr.hpp"
#include <dxgi1_6.h>
#include <d3d12sdklayers.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <thread>

using Microsoft::WRL::ComPtr;
extern "C" std::atomic<int64_t> CyberpunkVR_OsrStats[3];
extern "C" int32_t CyberpunkVR_AfwTune[8];

// Checks stay active in Release, where the shipped shaders are normally compiled.
static void Check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

// Fail on setup/recording errors rather than allowing a missing GPU operation to pass an assertion.
static void Require(HRESULT result, const char* message) {
    if (FAILED(result)) {
        std::fprintf(stderr, "HRESULT 0x%08lx: %s\n", static_cast<unsigned long>(result), message);
        std::exit(1);
    }
}

// Only process services are stubbed; adapter selection, history and reconstruction remain production code.
void Log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vfprintf(stdout, format, args);
    va_end(args);
}
namespace cvr::framegen {
bool ReadRenderFrameIndex(uint32_t*) { Check(false, "test must not read the game clock"); return false; }
}
namespace cvr::paths {
std::wstring File(Folder, const wchar_t*) {
    Check(false, "OSR tests must not load a PureDark DLL");
    return {};
}
}

namespace {
constexpr UINT kWidth = 128, kHeight = 64;
constexpr uint32_t kRed = 0xff0000ff, kBlue = 0xffff0000;

struct Result { bool usedHistory; std::vector<uint32_t> pixels; };
struct FrameOptions {
    uint32_t advance = 1;
    bool submit = true;
    bool invalidateAfterRecording = false;
    UINT width = kWidth, height = kHeight;
    float ipd = .064f, fov = .7f;
};

// Each recording is completed before reusing its allocator or releasing upload/readback operands.
class Fixture {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12InfoQueue> messages;
    UINT64 fenceValue = 0;
    uint32_t frameId = 0;
    HANDLE event = nullptr;
    std::vector<ComPtr<ID3D12Resource>> uploads;

    // Textures are initialized through real copies, with their production entry states.
    ComPtr<ID3D12Resource> Texture(DXGI_FORMAT format, const void* pixels, UINT pixelBytes,
                                  D3D12_RESOURCE_STATES state, UINT width, UINT height) {
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width; desc.Height = height;
        desc.DepthOrArraySize = desc.MipLevels = 1; desc.SampleDesc.Count = 1;
        desc.Format = format;
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        ComPtr<ID3D12Resource> texture;
        Require(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture)), "create texture");
        if (pixels) {
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
            UINT64 bytes = 0;
            device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
            auto upload = Buffer(bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
            void* mapped = nullptr;
            Require(upload->Map(0, nullptr, &mapped), "map upload");
            for (UINT y = 0; y < height; ++y)
                std::memcpy(static_cast<unsigned char*>(mapped) + footprint.Offset + y * footprint.Footprint.RowPitch,
                    static_cast<const unsigned char*>(pixels) + y * width * pixelBytes, width * pixelBytes);
            upload->Unmap(0, nullptr);
            D3D12_TEXTURE_COPY_LOCATION from{}; from.pResource = upload.Get();
            from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; from.PlacedFootprint = footprint;
            D3D12_TEXTURE_COPY_LOCATION to{}; to.pResource = texture.Get();
            to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
            uploads.push_back(upload);
        }
        Transition(texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, state);
        return texture;
    }

    // Allocate upload/readback memory owned by this fixture until its fence completes.
    ComPtr<ID3D12Resource> Buffer(UINT64 bytes, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state) {
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; desc.Width = bytes;
        desc.Height = 1; desc.DepthOrArraySize = desc.MipLevels = 1; desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = type;
        ComPtr<ID3D12Resource> buffer;
        Require(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state,
            nullptr, IID_PPV_ARGS(&buffer)), "create buffer");
        return buffer;
    }

    // Track only fixture resources; production OSR keeps its own transition bookkeeping.
    void Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
        if (before == after) return;
        D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = before; barrier.Transition.StateAfter = after;
        list->ResourceBarrier(1, &barrier);
    }

public:
    // Initialize a software GPU and optional validation without touching a game or XR runtime.
    Fixture() {
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
        ComPtr<IDXGIFactory4> factory; ComPtr<IDXGIAdapter> adapter;
        Require(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "DXGI factory");
        Require(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)), "WARP adapter");
        Require(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)), "WARP device");
        device.As(&messages);
        std::printf("D3D12 validation: %s\n", messages ? "enabled" : "unavailable");
        D3D12_COMMAND_QUEUE_DESC desc{};
        Require(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)), "queue");
        Require(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "allocator");
        Require(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
            IID_PPV_ARGS(&list)), "command list");
        Require(list->Close(), "close initial list");
        Require(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "fence");
        event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        Check(event != nullptr, "fence event");
        cvr::afw::SetLaunch(true);
        cvr::afw::SetMode(cvr::afw::kModeOsr);
    }

    ~Fixture() { if (event) CloseHandle(event); }

    // A plane at two metres leaves a narrow disoccluded strip that can expose stale same-depth colour.
    Result Frame(unsigned eye, uint32_t colour, bool reset, FrameOptions options = {}) {
        cvr::osr::HistoryCapture capture(true);
        const auto width = options.width, height = options.height;
        Require(allocator->Reset(), "reset allocator");
        Require(list->Reset(allocator.Get(), nullptr), "reset list");
        if (messages) messages->ClearStoredMessages();
        std::vector<uint32_t> pixels(width * height, colour);
        const float nearPlane = .02f, farPlane = 16000.f, distance = 2.f;
        const float depthValue = nearPlane * farPlane / ((farPlane - nearPlane) * distance)
            - nearPlane / (farPlane - nearPlane);
        std::vector<float> depths(width * height, depthValue);
        std::vector<float> motions(width * height * 2, 0);
        auto source = Texture(DXGI_FORMAT_R8G8B8A8_UNORM, pixels.data(), 4, D3D12_RESOURCE_STATE_PRESENT, width, height);
        auto depth = Texture(DXGI_FORMAT_R32_FLOAT, depths.data(), 4, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, width, height);
        auto motion = Texture(DXGI_FORMAT_R32G32_FLOAT, motions.data(), 8, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, width, height);
        auto output = Texture(DXGI_FORMAT_R8G8B8A8_UNORM, nullptr, 4, D3D12_RESOURCE_STATE_COPY_SOURCE, width, height);
        cvr::framegen::Inputs inputs{};
        inputs.depth = depth; inputs.motion = motion; inputs.hasDepth = inputs.hasMotion = inputs.submitted = true;
        inputs.queue = queue.Get(); inputs.camera.reset = reset;
        frameId += options.advance;
        inputs.frameId = frameId;
        inputs.camera.position[0] = (eye == 0 ? -.5f : .5f) * options.ipd;
        inputs.camera.right[0] = inputs.camera.up[1] = inputs.camera.forward[2] = 1;
        inputs.camera.nearPlane = nearPlane; inputs.camera.farPlane = farPlane; inputs.camera.inverted = true;
        cvr::afw::WarpRequest request{};
        request.device = device.Get(); request.queue = queue.Get(); request.list = list.Get();
        request.drawn = source.Get(); request.out = output.Get(); request.inputs = &inputs; request.eye = eye;
        request.fov[0] = request.fov[1] = {-options.fov, options.fov, options.fov, -options.fov}; request.ipd = options.ipd;
        const auto before = CyberpunkVR_OsrStats[2].load();
        Check(cvr::afw::Warp(request), "production adapter records OSR");
        Result result{CyberpunkVR_OsrStats[2].load() != before, {}};
        if (options.invalidateAfterRecording) {
            std::thread lifecycle([] { cvr::osr::InvalidateHistory(); });
            lifecycle.join();
        }
        if (!options.submit) {
            Require(list->Close(), "discard recorded capture");
            uploads.clear();
            return result; // Same guard as production: a recorded-but-unpublished capture invalidates history.
        }
        auto desc = output->GetDesc(); D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}; UINT64 bytes = 0;
        device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
        auto readback = Buffer(bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION to{}; to.pResource = readback.Get();
        to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; to.PlacedFootprint = footprint;
        D3D12_TEXTURE_COPY_LOCATION from{}; from.pResource = output.Get(); from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        Require(list->Close(), "close frame"); ID3D12CommandList* commands[] = {list.Get()};
        queue->ExecuteCommandLists(1, commands);
        Require(queue->Signal(fence.Get(), ++fenceValue), "signal frame");
        Require(fence->SetEventOnCompletion(fenceValue, event), "arm frame fence");
        Check(WaitForSingleObject(event, 10000) == WAIT_OBJECT_0 && fence->GetCompletedValue() >= fenceValue,
            "GPU frame completed before reuse/readback");
        void* mapped = nullptr; Require(readback->Map(0, nullptr, &mapped), "map result");
        result.pixels.resize(width * height);
        for (UINT y = 0; y < height; ++y)
            std::memcpy(result.pixels.data() + y * width,
                static_cast<unsigned char*>(mapped) + footprint.Offset + y * footprint.Footprint.RowPitch, width * 4);
        readback->Unmap(0, nullptr); uploads.clear();
        if (messages) {
            for (UINT64 i = 0; i < messages->GetNumStoredMessages(); ++i) {
                SIZE_T length = 0; messages->GetMessage(i, nullptr, &length);
                std::vector<unsigned char> storage(length);
                auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
                Require(messages->GetMessage(i, message, &length), "read validation message");
                if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
                    std::fprintf(stderr, "%s\n", message->pDescription);
                    Check(false, "no D3D12 validation errors");
                }
            }
        }
        capture.Commit();
        return result;
    }

    // Place the synthetic engine clock near wrap without changing production continuity rules.
    void SetFrameId(uint32_t value) { frameId = value; }
};

// Exact primary colours make stale-history contamination distinguishable from spatial fill.
size_t RedPixels(const Result& frame) {
    size_t count = 0;
    for (uint32_t pixel : frame.pixels) if ((pixel & 0xff) > 32) ++count;
    return count;
}

// A safe fallback must still reconstruct the current scene, not merely suppress all output.
void CheckBlue(const Result& frame) {
    Check(!frame.pixels.empty(), "current scene output is present");
    for (uint32_t pixel : frame.pixels) Check(pixel == kBlue, "output contains current blue scene only");
}
}

// Run the negative control first, then require native reset propagation and normal history recovery.
int main() {
    Fixture fixture;
    Check(!fixture.Frame(1, kRed, false).usedHistory, "first frame has no history");
    const auto stale = fixture.Frame(0, kBlue, false);
    Check(stale.usedHistory, "ordinary alternating capture uses history");
    Check(RedPixels(stale) > 0, "negative control exposes old colour at disocclusions");
    fixture.Frame(1, kRed, false);
    const auto reset = fixture.Frame(0, kBlue, true);
    Check(!reset.usedHistory, "native camera reset must reach production OSR");
    Check(RedPixels(reset) == 0, "reset must not show old same-depth scene colour");
    CheckBlue(reset);
    Check(fixture.Frame(1, kBlue, false).usedHistory, "next capture recovers fresh history");
    Check(!fixture.Frame(0, kBlue, true).usedHistory, "first persistent reset invalidates");
    Check(!fixture.Frame(1, kBlue, true).usedHistory, "persistent reset remains level-triggered");
    Check(fixture.Frame(0, kBlue, false).usedHistory, "history resumes when reset stops");
    CyberpunkVR_AfwTune[0] = 1;
    Check(!fixture.Frame(1, kBlue, true).usedHistory, "reset does not override history-disabled mode");
    Check(!fixture.Frame(0, kBlue, false).usedHistory, "history-disabled mode stays disabled");
    CyberpunkVR_AfwTune[0] = 3;
    const auto steady = fixture.Frame(1, kBlue, false);
    Check(steady.usedHistory && steady.pixels == reset.pixels, "steady uniform scene matches reset output");

    // A reset missed during a lost engine frame cannot leave the old scene eligible on resumption.
    const auto gap = fixture.Frame(0, kBlue, false, {.advance = 2});
    Check(!gap.usedHistory, "frame gap invalidates even without an observed native reset");
    CheckBlue(gap);
    Check(fixture.Frame(1, kBlue, false).usedHistory, "history recovers after frame gap");
    Check(!fixture.Frame(0, kBlue, false, {.advance = 0}).usedHistory, "duplicate engine frame is not new history");
    Check(fixture.Frame(1, kBlue, false).usedHistory, "history recovers after duplicate");
    Check(!fixture.Frame(1, kBlue, false).usedHistory, "same-eye repetition rejects older opposite-eye history");
    Check(fixture.Frame(0, kBlue, false).usedHistory, "normal alternation recovers after same eye");
    Check(!fixture.Frame(1, kBlue, false, {.advance = UINT32_MAX}).usedHistory, "backward engine clock invalidates");
    fixture.SetFrameId(UINT32_MAX - 2);
    Check(!fixture.Frame(0, kBlue, false).usedHistory, "jump near clock wrap invalidates");
    Check(fixture.Frame(1, kBlue, false).usedHistory, "last Uint32 frame remains consecutive");
    Check(fixture.Frame(0, kBlue, false).usedHistory, "natural Uint32 wrap preserves continuity");

    // Exercise the same guard as menu/backend/failed-capture exits in OpenXRCapture.cpp.
    { cvr::osr::HistoryCapture interruption(true); }
    Check(!fixture.Frame(1, kBlue, false).usedHistory, "menu/non-OSR capture leaves a pending invalidation");
    Check(fixture.Frame(0, kBlue, false).usedHistory, "history recovers after interruption");
    cvr::afw::SetMode(cvr::afw::kModeAfw);
    cvr::afw::SetMode(cvr::afw::kModeOsr);
    Check(!fixture.Frame(1, kBlue, false).usedHistory, "backend round trip invalidates without extra rendering");
    cvr::afw::SetMode(cvr::afw::kModeOsr);
    Check(fixture.Frame(0, kBlue, false).usedHistory, "unchanged backend does not keep resetting");

    // Recorded GPU commands may be discarded before publication; their CPU metadata must not become history.
    fixture.Frame(1, kRed, false);
    fixture.Frame(0, kRed, false);
    fixture.Frame(1, kBlue, false, {.submit = false});
    const auto resumed = fixture.Frame(0, kBlue, false);
    Check(!resumed.usedHistory, "discarded command list invalidates recorded history");
    CheckBlue(resumed);
    Check(fixture.Frame(1, kBlue, false).usedHistory, "history recovers after discarded recording");
    std::thread lifecycle([] { cvr::osr::InvalidateHistory(); });
    lifecycle.join();
    Check(!fixture.Frame(0, kBlue, false).usedHistory, "session-thread request is consumed by capture thread");
    Check(fixture.Frame(1, kBlue, false).usedHistory, "session request is consumed only once");
    const auto optics = fixture.Frame(0, kBlue, false, {.ipd = .070f, .fov = .65f});
    Check(optics.usedHistory, "FOV/IPD changes alone preserve world-space history");
    CheckBlue(optics);
    Check(!fixture.Frame(1, kBlue, false, {.width = 64, .height = 32}).usedHistory, "resize invalidates both eyes");
    Check(fixture.Frame(0, kBlue, false, {.width = 64, .height = 32}).usedHistory, "resize recovers normally");
    {
        Fixture replacementDevice;
        Check(!replacementDevice.Frame(1, kBlue, false).usedHistory, "new device starts with invalid history");
        Check(replacementDevice.Frame(0, kBlue, false).usedHistory, "new device builds fresh history");
        cvr::afw::SetLaunch(false);
        cvr::afw::SetLaunch(true);
        Check(!replacementDevice.Frame(1, kBlue, false).usedHistory, "launch off/on invalidates prior history");
        Check(replacementDevice.Frame(0, kBlue, false).usedHistory, "launch resumes ordinary history");
        Check(!cvr::afw::Warp({}), "invalid active capture falls back");
        Check(!replacementDevice.Frame(1, kBlue, false).usedHistory, "fallback invalidates the next capture");
        { cvr::osr::HistoryCapture inactive(false); }
        Check(replacementDevice.Frame(0, kBlue, false).usedHistory, "inactive capture guard has no side effects");
        replacementDevice.Frame(1, kBlue, false, {.invalidateAfterRecording = true});
        Check(!replacementDevice.Frame(0, kBlue, false).usedHistory, "request after Record is not lost by Commit");
        Check(replacementDevice.Frame(1, kBlue, false).usedHistory, "late request is consumed once");
    }
    cvr::afw::SetLaunch(false);
    Check(!cvr::afw::Warp({}), "disabled launch does not record OSR");
    std::printf("PASS: stale control %zu pixels, native reset, continuity/wrap, interruption, backend, discarded recording, session, optics, resize and device\n",
        RedPixels(stale));
}
