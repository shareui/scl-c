cmake_minimum_required(VERSION 3.16)
project(scl VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(SCL_C_SOURCES
    lib/src/arena.c
    lib/src/error.c
    lib/src/lexer.c
    lib/src/parser.c
    lib/src/value.c
    lib/src/doc.c
    lib/src/api.c
)

set(SCL_CXX_SOURCES
    lib/src/types.cpp
    lib/src/annotations.cpp
    lib/src/includes.cpp
    lib/src/serialize.cpp
)

add_library(scl SHARED ${SCL_C_SOURCES} ${SCL_CXX_SOURCES})

target_include_directories(scl
    PUBLIC  lib/include
    PRIVATE lib/src
)

target_compile_definitions(scl PRIVATE SCL_BUILD_SHARED)

set_source_files_properties(${SCL_C_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-Wall;-Wextra;-Werror=implicit-function-declaration"
)

set_source_files_properties(${SCL_CXX_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-Wall;-Wextra;-Werror"
)

target_compile_options(scl PRIVATE
    $<$<CONFIG:Release>:-O3;-DNDEBUG;-fdata-sections;-ffunction-sections>
)

if(APPLE)
    target_link_options(scl PRIVATE $<$<CONFIG:Release>:-Wl,-dead_strip>)
elseif(UNIX)
    target_link_options(scl PRIVATE $<$<CONFIG:Release>:-Wl,--gc-sections>)
endif()

set_target_properties(scl PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "scl"
)

include(GNUInstallDirs)
install(TARGETS scl LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES lib/include/scl.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
