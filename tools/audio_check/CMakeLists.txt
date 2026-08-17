# Listening harness for the hosted sound driver. Off by default and never built in CI: it opens a
# real audio device and is meant to be run by a person who can hear the result.
#
#     cmake -S . -B build -G Ninja -DKIRPICH_BUILD_AUDIO_CHECK=ON
#     cmake --build build --target audio-check
#     ./build/audio-check 5 20
add_executable(audio-check main.cpp)
target_link_libraries(audio-check PRIVATE kirpich-lib)
target_compile_features(audio-check PRIVATE cxx_std_20)
