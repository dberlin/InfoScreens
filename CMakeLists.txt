cmake_minimum_required(VERSION 3.20)
project(InfoScreensHostChecks LANGUAGES CXX)

# Engine-independent state used by the real multiplayer editor and RCO.
# The game plugin itself is built by Tools/package_mod.py through UBT/Alpakit.
enable_testing()
add_executable(screen_edit_state_tests Tests/ScreenEditStateTests.cpp)
target_compile_features(screen_edit_state_tests PRIVATE cxx_std_20)
target_include_directories(screen_edit_state_tests PRIVATE Mods/InfoScreens/Source/InfoScreens/Public)
if(MSVC)
    target_compile_options(screen_edit_state_tests PRIVATE /W4 /WX /UNDEBUG)
else()
    target_compile_options(screen_edit_state_tests PRIVATE -Wall -Wextra -Werror -pedantic -UNDEBUG)
endif()
add_test(NAME multiplayer_edit_ordering COMMAND screen_edit_state_tests)
