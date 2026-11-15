/* Single translation unit for stb_vorbis (task M3). Compiled as C with /W0; the project's own code
 * keeps /W4 /WX. The source is vendored unmodified in third_party/.
 * The push-data API is unused by the player and is compiled out. */
#define STB_VORBIS_NO_PUSHDATA_API
#include "../third_party/stb/stb_vorbis.c"
