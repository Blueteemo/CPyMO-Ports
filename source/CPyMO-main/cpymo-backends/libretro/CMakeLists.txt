cmake_minimum_required(VERSION 3.9)
project(cpymo-libretro C)

option(LIBRETRO_STATIC "Statically link the libretro core" OFF)
option(GARLIC_BOOTSTRAP_NO_AUDIO "GarlicOS bootstrap without FFmpeg/audio/video" ON)

set(SOURCES
  libretro.c
  ../garlicos/garlic_adapter.c
  ../garlicos/garlic_paths.c
)
aux_source_directory(../../cpymo SOURCES)
aux_source_directory(../software SOURCES)

if (LIBRETRO_STATIC)
  add_library(cpymo_libretro STATIC ${SOURCES})
else ()
  add_library(cpymo_libretro SHARED ${SOURCES})
endif ()

target_compile_definitions(cpymo_libretro PRIVATE
  -DTEXT_CHARACTER_W_SCALE=1
)
if (GARLIC_BOOTSTRAP_NO_AUDIO)
  target_compile_definitions(cpymo_libretro PRIVATE
    DISABLE_AUDIO DISABLE_MOVIE GARLIC_NO_FFMPEG)
endif()
target_link_options(cpymo_libretro PRIVATE
  -Wl,--version-script=${CMAKE_SOURCE_DIR}/link.T
  -Wl,--no-undefined
  $<$<NOT:$<BOOL:${GARLIC_BOOTSTRAP_NO_AUDIO}>>:-lavcodec>
  $<$<NOT:$<BOOL:${GARLIC_BOOTSTRAP_NO_AUDIO}>>:-lavformat>
  $<$<NOT:$<BOOL:${GARLIC_BOOTSTRAP_NO_AUDIO}>>:-lavutil>
  $<$<NOT:$<BOOL:${GARLIC_BOOTSTRAP_NO_AUDIO}>>:-lswresample>
  $<$<NOT:$<BOOL:${GARLIC_BOOTSTRAP_NO_AUDIO}>>:-lswscale>
)

# Follow naming conventions for libretro cores
set_target_properties(cpymo_libretro PROPERTIES PREFIX "")

# Link libm AFTER objects. Link-options -lm at the start breaks --as-needed.
target_link_libraries(cpymo_libretro PRIVATE m)
