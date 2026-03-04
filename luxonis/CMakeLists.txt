cmake_minimum_required(VERSION 3.21)

# Allow FetchContent_Populate (needed to add_subdirectory before project() for Hunter)
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# ---- depthai-core ----
# Download via FetchContent, but add_subdirectory BEFORE project() so that
# Hunter's HunterGate runs before any project() command.
include(FetchContent)
set(DEPTHAI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(DEPTHAI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    depthai-core
    GIT_REPOSITORY https://github.com/luxonis/depthai-core.git
    GIT_TAG        v2.32.0
    GIT_SHALLOW    TRUE
)
FetchContent_Populate(depthai-core)
add_subdirectory(${depthai-core_SOURCE_DIR} ${depthai-core_BINARY_DIR} EXCLUDE_FROM_ALL)

# ---- Our project (declared after depthai-core so Hunter is already set up) ----
project(depthpalette LANGUAGES CXX C)

# ---- cpp-httplib (header-only HTTP server, must be after project()) ----
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
        depthai::core
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
