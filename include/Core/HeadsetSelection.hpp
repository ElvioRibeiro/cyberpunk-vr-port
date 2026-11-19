#pragma once

extern "C" int GetCurrentHmdType();

namespace cvr {
// Match the launcher's persisted Quest 2, Quest 3S and Quest 3 preset IDs.
// Selection is only one check; ThumbRestSupport also validates the OpenXR reports.
inline bool IsMetaQuestSelected() {
    const int headset = GetCurrentHmdType();
    return headset == 0 || headset == 1 || headset == 2;
}
}
