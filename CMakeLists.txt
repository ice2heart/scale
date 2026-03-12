cmake_minimum_required(VERSION 3.22.1)
project(scale_project VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)


include(FetchContent)

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(stb)

add_executable(scale_app src/main.cpp)
target_include_directories(scale_app PRIVATE ${stb_SOURCE_DIR})
