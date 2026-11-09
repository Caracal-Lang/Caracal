cmake_minimum_required(VERSION 3.30)

project(Caracal)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake")

set(CARA_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "Caracal root directory")

include(GNUInstallDirs)
if(WIN32)
    if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    endif()
endif()

include(LLVM)
include(Tracy)
include(CaraReport)
include(NlohmannJson)
include(CaraTest)
include(CTest)
include(CopyBuildTarget)

# /GL and /LTCG on MSVC, -flto=thin on Clang
include(CheckIPOSupported)
check_ipo_supported(RESULT CARACAL_IPO_SUPPORTED OUTPUT CARACAL_IPO_OUTPUT LANGUAGES CXX)
if(CARACAL_IPO_SUPPORTED)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
else()
    message(WARNING "link time optimization is off, the toolchain does not support it: ${CARACAL_IPO_OUTPUT}")
endif()

add_subdirectory(Source)
add_subdirectory(Driver)

add_subdirectory(Tests/AllInOne)
add_subdirectory(Tests/Benchmarks)
add_subdirectory(Tests/DiagnosticsTests)
add_subdirectory(Tests/IRTests)
add_subdirectory(Tests/LLVMCodeGenTests)
add_subdirectory(Tests/LexerTests)
add_subdirectory(Tests/ParserTests)
add_subdirectory(Tests/ProjectTests)
add_subdirectory(Tests/SourceLocationTests)
add_subdirectory(Tests/TypeCheckerTests)
