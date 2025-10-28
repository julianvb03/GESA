cmake_minimum_required(VERSION 3.14)
project(GSEA VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -O2)
elseif(MSVC)
    add_compile_options(/W4 /O2)
endif()

include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/src)

find_package(OpenSSL REQUIRED)
find_package(OpenMP)


file(GLOB CLI_SOURCES "src/cli/*.cpp")
if(CLI_SOURCES)
    add_library(cli_lib STATIC ${CLI_SOURCES})
else()
    add_library(cli_lib INTERFACE)
endif()


file(GLOB_RECURSE COMPRESSION_SOURCES "src/compression/*.cpp")
if(COMPRESSION_SOURCES)
    add_library(compression_lib STATIC ${COMPRESSION_SOURCES})
else()
    add_library(compression_lib INTERFACE)
endif()

# Encryption library (explicit sources to ensure correct linkage)
set(ENCRYPTION_SOURCES
    src/encryption/RSA.cpp
)
add_library(encryption_lib STATIC ${ENCRYPTION_SOURCES})

file(GLOB FILESYSTEM_SOURCES "src/filesystem/*.cpp")
if(FILESYSTEM_SOURCES)
    add_library(filesystem_lib STATIC ${FILESYSTEM_SOURCES})
else()
    add_library(filesystem_lib INTERFACE)
endif()

file(GLOB CONCURRENCY_SOURCES "src/concurrency/*.cpp")
if(CONCURRENCY_SOURCES)
    add_library(concurrency_lib STATIC ${CONCURRENCY_SOURCES})
else()
    add_library(concurrency_lib INTERFACE)
endif()

file(GLOB UTILS_SOURCES "src/utils/*.cpp")
if(UTILS_SOURCES)
    add_library(utils_lib STATIC ${UTILS_SOURCES})
else()
    add_library(utils_lib INTERFACE)
endif()

# Link external dependencies
if(TARGET utils_lib)
    # OpenSSL for Base64 BIO usage in Utils
    target_link_libraries(utils_lib PUBLIC OpenSSL::Crypto)
    # OpenMP for parallel utils (createFreqMap)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(utils_lib PUBLIC OpenMP::OpenMP_CXX)
    endif()
endif()

add_executable(gsea src/main.cpp)

target_link_libraries(gsea
    cli_lib
    compression_lib
    encryption_lib
    filesystem_lib
    concurrency_lib
    utils_lib
)

if(UNIX)
    target_link_libraries(gsea pthread)
endif()

option(BUILD_TESTS "Build tests" ON)

if(BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG release-1.12.1
    )
    
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    
    FetchContent_MakeAvailable(googletest)
    
    enable_testing()
    
    file(GLOB TEST_SOURCES "tests/*.cpp")
    
    add_executable(gsea_tests ${TEST_SOURCES})
    
    target_link_libraries(gsea_tests
        gtest_main
        gtest
        gmock
        cli_lib
        compression_lib
        encryption_lib
        filesystem_lib
        concurrency_lib
        utils_lib
    )
    
    if(UNIX)
        target_link_libraries(gsea_tests pthread)
    endif()
    
    include(GoogleTest)
    gtest_discover_tests(gsea_tests)
    
    add_custom_target(run_tests
        COMMAND gsea_tests
        DEPENDS gsea_tests
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running tests..."
    )
endif()

#install(TARGETS gsea DESTINATION demostration)
#install(DIRECTORY include/ DESTINATION include)

message(STATUS "")
message(STATUS "GSEA - Configuration Summary")
message(STATUS "============================")
message(STATUS "CMake version    : ${CMAKE_VERSION}")
message(STATUS "C++ compiler     : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "C++ standard     : ${CMAKE_CXX_STANDARD}")
message(STATUS "Build type       : ${CMAKE_BUILD_TYPE}")
message(STATUS "Build tests      : ${BUILD_TESTS}")
message(STATUS "Install prefix   : ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
message(STATUS "Targets:")
message(STATUS "  - gsea         : Main executable")
if(BUILD_TESTS)
message(STATUS "  - gsea_tests   : Test executable")
message(STATUS "  - run_tests    : Run all tests")
endif()
message(STATUS "============================")
message(STATUS "")