cmake_minimum_required(VERSION 3.25)
project(swiftSearch VERSION 1.0 LANGUAGES CXX)

# made for c++23
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Source files
set(SOURCES
    main.cpp
    classes.cpp
    parse.cpp
    tokenize.cpp
)

# Header files
set(HEADERS
    constants.hpp
    classes.hpp
    parse.hpp
    tokenize.hpp
)


add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# Compiler warnings
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic -Werror=return-type>
    $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra -Wpedantic -Werror=return-type>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
)

# Set output directory
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Print configuration info
message(STATUS "C++ Standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")