cmake_minimum_required(VERSION 3.22)

project(InkwyrdAudio VERSION 0.1.0 LANGUAGES CXX)

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
    GIT_TAG        8.0.6
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

add_subdirectory(third_party/rnnoise)

add_subdirectory(src/discord-voice)
add_subdirectory(src/discord-spike)
add_subdirectory(src/audio-engine)
add_subdirectory(src/audio-engine-test)
add_subdirectory(src/vst-hosting)
add_subdirectory(src/vst-hosting-test)
add_subdirectory(src/mic-spike)
add_subdirectory(src/app)
