# RNNoise, built from source rather than taken from a package manager.
#
# WHY NOT VCPKG: vcpkg's `rnnoise` port declares `"supports": "!windows &
# !arm"` and refuses to install on x64-windows. That isn't a quality
# judgement on the library - the port is an autotools wrapper
# (vcpkg_make_configure/vcpkg_make_install) and autotools is what doesn't
# work here. The C sources themselves are plain, portable C with no
# platform assumptions, so compiling them directly with MSVC sidesteps
# the whole problem.
#
# THE MODEL IS A SEPARATE DOWNLOAD, AND THE VERSION MATTERS ENORMOUSLY.
# Since v0.2 the trained weights don't live in the git repo; upstream's
# own build fetches them from media.xiph.org and copies rnnoise_data.{c,h}
# into src/. The version below is the one v0.2 itself pins, taken from
# the `model_version` file in its own tarball (which download_model.sh
# reads).
#
# DO NOT copy vcpkg's port here - it downloads a DIFFERENT model
# (rnnoise_data-0a8755f8...), one generated for upstream's `main` branch
# after the network gained skip connections into its output layer. That
# model's init_rnnoise declares dense_out with 1536 inputs; v0.2's
# rnn.c feeds dense_out from gru3_state, a 384-float buffer. Nothing
# fails loudly: linear_init succeeds, rnnoise_create returns non-NULL,
# and the network happily reads 1152 floats of adjacent struct memory as
# if they were activations. Measured, with that model: RNNoise's VAD was
# a coin flip (55% "speech" on speech frames, 47% on silent ones), band
# gains sat at a near-flat 0.5, and it did -3.3dB to noise and -3.9dB to
# voice alike. With the correct model, the same code does what it should.
# See CLAUDE.md - this cost most of a session to find.
#
# rnnoise_data.c is ~29MB of generated float literals; the first build of
# it is slow, and that's expected, not a hang.
#
# NO RUNTIME CPU DISPATCH, AND NO AVX2 BY DEFAULT. Upstream can build
# SSE4.1/AVX2 variants of the neural-net kernels selected at runtime
# (RNN_ENABLE_X86_RTCD), which needs its own cpuid plumbing. Compiling
# the whole library for AVX2 instead is one flag, but it would make
# Inkwyrd crash outright on any pre-2013 CPU, and it isn't needed:
# measured on a 16s speech file, one 10ms frame costs 0.25ms scalar
# versus 0.15ms with AVX2 - 2.5% of one core against 1.5%. Both are far
# inside the budget for a single mic stream, so the portable build wins.
# INKWYRD_RNNOISE_AVX2=ON is kept for measuring that difference again,
# not for shipping.

include(FetchContent)

option(INKWYRD_RNNOISE_AVX2 "Compile RNNoise for AVX2 (raises the CPU floor - see above)" OFF)

# Matches the `model_version` file inside the v0.2 source tarball.
set(RNNOISE_MODEL_VERSION "0b50c45")
set(RNNOISE_MODEL_URL
    "https://media.xiph.org/rnnoise/models/rnnoise_data-${RNNOISE_MODEL_VERSION}.tar.gz")

FetchContent_Declare(rnnoise_src
    URL "https://github.com/xiph/rnnoise/archive/refs/tags/v0.2.tar.gz"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

FetchContent_Declare(rnnoise_model
    URL "${RNNOISE_MODEL_URL}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

FetchContent_MakeAvailable(rnnoise_src rnnoise_model)

# Fail loudly on the mismatch described above, rather than shipping a
# suppressor that quietly does nothing useful. The source tarball states
# which model it was built against; if a version bump here ever
# disagrees with it, that has to be a deliberate, checked decision.
file(READ "${rnnoise_src_SOURCE_DIR}/model_version" rnnoise_pinned_model)
string(STRIP "${rnnoise_pinned_model}" rnnoise_pinned_model)
if (NOT rnnoise_pinned_model STREQUAL RNNOISE_MODEL_VERSION)
    message(FATAL_ERROR
        "RNNoise model mismatch: the source tarball pins model "
        "'${rnnoise_pinned_model}' but this file downloads "
        "'${RNNOISE_MODEL_VERSION}'. These must match - a mismatched model "
        "loads without error and silently produces nonsense.")
endif()

# Upstream's portfile does exactly this copy; the library's own sources
# #include "rnnoise_data.h" as if it were always there.
foreach(generated rnnoise_data.c rnnoise_data.h)
    configure_file("${rnnoise_model_SOURCE_DIR}/src/${generated}"
                    "${rnnoise_src_SOURCE_DIR}/src/${generated}" COPYONLY)
endforeach()

add_library(rnnoise STATIC
    "${rnnoise_src_SOURCE_DIR}/src/denoise.c"
    "${rnnoise_src_SOURCE_DIR}/src/rnn.c"
    "${rnnoise_src_SOURCE_DIR}/src/pitch.c"
    "${rnnoise_src_SOURCE_DIR}/src/kiss_fft.c"
    "${rnnoise_src_SOURCE_DIR}/src/celt_lpc.c"
    "${rnnoise_src_SOURCE_DIR}/src/nnet.c"
    "${rnnoise_src_SOURCE_DIR}/src/nnet_default.c"
    "${rnnoise_src_SOURCE_DIR}/src/parse_lpcnet_weights.c"
    "${rnnoise_src_SOURCE_DIR}/src/rnnoise_data.c"
    "${rnnoise_src_SOURCE_DIR}/src/rnnoise_tables.c")

target_include_directories(rnnoise
    PUBLIC  "${rnnoise_src_SOURCE_DIR}/include"
    PRIVATE "${rnnoise_src_SOURCE_DIR}/src"
            "${CMAKE_CURRENT_SOURCE_DIR}/shim")

set_target_properties(rnnoise PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)

if (MSVC)
    # Without this the static lib picks up MSBuild's own default for a
    # static library (/MT) while everything else in this project uses the
    # DLL runtime, and every consumer links with LNK4098.
    set_target_properties(rnnoise PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

    # The generated model file trips these by the thousand; they say
    # nothing about code we wrote or can change.
    target_compile_options(rnnoise PRIVATE /wd4244 /wd4305 /wd4267 /wd4996)

    if (INKWYRD_RNNOISE_AVX2)
        target_compile_options(rnnoise PRIVATE /arch:AVX2)
    endif()
endif()
