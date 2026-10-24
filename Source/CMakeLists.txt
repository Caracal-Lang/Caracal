project(Caracal)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/Caracal/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/Caracal/*.h")

add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})
add_dependencies(${PROJECT_NAME} copy_core_files_buildstep)

# /Ob2 refuses to inline the one-byte vector push_back, /Ob3 inlines all of them
set_source_files_properties("${PROJECT_SOURCE_DIR}/Caracal/Syntax/Lexer.cpp"
    PROPERTIES COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:MSVC>:/Ob3>")

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(${PROJECT_NAME} PRIVATE ${LLVM_DEFINITIONS})

llvm_map_components_to_libnames(LLVM_LIBS core support native passes)
target_link_libraries(${PROJECT_NAME} PRIVATE ${LLVM_LIBS} CaraReport nlohmann_json::nlohmann_json)
if(CARACAL_TRACY)
    target_link_libraries(${PROJECT_NAME} PRIVATE TracyClient)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "CaracalCore")

include(GenerateExportHeader)
generate_export_header(${PROJECT_NAME}
    BASE_NAME ${PROJECT_NAME}
    EXPORT_MACRO_NAME CARACAL_API
    EXPORT_FILE_NAME ${PROJECT_SOURCE_DIR}/${PROJECT_NAME}/API.h
)

find_package(Git QUIET)
set(CARA_VERSION_HEADER "${PROJECT_SOURCE_DIR}/${PROJECT_NAME}/Version.h")
add_custom_target(caracal_version_buildstep
    COMMAND ${CMAKE_COMMAND}
        -DCARA_ROOT_DIR="${CARA_ROOT_DIR}"
        -DCARA_GIT_EXECUTABLE="${GIT_EXECUTABLE}"
        -DCARA_VERSION_HEADER="${CARA_VERSION_HEADER}"
        -P "${CARA_ROOT_DIR}/CMake/CaracalVersion.cmake"
    BYPRODUCTS "${CARA_VERSION_HEADER}"
    COMMENT "Updating the Caracal version from git"
)
add_dependencies(${PROJECT_NAME} caracal_version_buildstep)
