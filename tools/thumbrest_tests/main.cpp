#include <openxr/openxr.h>
#include "Core/LiveControls.hpp"
#include "Core/HeadsetSelection.hpp"
#include "Runtimes/ThumbRestSupport.hpp"
#include "Hooks/ScannerGesture.hpp"
#include "Overlay/VrInteraction.hpp"
#include <array>
#include <cstdio>
#include <stdexcept>

LiveControls g_liveControls{};
int selectedHeadset{};
// Supply the launcher selection API used by the production Quest gate.
extern "C" int GetCurrentHmdType() { return selectedHeadset; }
namespace {
struct Button { bool down{}, active=true, failed{}; };
std::array<std::array<Button,2>,2> buttons;
unsigned restQueries{};
constexpr XrPath touchProfile=10;
std::array<XrPath,2> profiles{touchProfile,touchProfile};
bool profileQueryFails{};
void Check(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
}
// Expose actual per-hand profiles independently of the launcher and touch state.
extern "C" XrResult XRAPI_CALL xrGetCurrentInteractionProfile(XrSession,XrPath hand,XrInteractionProfileState* out) {
    out->interactionProfile=profiles.at(static_cast<size_t>(hand)-1);
    return profileQueryFails ? XR_ERROR_RUNTIME_FAILURE : XR_SUCCESS;
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
    XrResult syncRes=XR_SUCCESS;
    XrSession m_session=reinterpret_cast<XrSession>(1);
    const char* systemName="Meta Quest 3";
    XrPath m_handPaths[2]{1,2};
    XrAction m_thumbstickClickAction=reinterpret_cast<XrAction>(1);
    XrAction m_thumbRestTouchAction=reinterpret_cast<XrAction>(2);
    bool leftClickValid{};
    // Exercise the production post-sync policy shared by activation and UI visibility.
    bool IsThumbRestSupported() const {
        return cvr::input::QueryThumbRestSupport(syncRes,systemName,m_session,m_handPaths,touchProfile);
    }
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
    // A saved enabled preference must not affect any non-Quest launcher preset.
    for (int headset=-1; headset<=19; ++headset) {
        selectedHeadset=headset;
        const bool quest=headset==0 || headset==1 || headset==2;
        const unsigned queriesBefore=restQueries;
        Check(input.Sample(0)==quest,"Launcher headset isolation failed");
        Check(quest || restQueries==queriesBefore,"Non-Quest headset queried thumb rest");
        buttons[0][0].down=true;
        Check(input.Sample(0),"Headset gate suppressed physical click");
        buttons[0][0].down=false;
    }
    selectedHeadset=2;
    // A Quest selection alone must not expose or activate the option on Steam Frame.
    for (const char* name : {"Steam Frame", "PICO 4", "Valve Index", "PSVR2", "Oculus Rift S", "", "Meta Quest 4"}) {
        input.systemName=name;
        Check(!input.IsThumbRestSupported() && !input.Sample(0),"Non-Quest OpenXR identity accepted");
    }
    for (const char* name : {"Oculus Quest2", "Meta Quest 3S", "META QUEST 3", "Quest 2"}) {
        input.systemName=name;
        Check(input.IsThumbRestSupported() && input.Sample(0),"Supported OpenXR Quest name rejected");
    }
    input.systemName=nullptr;
    Check(!input.IsThumbRestSupported(),"Missing OpenXR identity accepted");
    input.systemName="Meta Quest 3";
    // Even an emulated Quest name cannot pass when either active profile is incompatible.
    for (int hand=0; hand<2; ++hand) {
        for (XrPath profile : {XrPath(XR_NULL_PATH), XrPath(20)}) {
            profiles[hand]=profile;
            Check(!input.IsThumbRestSupported() && !input.Sample(0),"Mismatched or unbound profile accepted");
            buttons[0][0].down=true;
            Check(input.Sample(0),"OpenXR eligibility gate suppressed physical click");
            buttons[0][0].down=false;
        }
        profiles[hand]=touchProfile;
    }
    profileQueryFails=true;
    Check(!input.IsThumbRestSupported() && !input.Sample(0),"Failed profile query accepted");
    profileQueryFails=false;
    Check(input.Sample(0),"Matching profiles did not restore support");
    // Simulate a failed sync while the runtime still exposes its previous active touch.
    input.syncRes=XR_ERROR_RUNTIME_FAILURE;
    const unsigned queriesBeforeFailure=restQueries;
    Check(!input.Sample(0) && restQueries==queriesBeforeFailure,"Failed sync accepted cached touch");
    input.syncRes=XR_SESSION_NOT_FOCUSED;
    Check(!input.Sample(0),"Unfocused sync accepted touch");
    input.syncRes=XR_SUCCESS;
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
