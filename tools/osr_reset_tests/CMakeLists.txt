cmake_minimum_required(VERSION 3.24)
project(OsrResetTests LANGUAGES CXX)

enable_testing()
find_path(OSR_OPENXR_INCLUDE_DIR openxr/openxr.h
    HINTS "${CMAKE_CURRENT_SOURCE_DIR}/../../build/_deps/openxr-src/include"
    REQUIRED)

# Exercise the real input adapter and shaders; no game, OpenXR runtime or PureDark DLL is loaded.
add_executable(osr_reset_tests reset.cpp ../../src/Render/Afw.cpp ../../src/Render/Osr.cpp)
target_include_directories(osr_reset_tests PRIVATE ../../include "${OSR_OPENXR_INCLUDE_DIR}")
target_compile_features(osr_reset_tests PRIVATE cxx_std_20)
target_compile_definitions(osr_reset_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(osr_reset_tests PRIVATE d3d12 dxgi d3dcompiler)
if(MSVC)
    target_compile_options(osr_reset_tests PRIVATE /W4 /permissive-)
endif()
add_test(NAME osr_reset_warp COMMAND osr_reset_tests)
set_tests_properties(osr_reset_warp PROPERTIES TIMEOUT 90)
