#include "Overlay/VrPanel.hpp"
#include "Camera/ImagePoseIdentity.hpp"
#include <dxgi1_6.h>
#include <d3d12sdklayers.h>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <array>
using Microsoft::WRL::ComPtr;
using namespace cvr::vrui;
void Log(const char*,...) {}
void Check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
void Hr(HRESULT hr) {Check(SUCCEEDED(hr),"D3D12 failure");}
namespace mock {
ID3D12Device* device{};
std::array<ComPtr<ID3D12Resource>,3> textures;
bool visible=true,acquired{},waited{},timeout{};
uint32_t nextImage{},image{},acquires{},releases{},destroys{};
XrDuration lastTimeout{};
}
namespace cvr::vrui {
bool Visible() {return mock::visible;}
View GetView() {View v;v.valid=true;v.width=1.5f;v.height=1;return v;}
}
namespace cvr::camera {
std::recursive_mutex& ImageSubmissionMutex() {static std::recursive_mutex mutex;return mutex;}
}
extern "C" {
XrResult XRAPI_CALL xrEnumerateSwapchainFormats(XrSession,uint32_t capacity,uint32_t* count,int64_t* values) {
    *count=1;if(capacity)values[0]=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;return XR_SUCCESS;
}
XrResult XRAPI_CALL xrCreateSwapchain(XrSession,const XrSwapchainCreateInfo* ci,XrSwapchain* out) {
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width=ci->width;desc.Height=ci->height;desc.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;
    desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    for(auto& texture:mock::textures)Hr(mock::device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
        D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&texture)));
    *out=reinterpret_cast<XrSwapchain>(1);mock::nextImage=0;return XR_SUCCESS;
}
XrResult XRAPI_CALL xrEnumerateSwapchainImages(XrSwapchain,uint32_t capacity,uint32_t* count,XrSwapchainImageBaseHeader* out) {
    *count=3;if(capacity)for(unsigned i=0;i<3;++i)reinterpret_cast<XrSwapchainImageD3D12KHR*>(out)[i].texture=mock::textures[i].Get();return XR_SUCCESS;
}
XrResult XRAPI_CALL xrAcquireSwapchainImage(XrSwapchain,const XrSwapchainImageAcquireInfo*,uint32_t* image) {
    Check(!mock::acquired,"duplicate acquisition");mock::acquired=true;mock::waited=false;
    *image=mock::image=mock::nextImage++%3;++mock::acquires;return XR_SUCCESS;
}
XrResult XRAPI_CALL xrWaitSwapchainImage(XrSwapchain,const XrSwapchainImageWaitInfo* info) {
    Check(mock::acquired,"wait without acquire");mock::lastTimeout=info->timeout;
    if(mock::timeout)return XR_TIMEOUT_EXPIRED;mock::waited=true;return XR_SUCCESS;
}
XrResult XRAPI_CALL xrReleaseSwapchainImage(XrSwapchain,const XrSwapchainImageReleaseInfo*) {
    Check(mock::acquired && mock::waited,"unwaited image released");mock::acquired=false;++mock::releases;return XR_SUCCESS;
}
XrResult XRAPI_CALL xrDestroySwapchain(XrSwapchain) {
    Check(!mock::acquired,"acquired image destroyed");++mock::destroys;
    for(auto& texture:mock::textures)texture.Reset();return XR_SUCCESS;
}
}

