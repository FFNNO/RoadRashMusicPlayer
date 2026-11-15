// Single translation unit for the dr_libs decoder implementations (task M3). Compiled with /W0;
// the project's own code keeps /W4 /WX. The headers are vendored unmodified in third_party/.
#define DR_WAV_IMPLEMENTATION
#define DR_MP3_IMPLEMENTATION
#define DR_FLAC_IMPLEMENTATION

#include "../third_party/dr_libs/dr_wav.h"
#include "../third_party/dr_libs/dr_mp3.h"
#include "../third_party/dr_libs/dr_flac.h"
