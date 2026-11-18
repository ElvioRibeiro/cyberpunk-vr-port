#include <windows.h>
#include <cstdio>
extern "C" { __declspec(dllexport) volatile LONG CyberpunkVR_FixedVrsReportSeq=0;
__declspec(dllexport) char CyberpunkVR_FixedVrsReport[1024]="{}"; }
// Publish predictable modes and counters through the same odd/even protocol as the real port.
extern "C" __declspec(dllexport) void tick(int frame){
InterlockedIncrement(&CyberpunkVR_FixedVrsReportSeq);
const int mode=(frame/10)%2;
std::snprintf(CyberpunkVR_FixedVrsReport,1024,"{\"requested\":%d,\"applied\":%d,\"stereo\":1,\"draws\":[%d,%d]}",mode,mode,frame*2,frame*3);
InterlockedIncrement(&CyberpunkVR_FixedVrsReportSeq); }

extern "C" {
__declspec(dllexport) volatile LONG CyberpunkVR_SceneGpuRequest=0,CyberpunkVR_SceneGpuFrameRequest=0;
__declspec(dllexport) volatile LONG CyberpunkVR_SceneGpuGroupMode=0,CyberpunkVR_SceneGpuNodeFilter=0;
__declspec(dllexport) volatile LONG CyberpunkVR_SceneGpuViewportDetail=0,CyberpunkVR_SceneGpuPipelineStats=0,CyberpunkVR_SceneGpuDrawOcclusion=0;
__declspec(dllexport) volatile LONG CyberpunkVR_SceneGpuReportSeq=0;
__declspec(dllexport) char CyberpunkVR_SceneGpuReport[262144]="{\"active\":false}";
}
// Simulate consumption of a profiler request so permission/restoration paths can be tested without a GPU hook.
extern "C" __declspec(dllexport) void profileTick(){
    if(InterlockedExchange(&CyberpunkVR_SceneGpuFrameRequest,0)){
        InterlockedIncrement(&CyberpunkVR_SceneGpuReportSeq);
        std::snprintf(CyberpunkVR_SceneGpuReport,sizeof(CyberpunkVR_SceneGpuReport),"{\"active\":false,\"mode\":\"frames\",\"frames\":4,\"completed\":1,\"discarded\":0,\"pending\":0,\"recordings\":0,\"exhausted\":false,\"groups\":[{\"node\":3399076,\"sum_ms\":4.0,\"ps\":[0,0,0,0,0,0,0,100,0,0,0]}]}");
        InterlockedIncrement(&CyberpunkVR_SceneGpuReportSeq);
    }
}
