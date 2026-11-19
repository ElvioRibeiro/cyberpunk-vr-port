#pragma once
#include "Core/HeadsetSelection.hpp"
#include <openxr/openxr.h>
#include <cctype>
#include <string>

namespace cvr::input {
// Accept only the Quest models offered by the launcher, ignoring spaces and case.
// This is runtime-reported identity, not proof against a runtime emulating a Quest.
inline bool IsSupportedQuestSystem(const char* name) {
    std::string normalized;
    if (!name) return false;
    for (const unsigned char ch : std::string(name)) {
        if (std::isalnum(ch)) normalized += static_cast<char>(std::tolower(ch));
    }
    for (const char* model : {"quest2", "quest3s", "quest3"}) {
        if (normalized == model || normalized == std::string("meta") + model ||
            normalized == std::string("oculus") + model) return true;
    }
    return false;
}

// Query after sync so a profile switch, failure or loss of focus revokes support
// before touch is read. Unknown identities and either missing hand fail closed.
inline bool QueryThumbRestSupport(XrResult sync, const char* systemName,
                                 XrSession session, const XrPath* hands, XrPath touchProfile) {
    if (!cvr::IsMetaQuestSelected() || sync != XR_SUCCESS ||
        !IsSupportedQuestSystem(systemName) || session == XR_NULL_HANDLE ||
        touchProfile == XR_NULL_PATH) return false;
    for (int hand = 0; hand < 2; ++hand) {
        XrInteractionProfileState state{XR_TYPE_INTERACTION_PROFILE_STATE};
        if (XR_FAILED(xrGetCurrentInteractionProfile(session, hands[hand], &state)) ||
            state.interactionProfile != touchProfile) return false;
    }
    return true;
}
}
