#pragma once
#include <cstddef>

// The documented dsound.dll export list, in the ordinals the 32-bit system DLL uses
// (C:\Windows\SysWOW64\dsound.dll, verified with a PE export-table parse). The forwarders in
// dsound_proxy.cpp and the dsound.def ordinals must match this table; music_test checks both.
namespace rr95::music {

struct DsoundExport {
    const char* name;
    int ordinal;
};

const DsoundExport* DsoundExportTable(size_t& count);
int DsoundExportOrdinal(const char* name);

}  // namespace rr95::music
