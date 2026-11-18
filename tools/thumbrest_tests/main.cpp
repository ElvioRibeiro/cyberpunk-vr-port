#include <openxr/openxr.h>
#include "Core/LiveControls.hpp"
#include "Hooks/ScannerGesture.hpp"
#include "Overlay/VrInteraction.hpp"
#include <array>
#include <cstdio>
#include <stdexcept>

LiveControls g_liveControls{};
namespace {
struct Button { bool down{}, active=true, failed{}; };
std::array<std::array<Button,2>,2> buttons;
unsigned restQueries{};
void Check(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
}
extern "C" XrResult XRAPI_CALL xrGetActionStateBoolean(XrSession,const XrActionStateGetInfo* info,XrActionStateBoolean* out) {
    const auto action=reinterpret_cast<uintptr_t>(info->action)-1;
    const auto hand=static_cast<size_t>(info->subactionPath)-1;
    if(action==1)++restQueries;
    const auto& button=buttons.at(hand).at(action);
    out->isActive=button.active;out->currentState=button.down;
    return button.failed?XR_ERROR_RUNTIME_FAILURE:XR_SUCCESS;
}

struct InputFixture {
    XrSession m_session{};
    XrPath m_handPaths[2]{1,2};
    XrAction m_thumbstickClickAction=reinterpret_cast<XrAction>(1);
    XrAction m_thumbRestTouchAction=reinterpret_cast<XrAction>(2);
    bool leftClickValid{};
    // Run exactly the production acquisition/merge for one physical hand.
    bool Sample(int i) {
        leftClickValid=false;
#include "thumbrest_read.inc"
#include "thumbrest_merge.inc"
        return sclick;
    }
};

int main() try {
    InputFixture input;
    Check(g_liveControls.xrThumbRestStickClick==0,"Fallback must default off");
    buttons[0][1].down=true;
    Check(!input.Sample(0) && restQueries==0,"Disabled fallback read or activated touch");
    buttons[0][0].down=true;
    Check(input.Sample(0),"Physical click lost with fallback off");
    g_liveControls.xrThumbRestStickClick=1;
    buttons[0][0].down=false;
    Check(input.Sample(0) && input.leftClickValid,"Active rest did not provide a valid L3");
    Check(!input.Sample(1),"Left thumb rest leaked into right hand");
    buttons[0][1].active=false;
    Check(!input.Sample(0),"Inactive action's stale touch was accepted");
    buttons[0][1].active=true;buttons[0][1].failed=true;
    Check(!input.Sample(0),"Failed action's stale touch was accepted");
    buttons[0][0].down=true;
    Check(input.Sample(0),"Failed fallback suppressed physical click");
    buttons[0][0].active=false;buttons[0][1].failed=false;
    Check(input.Sample(0) && input.leftClickValid,"Fallback validity depended on physical action activity");
    input.m_thumbRestTouchAction=XR_NULL_HANDLE;
    Check(!input.Sample(0),"Missing rest handle created a click");
    input.m_thumbRestTouchAction=reinterpret_cast<XrAction>(2);
    buttons={};
    // Overlapping touch/physical holds must yield a single deferred L3 release, not two clicks.
    cvr::input::DeferredLeftStickClick click;
    buttons[0][1].down=true;Check(!click.Update(input.Sample(0),false),"L3 emitted before release");
    buttons[0][0].down=true;Check(!click.Update(input.Sample(0),false),"Overlap emitted another L3");
    buttons[0][1].down=false;Check(!click.Update(input.Sample(0),false),"Touch release released a physical hold");
    buttons[0][0].down=false;Check(click.Update(input.Sample(0),false),"Combined release did not emit L3");
    Check(!click.Update(input.Sample(0),false),"Combined release repeated L3");
    buttons[0][1].down=true;click.Update(input.Sample(0),true);
    buttons[0][1].down=false;Check(!click.Update(input.Sample(0),false),"Claimed chord leaked deferred L3");
    // Both rests intentionally retain the existing half-second menu-chord behavior.
    buttons[0][1].down=buttons[1][1].down=true;
    cvr::vrui::HoldChord chord;
    for(int step=0;step<4;++step)Check(!chord.Update(input.Sample(0)&&input.Sample(1),.1f),"Menu chord fired too early");
    Check(chord.Update(input.Sample(0)&&input.Sample(1),.11f),"Both rests did not invoke the menu chord");
    Check(!chord.Update(true,.1f),"Held menu chord repeated");
    g_liveControls.xrThumbRestStickClick=0;
    Check(!input.Sample(0) && !input.Sample(1),"Disabling fallback retained synthetic holds");
    std::puts("PASS production action reads, inactive/error handling, hand isolation, overlapping holds and existing chords");
} catch(const std::exception& error) { std::fprintf(stderr,"%s\n",error.what());return 1; }
