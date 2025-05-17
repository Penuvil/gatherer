cmake_minimum_required(VERSION 3.10)

# Project name and version
project(gatherer VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(LIBS)

# Add dependencies
include(cmake/CPM.cmake)

CPMAddPackage("gh:marzer/tomlplusplus#v3.4.0")
CPMAddPackage("gh:skypjack/entt#v3.15.0")

# SDL3
CPMAddPackage("gh:libsdl-org/SDL#release-3.2.10")
list(APPEND LIBS SDL3::SDL3)

# SDL_Image
set(SDLIMAGE_AVIF OFF CACHE BOOL "" FORCE)
CPMAddPackage("gh:libsdl-org/SDL_Image#release-3.2.4")
list(APPEND LIBS SDL3_image::SDL3_image)

# Add the executable
file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(${PROJECT_NAME} "src/main.cpp")

target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBS})

# Include directories (if any)
include_directories(${PROJECT_SOURCE_DIR}/src
                    ${tomlplusplus_SOURCE_DIR}
                    ${entt_SOURCE_DIR}/src)

# Set compiler-specific options (optional)
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -pedantic)
endif()

# Output directories
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)
