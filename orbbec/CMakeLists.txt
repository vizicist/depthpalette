cmake_minimum_required(VERSION 3.21)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- OrbbecSDK v2 ----
include(FetchContent)
FetchContent_Declare(
    OrbbecSDK
    GIT_REPOSITORY https://github.com/orbbec/OrbbecSDK_v2.git
    GIT_TAG        v2.7.6
    GIT_SHALLOW    TRUE
)
set(OB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(OB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(OrbbecSDK)

project(depthpalette LANGUAGES CXX C)

# ---- cpp-httplib (header-only HTTP server) ----
FetchContent_Declare(
    httplib
    URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.30.0.zip
)
FetchContent_MakeAvailable(httplib)

# ---- Application ----
add_executable(depthpalette
    src/main.cpp
    src/viewer.cpp
    src/webserver.cpp
)

# Prevent windows.h min/max macros from conflicting
target_compile_definitions(depthpalette PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

target_include_directories(depthpalette PRIVATE src)
target_link_libraries(depthpalette
    PRIVATE
        ob::OrbbecSDK
        httplib::httplib
)

# Copy any required DLLs next to the executable
if(WIN32)
    add_custom_command(TARGET depthpalette POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:depthpalette>
                $<TARGET_FILE_DIR:depthpalette>
        COMMAND_EXPAND_LISTS
    )
endif()
