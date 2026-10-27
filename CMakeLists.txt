cmake_minimum_required(VERSION 3.25)
project(swiftSearch VERSION 1.1 LANGUAGES CXX)

# made for c++23
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Never benchmark an unoptimised build by accident. Before this, CMAKE_BUILD_TYPE
# was empty, which meant no -O flag at all was ever passed.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

find_package(Threads REQUIRED)

# Warnings, shared by every target below
add_library(swiftSearch_warnings INTERFACE)
target_compile_options(swiftSearch_warnings INTERFACE
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic -Werror=return-type>
    $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wall -Wextra -Wpedantic -Werror=return-type>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
)

# The engine, minus main(). Split out so tests can link against it without
# dragging in an entry point. This is also the first seam of the eventual
# library/CLI split.
# The storage engine, minus main(). Split out so tests can link against it
# without dragging in an entry point. This is also the first seam of the
# eventual library/CLI split.
#
# The V1 search sources (parse.cpp/tokenize.cpp) are NOT here: they reference an
# API the V2 rewrite deleted and now live in legacy/, which is on no target and
# no include path. See legacy/README.md and docs/revival-checklist.md.
add_library(swiftSearch_core OBJECT
    src/crc32c.cpp
    src/sha256.cpp
    src/record.cpp
    src/box_meta.cpp
    src/http/url.cpp
    src/http/mime.cpp
    src/http/json_write.cpp
    src/http/parse.cpp
    src/http/message.cpp
    src/http/conditional.cpp
    src/http/routes.cpp
    src/http/conn.cpp
    src/http/server.cpp
)
target_include_directories(swiftSearch_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(swiftSearch_core PUBLIC Threads::Threads PRIVATE swiftSearch_warnings)

add_executable(${PROJECT_NAME} src/main.cpp
    src/find.hpp)
target_link_libraries(${PROJECT_NAME} PRIVATE swiftSearch_core swiftSearch_warnings)

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# tests/ is gitignored, so only wire it up when it is actually present.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
    add_subdirectory(tests)
endif()

message(STATUS "C++ Standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
