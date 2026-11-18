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
