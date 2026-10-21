project(Driver)

file(GLOB SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/source/*.cpp")
file(GLOB HEADERS CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/source/*.h")

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_link_libraries(${PROJECT_NAME} PRIVATE Caracal)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "Caracal")

if (WIN32)
    add_custom_command(
        TARGET ${PROJECT_NAME} PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:${PROJECT_NAME}> $<TARGET_FILE_DIR:${PROJECT_NAME}>
        COMMAND_EXPAND_LISTS
    )

    # copy the lld-link from the linked LLVM
    find_program(CARA_LLD_LINK lld-link HINTS "${LLVM_TOOLS_BINARY_DIR}" NO_DEFAULT_PATH)
    if (CARA_LLD_LINK)
        set(CARA_BUNDLED_LLD_LINK "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/lld-link.exe")
        add_custom_command(
            OUTPUT "${CARA_BUNDLED_LLD_LINK}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CARA_LLD_LINK}" "${CARA_BUNDLED_LLD_LINK}"
            DEPENDS "${CARA_LLD_LINK}"
            COMMENT "Copying lld-link next to the compiler"
        )
        add_custom_target(copy_lld_link_buildstep ALL DEPENDS "${CARA_BUNDLED_LLD_LINK}")
        add_dependencies(${PROJECT_NAME} copy_lld_link_buildstep)
    else ()
        message(STATUS "No lld-link in ${LLVM_TOOLS_BINARY_DIR}, caracal links with the lld-link on the PATH")
    endif ()
endif ()
