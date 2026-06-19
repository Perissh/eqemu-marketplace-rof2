cmake_minimum_required(VERSION 3.15)
project(EQMarketplaceASI CXX)

# Must be 32-bit to match the RoF2 EQ client. Configure with: cmake -A Win32 -B build
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Must build x86 (32-bit) to match the RoF2 EQ client. Use: cmake -A Win32")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static CRT so the .asi has zero external runtime deps (no UCRT / api-ms-win-* dlls).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# The standalone Marketplace client mod. One translation unit, carved from the
# BoZ client DLL: the marketplace window (categories/tiles/buy/funds) + 3D preview,
# its offset header, and only the 3 hooks the marketplace needs.
add_library(EQMarketplaceASI SHARED
    src/marketplace_asi.cpp
)

set_target_properties(EQMarketplaceASI PROPERTIES
    OUTPUT_NAME "Marketplace"
    SUFFIX ".asi"
    PREFIX ""
)

target_compile_definitions(EQMarketplaceASI PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)
