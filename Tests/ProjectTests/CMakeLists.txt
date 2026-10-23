project(ProjectTests)

file(GLOB SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/Source/*.cpp")
file(GLOB HEADERS CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/Source/*.h")

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_link_libraries(${PROJECT_NAME} PRIVATE Caracal CaraTest)

add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})

if (WIN32)
    add_custom_command(
        TARGET ${PROJECT_NAME} PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:${PROJECT_NAME}> $<TARGET_FILE_DIR:${PROJECT_NAME}>
        COMMAND_EXPAND_LISTS
    )
endif ()
