# Feasibility spike, not part of the app. Measures what RNNoise actually
# does to a mic-like signal before any of it is wired into MasterEngine.
# Links the `rnnoise` target built from source in third_party/rnnoise -
# see that file for why this isn't a vcpkg dependency.
add_executable(MicSpike Main.cpp)
target_link_libraries(MicSpike PRIVATE rnnoise)
