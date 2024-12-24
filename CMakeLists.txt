cmake_minimum_required(VERSION 3.0)
 
project(redisfmt)

include(FetchContent)

FetchContent_Declare(
    fmt 
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)
FetchContent_Declare(
    expected
    GIT_REPOSITORY https://github.com/TartanLlama/expected.git
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)

FetchContent_Declare(
    hiredis
    GIT_REPOSITORY https://github.com/redis/hiredis.git
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)



set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE "Debug")
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/build)

load_cache(LIB_FMT_DIR "")
load_cache(LIB_EXPECTED_DIR "")
load_cache(LIB_HIREDIS_DIR "")
if("${LIB_FMT_DIR}" STREQUAL "")
    FetchContent_GetProperties(fmt)
    if(NOT fmt_POPULATED)
        FetchContent_MakeAvailable(fmt)
    endif()
endif()


if("${LIB_EXPECTED_DIR}" STREQUAL "")
    FetchContent_GetProperties(expected)
    if(NOT expected_POPULATED)
        FetchContent_MakeAvailable(expected)
    endif()
endif()

if("${LIB_HIREDIS_DIR}" STREQUAL "")
    FetchContent_GetProperties(hiredis)
    if(NOT hiredis_POPULATED)
        FetchContent_MakeAvailable(hiredis)
    endif()
endif()

set(redisfmt_files ${CMAKE_CURRENT_SOURCE_DIR}/include/redisfmt.hpp)

# msvc 按文件夹分组
macro(source_group_by_dir source_files)
    if(MSVC)
        set(sgbd_cur_dir ${CMAKE_CURRENT_SOURCE_DIR})
        foreach(sgbd_file ${${source_files}})
            string(REGEX REPLACE ${sgbd_cur_dir}/\(.*\) \\1 sgbd_fpath ${sgbd_file})
            string(REGEX REPLACE "\(.*\)/.*" \\1 sgbd_group_name ${sgbd_fpath})
            string(COMPARE EQUAL ${sgbd_fpath} ${sgbd_group_name} sgbd_nogroup)
            string(REPLACE "/" "\\" sgbd_group_name ${sgbd_group_name})
            if(sgbd_nogroup)
                set(sgbd_group_name "\\")
            endif(sgbd_nogroup)
            source_group(${sgbd_group_name} FILES ${sgbd_file})
        endforeach(sgbd_file)
    endif(MSVC)
endmacro(source_group_by_dir)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(example ${CMAKE_CURRENT_SOURCE_DIR}/example/example.cpp)
target_link_libraries(example fmt::fmt tl::expected hiredis::hiredis)
 
add_executable(test2 ${CMAKE_CURRENT_SOURCE_DIR}/example/test2.cpp)
target_link_libraries(test2 fmt::fmt)
