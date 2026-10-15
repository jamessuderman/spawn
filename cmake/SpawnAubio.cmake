# Spawn's aubio wrapper.
#
# Builds aubio (BSD-licensed onset/tempo detection) as a STATIC library
# from the vendored sources under third_party/aubio/src. We deliberately
# do NOT use aubio's own CMakeLists as a subdirectory because that one
# builds aubio as a SHARED library, drags in pkg-config, builds example
# binaries that depend on libsndfile / libav / Python, and tests that
# bring their own world of dependencies.
#
# Instead we hand-pick the C sources we need, declare include directories
# ourselves, and skip aubio's I/O / synth / pitch / effects subsystems —
# Spawn already has its own WAV/AIFF/MP3/FLAC/OGG/OPUS decoder
# (SampleDecoder + JUCE AudioFormatManager) and never asks aubio to
# touch files. The pitch effects and rubberband/effects subsystems are
# out of scope until M5.
#
# Sources selected by dependency walk from src/tempo/tempo.c:
#   fvec, cvec, lvec, vecutils, mathutils, musicutils,
#   spectral/{fft, ooura_fft8g, phasevoc, specdesc, statistics, tss,
#             filterbank, filterbank_mel, awhitening},
#   temporal/{biquad, filter, a_weighting, c_weighting, resampler},
#   onset/{onset, peakpicker}, tempo/{tempo, beattracking},
#   utils/{log, scale, parameter, strutils, hist}.
#
# Skipped (with rationale):
#   io/*            — Spawn doesn't read audio via aubio.
#   effects/*       — Rubber Band and pitch-shift; v2.
#   pitch/*         — Pitch detection; v2.
#   synth/*         — Sampler and wavetable; v2.
#   notes/*         — Needs pitch/, which we skip.
#   utils/windll.c  — Windows DLL decoration noise we don't need in a
#                      static link.

set(SPAWN_AUBIO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/aubio")

# We stage a synthetic include directory that contains config.h
# alongside aubio's src/ tree. aubio's sources do
# `#include "config.h"` from aubio_priv.h, which MSVC resolves
# against the *source file's directory* first for quoted includes
# — so we can't make config.h live anywhere else and still be
# found. Two options: pollute aubio's src/ directory (modifies
# the submodule — bad), or stage aubio sources under our build
# directory. We stage. The cost is a one-time copy of ~38 small
# .c files and ~50 .h headers into the build dir; nothing gets
# built in the aubio submodule.
set(_SPAWN_AUBIO_STAGE "${CMAKE_CURRENT_BINARY_DIR}/aubio_stage")
file(MAKE_DIRECTORY "${_SPAWN_AUBIO_STAGE}/src")

# Mirror aubio's src/ tree under the stage. We copy .c and .h
# files (recursively). `.gitignore` / `meson.build` / cmake configs
# are not copied — aubio's compilation only needs the C sources
# and the headers they include.
file(GLOB_RECURSE _SPAWN_AUBIO_FILES
    "${SPAWN_AUBIO_DIR}/src/*.c"
    "${SPAWN_AUBIO_DIR}/src/*.h"
)
foreach(_file ${_SPAWN_AUBIO_FILES})
    file(RELATIVE_PATH _rel "${SPAWN_AUBIO_DIR}/src" "${_file}")
    get_filename_component(_dir "${_rel}" DIRECTORY)
    file(MAKE_DIRECTORY "${_SPAWN_AUBIO_STAGE}/src/${_dir}")
    file(COPY "${_file}" DESTINATION "${_SPAWN_AUBIO_STAGE}/src/${_dir}")
endforeach()

# Drop config.h next to aubio_priv.h so `#include "config.h"`
# resolves in the staged aubio_priv.h's directory.
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/aubio_config.h.in"
    "${_SPAWN_AUBIO_STAGE}/src/config.h"
    @ONLY
)

set(SPAWN_AUBIO_SRC_DIR "${SPAWN_AUBIO_DIR}/src")

