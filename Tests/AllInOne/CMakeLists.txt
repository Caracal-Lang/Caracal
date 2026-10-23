project(AllInOne)

add_executable(
    ${PROJECT_NAME} 
    "${PROJECT_SOURCE_DIR}/../DiagnosticsTests/Source/DiagnosticsTests.cpp"
    "${PROJECT_SOURCE_DIR}/../IRTests/Source/IRTests.cpp"
    "${PROJECT_SOURCE_DIR}/../LLVMCodeGenTests/Source/LLVMCodeGenTests.cpp"
    "${PROJECT_SOURCE_DIR}/../LexerTests/Source/LexerTests.cpp"
    "${PROJECT_SOURCE_DIR}/../ParserTests/Source/ParserTests.cpp"
    "${PROJECT_SOURCE_DIR}/../ProjectTests/Source/ProjectTests.cpp"
    "${PROJECT_SOURCE_DIR}/../TypeCheckerTests/Source/TypeCheckerTests.cpp"
    "${PROJECT_SOURCE_DIR}/../SourceLocationTests/Source/SourceLocationTests.cpp"
    "Source/main.cpp")

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
