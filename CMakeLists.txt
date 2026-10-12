cmake_minimum_required(VERSION 3.22.1)
project(scale_project VERSION 0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to an optimized build on single-config generators (Make, Ninja).
# Without this the image code is compiled at -O0 and is many times slower.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Link the MSVC runtime statically so the .exe runs without the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        2c980bb59875b0d32144a71867fbdebb2f77cd20
)
# Fast PNG encoder. SOURCE_SUBDIR points nowhere so only the sources are fetched
# (its own CMakeLists builds a test app we don't need).
FetchContent_Declare(
    fpng
    GIT_REPOSITORY https://github.com/richgel999/fpng.git
    GIT_TAG        925796543b9d26b8edfcdcecd94c1dac280f29fc
    SOURCE_SUBDIR  none
)
FetchContent_MakeAvailable(stb fpng)

add_executable(scale_app src/main.cpp src/stb_impl.cpp ${fpng_SOURCE_DIR}/src/fpng.cpp)
target_include_directories(scale_app PRIVATE ${stb_SOURCE_DIR} ${fpng_SOURCE_DIR}/src)

# fpng's SSE 4.1 / PCLMUL path (requires a post-2008 x86 CPU). MSVC needs no
# flags; GCC/Clang do. On ARM fpng uses its portable path automatically.
if(NOT MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i.86")
    set_source_files_properties(${fpng_SOURCE_DIR}/src/fpng.cpp
        PROPERTIES COMPILE_OPTIONS "-msse4.1;-mpclmul")
endif()

if(MSVC)
    # /utf-8: source and execution charset are UTF-8 (needed for std::print of paths)
    target_compile_options(scale_app PRIVATE /utf-8)
    target_compile_definitions(scale_app PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(MINGW)
    # entry point is wmain on Windows; also link the GCC runtime statically
    target_link_options(scale_app PRIVATE -municode -static)
endif()