set(SPAWN_AUBIO_SOURCES
    # Core types
    "${_SPAWN_AUBIO_STAGE}/src/fvec.c"
    "${_SPAWN_AUBIO_STAGE}/src/cvec.c"
    "${_SPAWN_AUBIO_STAGE}/src/fmat.c"
    "${_SPAWN_AUBIO_STAGE}/src/lvec.c"
    "${_SPAWN_AUBIO_STAGE}/src/vecutils.c"
    "${_SPAWN_AUBIO_STAGE}/src/mathutils.c"
    "${_SPAWN_AUBIO_STAGE}/src/musicutils.c"
    # Spectral
    "${_SPAWN_AUBIO_STAGE}/src/spectral/fft.c"
    "${_SPAWN_AUBIO_STAGE}/src/spectral/ooura_fft8g.c"
    "${_SPAWN_AUBIO_STAGE}/src/spectral/phasevoc.c"
    "${_SPAWN_AUBIO_STAGE}/src/spectral/specdesc.c"
    "${_SPAWN_AUBIO_STAGE}/src/spectral/statistics.c"
    "${_SPAWN_AUBIO_STAGE}/src/spectral/tss.c"
    "${_SPAWN_AUBIO_STAGE}/src/spectral/filterbank.c"
    "${_SPAWN_AUBIO_STAGE}/src/spectral/filterbank_mel.c"
    "${_SPAWN_AUBIO_STAGE}/src/spectral/awhitening.c"
    # Temporal filters (biquad used by aubio's spectral descriptor for
    # adaptive whitening)
    "${_SPAWN_AUBIO_STAGE}/src/temporal/biquad.c"
    "${_SPAWN_AUBIO_STAGE}/src/temporal/filter.c"
    "${_SPAWN_AUBIO_STAGE}/src/temporal/a_weighting.c"
    "${_SPAWN_AUBIO_STAGE}/src/temporal/c_weighting.c"
    "${_SPAWN_AUBIO_STAGE}/src/temporal/resampler.c"
    # Onset
    "${_SPAWN_AUBIO_STAGE}/src/onset/onset.c"
    "${_SPAWN_AUBIO_STAGE}/src/onset/peakpicker.c"
    # Tempo
    "${_SPAWN_AUBIO_STAGE}/src/tempo/tempo.c"
    "${_SPAWN_AUBIO_STAGE}/src/tempo/beattracking.c"
    # Utils (skip windll)
    "${_SPAWN_AUBIO_STAGE}/src/utils/log.c"
    "${_SPAWN_AUBIO_STAGE}/src/utils/scale.c"
    "${_SPAWN_AUBIO_STAGE}/src/utils/parameter.c"
    "${_SPAWN_AUBIO_STAGE}/src/utils/strutils.c"
    "${_SPAWN_AUBIO_STAGE}/src/utils/hist.c"
)

add_library(spawn_aubio STATIC ${SPAWN_AUBIO_SOURCES})

target_include_directories(spawn_aubio
    PUBLIC
        "${_SPAWN_AUBIO_STAGE}/src"
)

target_compile_definitions(spawn_aubio
    PRIVATE
        # Marks config.h as present so aubio_priv.h takes its branch.
        HAVE_CONFIG_H
        # We feed aubio single-precision (smpl_t = float) buffers.
        HAVE_AUBIO_DOUBLE=0
        # Disable optional FFT / BLAS backends so fft.c lands on the
        # OOURA portable path and aubio_priv.h skips FFTW/Accelerate/
        # IPP/ATLAS/BLAS includes.
        # (no HAVE_FFTW3 / HAVE_FFTW3F / HAVE_ACCELERATE / HAVE_INTEL_IPP /
        #  HAVE_ATLAS / HAVE_BLAS macros.)
        # aubio's AUBIO_ERR/etc macros take two forms: C99 `__VA_ARGS__`
        # (the gate HAVE_C99_VARARGS_MACROS) and GCC-style
        # `args.../##args` (the else branch). MSVC's preprocessor only
        # supports the former; define it so we never fall into the
        # GCC-only path.
        HAVE_C99_VARARGS_MACROS=1
)

if(MSVC)
    # Aubio's sources are C99; force C compilation and let MSVC's
    # lax-conformance mode accept C99-isms. /permissive- would reject
    # some of aubio's K&R-style code patterns.
    target_compile_options(spawn_aubio PRIVATE /TC /permissive-)
    # Aubio's C99 `for (int i = ...)` loops and trailing-comma in
    # enums trip MSVC's older language extensions; permissive- above
    # covers most, but a few C99-only constructs still need /std:c11.
    # /std:c11 is implicit under VS 17.10 (which Spawn already builds
    # against); /std:c99 does not exist as an MSVC switch.
    #
    # NULL and the standard library plumbing that MSVC's C compiler
    # doesn't auto-provide (vs. C++) are defined in cmake/aubio_config.h.in
    # — config.h is the very first header every aubio source sees,
    # so the defines land before any aubio_priv.h gate runs.
else()
    target_compile_options(spawn_aubio
        PRIVATE -Wno-unused-parameter -Wno-unused-function
    )
endif()

# aubio's `cvec.c` and a couple of spectral sources need C linkage;
# the rest of Spawn is C++. Wrapping the include of aubio headers in
# extern "C" is the consumer's job (we already do that in
# BpmDetector.cpp via a small typed wrapper). Nothing to set here.
