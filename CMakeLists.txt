cmake_minimum_required(VERSION 3.21)

# Static CRT (/MT) for every target, so the proxy has no VC runtime dependency.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(RoadRashMusicPlayer LANGUAGES C CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "Road Rash Music Player must be built for 32-bit x86 (use -A Win32 or a vcvars32 environment).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(RRMP_COMMON_OPTIONS /W4 /WX /permissive- /utf-8)
set(RRMP_COMMON_DEFINITIONS WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)

# Vendored decoder implementations: warnings off for these translation units only, so the project's
# own files keep /W4 /WX. Never edit the vendored sources themselves.
set_source_files_properties(
  src/audio_decoders_impl.cpp
  src/stb_vorbis_impl.c
  PROPERTIES COMPILE_OPTIONS "/W0"
)

# Player sources shared by the DLL and the unit test.
set(RRMP_PLAYER_SOURCES
  src/binding.cpp
  src/engine.cpp
  src/hotkey.cpp
  src/music_config.cpp
  src/music_game.cpp
  src/music_library.cpp
  src/music_menu.cpp
  src/music_overlay.cpp
  src/music_player.cpp
  src/music_registry.cpp
  src/music_tags.cpp
  src/audio_decoders.cpp
  src/audio_decoders_impl.cpp
  src/stb_vorbis_impl.c
  src/dsound_exports.cpp
  src/shared/log.cpp
)

# Proxy dsound.dll -------------------------------------------------------------
add_library(dsound SHARED
  ${RRMP_PLAYER_SOURCES}
  src/dllmain.cpp
  src/dsound_proxy.cpp
  src/music_glue.cpp
  src/shared/exe_identity.cpp
  src/shared/hook.cpp
  src/dsound.def
)
set_target_properties(dsound PROPERTIES OUTPUT_NAME "dsound" PREFIX "")
target_compile_options(dsound PRIVATE ${RRMP_COMMON_OPTIONS})
target_compile_definitions(dsound PRIVATE ${RRMP_COMMON_DEFINITIONS})
target_link_libraries(dsound PRIVATE bcrypt ole32)
target_link_options(dsound PRIVATE /IGNORE:4104 /IGNORE:4222)

# Unit test: binding decision, [Music] ini, player, menu, overlay, exports -----
add_executable(music_test
  tests/music_test.cpp
  ${RRMP_PLAYER_SOURCES}
)
target_compile_options(music_test PRIVATE ${RRMP_COMMON_OPTIONS})
target_compile_definitions(music_test PRIVATE ${RRMP_COMMON_DEFINITIONS}
  RR95_MUSIC_PROXY_PATH=L"$<TARGET_FILE:dsound>")
target_link_libraries(music_test PRIVATE ole32)
add_dependencies(music_test dsound)
target_link_options(music_test PRIVATE /INCREMENTAL:NO)

enable_testing()
add_test(NAME music_test COMMAND music_test)
