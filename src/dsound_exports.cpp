#include "dsound_exports.h"

#include <cstring>

namespace rr95::music {

namespace {

// Matches the exported names and ordinals of the 32-bit system dsound.dll
// (C:\Windows\SysWOW64\dsound.dll on 64-bit Windows; the PE export table reports base 1 and these
// twelve names). dsound.def repeats the ordinal for every name.
const DsoundExport kExports[] = {
    {"DirectSoundCreate", 1},
    {"DirectSoundEnumerateA", 2},
    {"DirectSoundEnumerateW", 3},
    {"DllCanUnloadNow", 4},
    {"DllGetClassObject", 5},
    {"DirectSoundCaptureCreate", 6},
    {"DirectSoundCaptureEnumerateA", 7},
    {"DirectSoundCaptureEnumerateW", 8},
    {"GetDeviceID", 9},
    {"DirectSoundFullDuplexCreate", 10},
    {"DirectSoundCreate8", 11},
    {"DirectSoundCaptureCreate8", 12},
};

}  // namespace

const DsoundExport* DsoundExportTable(size_t& count) {
    count = sizeof(kExports) / sizeof(kExports[0]);
    return kExports;
}

int DsoundExportOrdinal(const char* name) {
    if (name == nullptr) {
        return -1;
    }
    for (const DsoundExport& entry : kExports) {
        if (std::strcmp(entry.name, name) == 0) {
            return entry.ordinal;
        }
    }
    return -1;
}

}  // namespace rr95::music
