cmake_minimum_required(VERSION 3.16)
project(libwaveform C)

set(CMAKE_C_STANDARD 11)

include(FetchContent)
include(GNUInstallDirs)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules")
message(STATUS "CMAKE_MODULE_PATH=${CMAKE_MODULE_PATH}")

function(define_file_basename_for_sources targetname)
    get_target_property(source_files "${targetname}" SOURCES)
    foreach (sourcefile ${source_files})
        # Add the FILE_BASENAME=filename compile definition to the list.
        get_filename_component(basename "${sourcefile}" NAME)
        # Set the updated compile definitions on the source file.
        set_property(
                SOURCE "${sourcefile}" APPEND
                PROPERTY COMPILE_DEFINITIONS "FILE_BASENAME=\"${basename}\"")
    endforeach ()
endfunction()

set(WAVEFORM_SRCS
        src/utils.c
        src/waveform.c
        src/radio.c
        src/vita.c
        src/meters.c
        src/discovery.c)

set(WAVEFORM_HDRS
        src/utils.h
        src/meters.h
        src/vita.h)

FetchContent_Declare(sds
        GIT_REPOSITORY https://github.com/antirez/sds.git
        GIT_TAG 2.0.0
        )
FetchContent_GetProperties(sds)
if (NOT sds_POPULATED)
    FetchContent_Populate(sds)
endif ()
set(sds_SOURCES ${sds_SOURCE_DIR}/sds.c ${sds_SOURCE_DIR}/sds.h ${sds_SOURCE_DIR}/sdsalloc.h)

FetchContent_Declare(pthread-wq
        GIT_REPOSITORY https://github.com/mheily/libpwq.git
        GIT_TAG v0.9.1
        )
FetchContent_GetProperties(pthread-wq)
if (NOT pthread-wq_POPULATED)
    FetchContent_Populate(pthread-wq)
endif ()

add_library(pthread_workqueue STATIC
        ${pthread-wq_SOURCE_DIR}/src/api.c
        ${pthread-wq_SOURCE_DIR}/src/witem_cache.c
        ${pthread-wq_SOURCE_DIR}/src/linux/load.c
        ${pthread-wq_SOURCE_DIR}/src/linux/thread_info.c
        ${pthread-wq_SOURCE_DIR}/src/linux/thread_rt.c
        ${pthread-wq_SOURCE_DIR}/src/linux/thread_rt.c
        ${pthread-wq_SOURCE_DIR}/src/posix/manager.c
        )
target_include_directories(pthread_workqueue
        PUBLIC
        ${pthread-wq_SOURCE_DIR}/include
        PRIVATE
        ${pthread-wq_SOURCE_DIR}/src
        )
target_compile_options(pthread_workqueue PUBLIC -fPIC)

FetchContent_Declare(utlist
        GIT_REPOSITORY https://github.com/troydhanson/uthash.git
        GIT_TAG v2.1.0
        )
FetchContent_GetProperties(utlist)
if (NOT utlist_POPULATED)
    FetchContent_Populate(utlist)
endif ()

find_package(LibEvent REQUIRED)

add_library(waveform SHARED ${WAVEFORM_SRCS} ${WAVEFORM_HDRS} ${sds_SOURCES})
set_target_properties(waveform PROPERTIES
        PUBLIC_HEADER "include/waveform_api.h"
        SOVERSION 1
        VERSION 1.0)

target_link_libraries(waveform
        PUBLIC
        LibEvent::LibEvent
        pthread
        m
        PRIVATE
        pthread_workqueue
        )
target_link_options(waveform PRIVATE -Wl,--as-needed)

target_include_directories(waveform
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include/waveform>
        PRIVATE
        ${sds_SOURCE_DIR}
        ${utlist_SOURCE_DIR}/src
        )
define_file_basename_for_sources(waveform)

find_package(Doxygen)
if (DOXYGEN_FOUND)
    set(DOXYGEN_OPTIMIZE_OUTPUT_FOR_C YES)
    set(DOXYGEN_PROJECT_NAME "SmartSDR Waveform SDK")
    set(DOXYGEN_PROJECT_NUMBER "1.0")
    set(DOXYGEN_GENERATE_LATEX NO)

    doxygen_add_docs(doxygen include/waveform_api.h ALL)
    install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/html TYPE DOC)
endif ()

set(CMAKE_INSTALL_LIBDIR "lib/${CMAKE_LIBRARY_ARCHITECTURE}")
set(CMAKE_INSTALL_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}/waveform")
install(TARGETS waveform EXPORT LibWaveformTargets
        LIBRARY
        DESTINATION "${LIBDIR}"
        PUBLIC_HEADER
        DESTINATION "${INCLUDEDIR}")

install(DIRECTORY ${CMAKE_SOURCE_DIR}/example TYPE DOC)
install(FILES ${CMAKE_SOURCE_DIR}/doc/README.md TYPE DOC)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/LibWaveform/LibWaveformConfigVersion.cmake"
        VERSION "1.0"
        COMPATIBILITY AnyNewerVersion
)
export(EXPORT LibWaveformTargets
        FILE "${CMAKE_CURRENT_BINARY_DIR}/LibWaveform/LibWaveformTargets.cmake"
        NAMESPACE Waveform::
        )
configure_file(cmake/LibWaveformConfig.cmake
        "${CMAKE_CURRENT_BINARY_DIR}/LibWaveform/LibWaveformConfig.cmake"
        COPYONLY
        )

set(ConfigPackageLocation lib/${CMAKE_LIBRARY_ARCHITECTURE}/cmake/LibWaveform)
install(EXPORT LibWaveformTargets
        FILE
        LibWaveformTargets.cmake
        NAMESPACE
        LibWaveform::
        DESTINATION
        ${ConfigPackageLocation}
        )
install(
        FILES
        cmake/LibWaveformConfig.cmake
        "${CMAKE_CURRENT_BINARY_DIR}/LibWaveform/LibWaveformConfigVersion.cmake"
        DESTINATION
        ${ConfigPackageLocation}
)
install(
        DIRECTORY
        cmake/Modules
        DESTINATION
        ${ConfigPackageLocation}
)