// The fixture waits only at observation points; production Prepare must not wait on the producer.
void Drain(ID3D12Device* device,ID3D12CommandQueue* queue) {
    ComPtr<ID3D12Fence> fence;Hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    Hr(queue->Signal(fence.Get(),1));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    Hr(fence->SetEventOnCompletion(1,event));const auto status=WaitForSingleObject(event,5000);CloseHandle(event);
    Check(status==WAIT_OBJECT_0,"GPU timeout");
}
// Observe the actual released swapchain texel after the real ColorBlit shader runs.
void Pixel(ID3D12Device* device,ID3D12CommandQueue* queue,unsigned channel) {
    auto texture=mock::textures[mock::image];auto desc=texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes{};
    device->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&bytes);
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=bytes;
    buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> readback;Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
    Hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    Hr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={texture.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};
    list->ResourceBarrier(1,&b);
    D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprint;
    src.pResource=texture.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(1,&b);
    Hr(list->Close());ID3D12CommandList* commands[]={list.Get()};queue->ExecuteCommandLists(1,commands);Drain(device,queue);
    unsigned char* data{};Hr(readback->Map(0,nullptr,reinterpret_cast<void**>(&data)));
    const bool correct=data[channel]>250 && data[3]>250 && data[(channel+1)%3]<5 && data[(channel+2)%3]<5;
    readback->Unmap(0,nullptr);Check(correct,"released image contains wrong canvas");
}
// Build an actual producer draw with its own queue, independently gated by the test.
struct Producer {
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    std::shared_ptr<Canvas> canvas;
    Producer(ID3D12Device* device,ID3D12CommandQueue* queue,unsigned channel) {
        canvas=AcquireCanvas(device,DXGI_FORMAT_R8G8B8A8_UNORM);Check(bool(canvas),"pool exhausted unexpectedly");
        Hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
        Hr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
        Hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
        D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition={canvas->texture.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET};
        list->ResourceBarrier(1,&b);float color[4]={0,0,0,1};color[channel]=1;
        list->ClearRenderTargetView(canvas->rtv->GetCPUDescriptorHandleForHeapStart(),color,0,nullptr);
        std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(1,&b);Hr(list->Close());
        ID3D12CommandList* commands[]={list.Get()};queue->ExecuteCommandLists(1,commands);
        Hr(queue->Signal(fence.Get(),1));PublishCanvas(canvas,fence.Get(),1);
    }
};
int main() try {
    ComPtr<ID3D12Debug> debug;
    if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
    else std::puts("D3D12 debug layer unavailable");
    ComPtr<IDXGIFactory4> factory;Hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter> warp;Hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
    ComPtr<ID3D12Device> device;Hr(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));mock::device=device.Get();
    D3D12_COMMAND_QUEUE_DESC qd{};ComPtr<ID3D12CommandQueue> producer,runtime;
    Hr(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&producer)));Hr(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&runtime)));
    Panel panel;
    auto prepare=[&] {
        auto start=std::chrono::steady_clock::now();
        auto result=panel.Prepare(reinterpret_cast<XrSession>(1),reinterpret_cast<XrSpace>(1),device.Get(),runtime.Get());
        Check(std::chrono::steady_clock::now()-start<std::chrono::seconds(3),"Prepare blocked on pending producer");return result;
    };
    ComPtr<ID3D12Fence> gate;Hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));
    Hr(producer->Wait(gate.Get(),1));
    Producer first(device.Get(),producer.Get(),0);
    Check(!prepare() && mock::acquires==0,"unfinished first canvas submitted");
    Hr(gate->Signal(1));Drain(device.Get(),producer.Get());
    Check(prepare()!=nullptr,"finished first canvas refused");Drain(device.Get(),runtime.Get());Pixel(device.Get(),runtime.Get(),0);
    Hr(producer->Wait(gate.Get(),2));Producer pending(device.Get(),producer.Get(),1);
    const auto copies=mock::releases;
    Check(prepare()!=nullptr && mock::releases==copies,"pending canvas replaced valid panel");
    Pixel(device.Get(),runtime.Get(),0);
    // A new consumer must select the older finished publication even though the newest is pending.
    panel.Shutdown();
    PublishCanvas(first.canvas,first.fence.Get(),1);
    PublishCanvas(pending.canvas,pending.fence.Get(),1);
    Check(prepare()!=nullptr,"finished fallback missing");
    Drain(device.Get(),runtime.Get());Pixel(device.Get(),runtime.Get(),0);
    Hr(gate->Signal(2));Drain(device.Get(),producer.Get());
    Check(prepare()!=nullptr,"completed update missing");
    Drain(device.Get(),runtime.Get());Pixel(device.Get(),runtime.Get(),1);
    ClearCanvas();const auto releases=mock::releases;
    Check(prepare()!=nullptr && mock::releases==releases,"last image lost without ready canvas");
    mock::visible=false;prepare();
    mock::visible=true;Check(!prepare(),"closed panel resurrected old publication");
    first.canvas.reset();pending.canvas.reset();
    Producer reopened(device.Get(),producer.Get(),2);Drain(device.Get(),producer.Get());
    Check(prepare()!=nullptr,"reopened panel failed");Drain(device.Get(),runtime.Get());Pixel(device.Get(),runtime.Get(),2);
    // A producer-owned slot is unpublished immediately, before it has a new fence.
    reopened.canvas.reset();Drain(device.Get(),runtime.Get());prepare();
    auto writing=AcquireCanvas(device.Get(),DXGI_FORMAT_R8G8B8A8_UNORM);
    Check(bool(writing),"idle canvas unavailable");
    const auto beforeWriting=mock::releases;
    Check(prepare()!=nullptr && mock::releases==beforeWriting,"unpublished producer slot selected");
    // Exhausted leases cannot be stolen by a later producer.
    auto leased=AcquireCanvas(device.Get(),DXGI_FORMAT_R8G8B8A8_UNORM);
    auto third=AcquireCanvas(device.Get(),DXGI_FORMAT_R8G8B8A8_UNORM);
    Check(leased && third && !AcquireCanvas(device.Get(),DXGI_FORMAT_R8G8B8A8_UNORM),"leased pool slot reused");
    writing.reset();leased.reset();third.reset();
    panel.Shutdown();ClearCanvas();std::puts("VR panel synchronization passed");return 0;
} catch(const std::exception& e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
