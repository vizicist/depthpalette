cmake_minimum_required(VERSION 3.21)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- OrbbecSDK v2 ----
# Set output directories so OrbbecSDK extensions are copied to the build dir
if(NOT CMAKE_LIBRARY_OUTPUT_DIRECTORY)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
endif()
if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

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
if(WIN32)
    set(VIEWER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../common/src/viewer.cpp)
else()
    set(VIEWER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../common/src/viewer_linux.cpp)
endif()

add_executable(depthpalette
    src/main.cpp
    ${VIEWER_SRC}
    src/webserver.cpp
)

if(WIN32)
    # Prevent windows.h min/max macros from conflicting
    target_compile_definitions(depthpalette PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if(NOT WIN32)
    # Use Linux viewer header instead of Windows one
    target_compile_definitions(depthpalette PRIVATE VIEWER_LINUX)
endif()

target_include_directories(depthpalette PRIVATE
    src
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/src
)
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
