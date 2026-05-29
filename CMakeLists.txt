cmake_minimum_required(VERSION 3.16)
project(tscflp_ga_elitism LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(LEMON_ROOT "" CACHE PATH "LEMON install prefix")

find_package(LEMON QUIET)
if(LEMON_FOUND AND NOT TARGET lemon::lemon)
    if(LEMON_LIBRARIES AND LEMON_INCLUDE_DIRS)
        add_library(lemon::lemon UNKNOWN IMPORTED)
        set_target_properties(lemon::lemon PROPERTIES
            IMPORTED_LOCATION "${LEMON_LIBRARIES}"
            INTERFACE_INCLUDE_DIRECTORIES "${LEMON_INCLUDE_DIRS}"
        )
    endif()
endif()
if(NOT TARGET lemon::lemon)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LEMON_PC QUIET lemon)
        if(LEMON_PC_FOUND)
            find_library(LEMON_LIBRARY NAMES lemon emon PATHS ${LEMON_PC_LIBRARY_DIRS})
            if(LEMON_LIBRARY)
                add_library(lemon::lemon UNKNOWN IMPORTED)
                set_target_properties(lemon::lemon PROPERTIES
                    IMPORTED_LOCATION "${LEMON_LIBRARY}"
                    INTERFACE_INCLUDE_DIRECTORIES "${LEMON_PC_INCLUDE_DIRS}"
                )
            endif()
        endif()
    endif()
endif()
if(NOT TARGET lemon::lemon AND LEMON_ROOT)
    find_path(LEMON_INCLUDE_DIR lemon/list_graph.h PATHS ${LEMON_ROOT}/include)
    find_library(LEMON_LIBRARY NAMES lemon emon PATHS ${LEMON_ROOT}/lib)
    if(LEMON_INCLUDE_DIR AND LEMON_LIBRARY)
        add_library(lemon::lemon UNKNOWN IMPORTED)
        set_target_properties(lemon::lemon PROPERTIES
            IMPORTED_LOCATION "${LEMON_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${LEMON_INCLUDE_DIR}"
        )
    endif()
endif()
if(NOT TARGET lemon::lemon)
    message(FATAL_ERROR "LEMON not found. Install it or set LEMON_ROOT/LEMON_DIR, or ensure pkg-config can find lemon.pc.")
endif()

set(HIGHS_ROOT "" CACHE PATH "HiGHS install prefix")
find_package(HiGHS QUIET)
if(TARGET highs)
    set(TSCFLP_HIGHS_TARGET highs)
elseif(HiGHS_FOUND)
    set(TSCFLP_HIGHS_TARGET highs)
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(HIGHS_PC QUIET highs)
        if(HIGHS_PC_FOUND)
            find_library(HIGHS_LIBRARY NAMES highs PATHS ${HIGHS_PC_LIBRARY_DIRS})
            if(HIGHS_LIBRARY)
                add_library(highs UNKNOWN IMPORTED)
                set_target_properties(highs PROPERTIES
                    IMPORTED_LOCATION "${HIGHS_LIBRARY}"
                    INTERFACE_INCLUDE_DIRECTORIES "${HIGHS_PC_INCLUDE_DIRS};${HIGHS_PC_INCLUDE_DIRS}/highs"
                )
                set(TSCFLP_HIGHS_TARGET highs)
            endif()
        endif()
    endif()
    if(NOT TSCFLP_HIGHS_TARGET AND HIGHS_ROOT)
        find_path(HIGHS_INCLUDE_DIR highs/Highs.h PATHS ${HIGHS_ROOT}/include)
        if(NOT HIGHS_INCLUDE_DIR)
            find_path(HIGHS_INCLUDE_DIR Highs.h PATHS ${HIGHS_ROOT}/include/highs)
            if(HIGHS_INCLUDE_DIR)
                set(HIGHS_INCLUDE_DIR "${HIGHS_ROOT}/include/highs")
            endif()
        elseif(HIGHS_INCLUDE_DIR STREQUAL "${HIGHS_ROOT}/include")
            set(HIGHS_INCLUDE_DIR "${HIGHS_ROOT}/include/highs")
        endif()
        find_library(HIGHS_LIBRARY NAMES highs PATHS ${HIGHS_ROOT}/lib)
        if(HIGHS_INCLUDE_DIR AND HIGHS_LIBRARY)
            add_library(highs UNKNOWN IMPORTED)
            set_target_properties(highs PROPERTIES
                IMPORTED_LOCATION "${HIGHS_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${HIGHS_INCLUDE_DIR}"
            )
            set(TSCFLP_HIGHS_TARGET highs)
        endif()
    endif()
endif()

if(NOT TSCFLP_HIGHS_TARGET)
    message(FATAL_ERROR "HiGHS not found. Install it or set HIGHS_ROOT/HiGHS_DIR, or ensure pkg-config can find highs.pc.")
endif()

add_executable(tscflp_ga
    src/main.cpp
    src/DataLoader.cpp
    src/FlowSolver.cpp
    src/LPSolver.cpp
    src/GA.cpp
)

target_include_directories(tscflp_ga PRIVATE include)

target_link_libraries(tscflp_ga
    PRIVATE
        lemon::lemon
    ${TSCFLP_HIGHS_TARGET}
)
