cmake_minimum_required(VERSION 3.22)

# C as well as CXX: JUCE 8.0.15 refuses to configure without a C compiler
# enabled ("A C compiler is required to build targets that depend on
# JUCE"), where 8.0.6 did not. Nothing here is written in C - rnnoise is,
# and it already worked, because it enables its own.
# VERSION is the numeric part of the release, and JUCE stamps it into the
# exe's version resource. It has to be a literal (project() runs before
# the .iss can be read), so a check further down fails the configure if
# it ever drifts from the real version.
project(InkwyrdAudio VERSION 0.1.5 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# See vcpkg-overlay-triplets/x64-windows.cmake for why: this machine's
# MSVC toolset emits calls to a vectorized STL helper it doesn't itself
# export. Applied here too so our own code can't hit the same missing
# symbol if it ever calls the same std::string overload directly.
add_compile_definitions(_USE_STD_VECTOR_ALGORITHMS=0)

# --- JUCE (fetched fresh, not a shared local checkout) ---
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# --- vcpkg-provided libs used across targets ---
find_package(Opus CONFIG REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(ixwebsocket CONFIG REQUIRED)

# --- discord/libdave (DAVE end-to-end voice encryption, MIT-licensed) ---
# Discord requires DAVE support for every voice connection as of March
# 2026 - this isn't optional. Fetched as Discord's own prebuilt Windows
# release (self-contained DLL statically linking BoringSSL/mlspp/
# nlohmann-json - see licenses/ inside the archive) rather than built
# from source, since upstream's build is Make-based with no Windows/MSVC
# path; a prebuilt release avoids fighting a second, unrelated toolchain.
FetchContent_Declare(
    dave
    URL                    https://github.com/discord/libdave/releases/download/v1.2.0/cpp/libdave-Windows-X64-boringssl.zip
    URL_HASH               SHA256=F4CADE98328FCE51EA6E17FCC3C7BD39447999A554DBF383F7F63B89F0BBD714
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(dave)

add_library(dave SHARED IMPORTED GLOBAL)
set_target_properties(dave PROPERTIES
    IMPORTED_LOCATION             "${dave_SOURCE_DIR}/bin/libdave.dll"
    IMPORTED_IMPLIB               "${dave_SOURCE_DIR}/lib/libdave.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${dave_SOURCE_DIR}/include"
)

# dave is a manually-declared IMPORTED target (not vcpkg-installed), so
# vcpkg's automatic post-build DLL copy (which handles opus/sodium/zlib)
# doesn't know about it. Any executable that links DiscordVoice
# (transitively pulling in dave) needs this called on it once.
function(inkwyrd_copy_dave_dll target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:dave>"
            "$<TARGET_FILE_DIR:${target}>"
    )
endfunction()

# --- The Visual C++ runtime, shipped beside the exe ---
#
# The app imports MSVCP140.dll, VCRUNTIME140.dll and VCRUNTIME140_1.dll
# (so does vcpkg's tag.dll), because the vcpkg triplet links the CRT
# dynamically - see vcpkg-overlay-triplets/x64-windows.cmake. Those are
# NOT part of Windows: they arrive with the "Microsoft Visual C++
# 2015-2022 Redistributable", which a development machine always has and
# a clean install may not. Without them the app doesn't start, and what
# the user sees is a bare "MSVCP140.dll was not found" from the loader
# with no mention of Inkwyrd at all.
#
# Copying them next to the exe (Microsoft's own "local deployment"
# option for these files) means the downloads work as they are, with no
# prerequisite to install. It also covers the portable ZIP, which has no
# installer that could have chained the redistributable.
#
# InstallRequiredSystemLibraries rather than a hardcoded path: it
# resolves the redistributable matching whichever toolset is building,
# so this doesn't quietly ship a stale runtime after a Visual Studio
# update.
#
# UCRT is excluded deliberately - api-ms-win-crt-*.dll are API sets
# resolved by the OS's own ucrtbase.dll, present on every Windows 10 and
# 11, and shipping a private copy of those is the documented way to get
# subtle mismatches.
if(MSVC)
    set(CMAKE_INSTALL_UCRT_LIBRARIES FALSE)
    set(CMAKE_INSTALL_OPENMP_LIBRARIES FALSE)
    # We copy them ourselves, post-build; no install() rule is wanted.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    include(InstallRequiredSystemLibraries)

    if(NOT CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        message(FATAL_ERROR
            "Couldn't find the MSVC redistributable DLLs to ship. Building "
            "without them produces downloads that fail to start on any machine "
            "that hasn't installed the VC++ redistributable - refusing to "
            "configure rather than ship that silently.")
    endif()

    message(STATUS "Shipping MSVC runtime: ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}")
endif()

# Copies the runtime above next to a target's exe. Must be called for
# anything that actually ships (the app); spikes and tests run from a
# machine that has the redistributable anyway.
function(inkwyrd_copy_msvc_runtime target)
    if(NOT MSVC)
        return()
    endif()

    foreach(runtime_dll ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS})
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${runtime_dll}"
                "$<TARGET_FILE_DIR:${target}>"
            VERBATIM
        )
    endforeach()
endfunction()

# The beta-qualified version string, read from the installer script so
# there is still ONE place to bump per release (see CLAUDE.md's release
# process). JUCE's own version can't carry it - Windows' version fields
# hold four numbers, not "beta.20.1" - so it comes through as a define.
#
# At the ROOT rather than in src/app, because the test harness compiles
# against it too: INKWYRD_UPDATECHECK compares this build against what
# GitHub reports, and a harness with its own idea of the version would
# be checking the wrong thing.
file(READ "${CMAKE_SOURCE_DIR}/installer/InkwyrdAudio.iss" inkwyrd_iss_contents)
string(REGEX MATCH "#define MyAppVersion \"([^\"]+)\"" inkwyrd_version_match "${inkwyrd_iss_contents}")
set(INKWYRD_VERSION_STRING "${CMAKE_MATCH_1}")

if(NOT INKWYRD_VERSION_STRING)
    message(WARNING "Couldn't read MyAppVersion from installer/InkwyrdAudio.iss - falling back")
    set(INKWYRD_VERSION_STRING "0.1.0")
endif()

# Otherwise editing the .iss alone leaves a stale version compiled in.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/installer/InkwyrdAudio.iss")

# The exe's version resource comes from project() above; the release
# version comes from the .iss. They are two literals that must agree, so
# drifting them is a configure error rather than a release that reports
# the wrong version in Windows' "Installed apps" list.
string(REGEX REPLACE "-.*$" "" INKWYRD_BASE_VERSION "${INKWYRD_VERSION_STRING}")
if(NOT INKWYRD_BASE_VERSION STREQUAL PROJECT_VERSION)
    message(FATAL_ERROR
        "Version mismatch: installer/InkwyrdAudio.iss says ${INKWYRD_VERSION_STRING} "
        "(numeric part ${INKWYRD_BASE_VERSION}) but project() in CMakeLists.txt says "
        "${PROJECT_VERSION}. Bump project(InkwyrdAudio VERSION ...) to match.")
endif()

add_subdirectory(third_party/rnnoise)

add_subdirectory(src/discord-voice)
add_subdirectory(src/discord-spike)
add_subdirectory(src/audio-engine)
add_subdirectory(src/audio-engine-test)
add_subdirectory(src/vst-hosting)
add_subdirectory(src/vst-hosting-test)
add_subdirectory(src/mic-spike)
add_subdirectory(src/app)
