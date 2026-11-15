#pragma once
#include "binding.h"

// Contract between the dsound proxy (binding decision and exports) and the game glue.
namespace rr95::music {

// True when [Music] Enabled=1 and the loaded exe is a supported build.
bool MusicProxyEnabled();

// Runs the binding decision now, if the schedule allows it for this trigger.
void MusicProxyBind(BindingTrigger trigger);

}  // namespace rr95::music
