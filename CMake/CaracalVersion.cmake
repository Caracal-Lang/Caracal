# writes a header defining CARA_VERSION for the HEAD commit
# YYYY.MONTH.PATCH from the HEAD commit date, PATCH is day * 1000 + index of HEAD among that day's commits
# the header is only rewritten when the version changes

if(NOT DEFINED CARA_VERSION_HEADER)
    message(FATAL_ERROR "CARA_VERSION_HEADER must be defined with -DCARA_VERSION_HEADER=path/to/header")
endif()

set(CARA_COMMIT_DATES "")
if(CARA_GIT_EXECUTABLE)
    # committer dates of the first-parent history, newest first, in each commit's own time zone
    execute_process(
        COMMAND "${CARA_GIT_EXECUTABLE}" log --first-parent -n 1000 --format=%cd --date=format:%Y.%m.%d HEAD
        WORKING_DIRECTORY "${CARA_ROOT_DIR}"
        OUTPUT_VARIABLE CARA_COMMIT_DATES
        RESULT_VARIABLE CARA_GIT_RESULT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(NOT CARA_GIT_RESULT EQUAL 0)
        set(CARA_COMMIT_DATES "")
    endif()
endif()

if(CARA_COMMIT_DATES STREQUAL "")
    # no git history, falls back to the UTC build date with build index 0
    string(TIMESTAMP CARA_HEAD_DATE "%Y.%m.%d" UTC)
    set(CARA_BUILD_INDEX 0)
else()
    string(REPLACE "\n" ";" CARA_COMMIT_DATES "${CARA_COMMIT_DATES}")
    list(GET CARA_COMMIT_DATES 0 CARA_HEAD_DATE)
    set(CARA_BUILD_INDEX -1)
    foreach(CARA_COMMIT_DATE IN LISTS CARA_COMMIT_DATES)
        if(NOT CARA_COMMIT_DATE STREQUAL CARA_HEAD_DATE)
            break()
        endif()
        math(EXPR CARA_BUILD_INDEX "${CARA_BUILD_INDEX} + 1")
    endforeach()
endif()

string(REPLACE "." ";" CARA_DATE_PARTS "${CARA_HEAD_DATE}")
list(GET CARA_DATE_PARTS 0 CARA_YEAR)
list(GET CARA_DATE_PARTS 1 CARA_MONTH)
list(GET CARA_DATE_PARTS 2 CARA_DAY)
string(REGEX REPLACE "^0" "" CARA_MONTH "${CARA_MONTH}")
string(REGEX REPLACE "^0" "" CARA_DAY "${CARA_DAY}")
math(EXPR CARA_PATCH "${CARA_DAY} * 1000 + ${CARA_BUILD_INDEX}")

set(CARA_VERSION "${CARA_YEAR}.${CARA_MONTH}.${CARA_PATCH}")
file(CONFIGURE OUTPUT "${CARA_VERSION_HEADER}"
    CONTENT "#pragma once\n\n#define CARA_VERSION \"${CARA_VERSION}\"\n")
