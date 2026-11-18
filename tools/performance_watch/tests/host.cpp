#include <windows.h>
#include <cstring>
// Publish changing, frozen or deliberately busy reports for actual cross-process reader tests.
int main(int argc,char** argv){
    auto module=LoadLibraryW(L"cyberpunkvrport.dll");if(!module)return 1;
    auto tick=reinterpret_cast<void(*)(int)>(GetProcAddress(module,"tick"));if(!tick)return 2;
    if(argc>1 && std::strcmp(argv[1],"busy")==0){
        auto seq=reinterpret_cast<volatile LONG*>(GetProcAddress(module,"CyberpunkVR_FixedVrsReportSeq"));
        if(!seq)return 3;InterlockedExchange(seq,1);Sleep(10000);return 0;
    }
    if(argc>1 && std::strcmp(argv[1],"stale")==0){tick(5);Sleep(10000);return 0;}
    auto profileTick=reinterpret_cast<void(*)()>(GetProcAddress(module,"profileTick"));
    for(int i=0;i<150;++i){tick(i);if(profileTick)profileTick();Sleep(200);}return 0;
}
