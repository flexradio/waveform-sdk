set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules")

set(WAVEFORM_SRCS
        utils.c
        waveform.c radio.c vita.c meters.c discovery.c)

set(WAVEFORM_HDRS
        utils.h
        meters.h
        vita.h)

FetchContent_Declare(sds
        GIT_REPOSITORY https://github.com/antirez/sds.git
        GIT_TAG 2.0.0
)
FetchContent_GetProperties(sds)
if(NOT sds_POPULATED)
    FetchContent_Populate(sds)
endif()
set(sds_SOURCES ${sds_SOURCE_DIR}/sds.c ${sds_SOURCE_DIR}/sds.h ${sds_SOURCE_DIR}/sdsalloc.h)

FetchContent_Declare(utlist
        GIT_REPOSITORY https://github.com/troydhanson/uthash.git
        GIT_TAG v2.1.0
        )
FetchContent_GetProperties(utlist)
if (NOT utlist_POPULATED)
    FetchContent_Populate(utlist)
endif ()

find_package(LibEvent REQUIRED)
find_package(PthreadWorkQueue REQUIRED)

add_library(waveform SHARED ${WAVEFORM_SRCS} ${WAVEFORM_HDRS} ${sds_SOURCES})
target_link_libraries(waveform
        PUBLIC
        LibEvent::LibEvent
        PthreadWorkQueue::PthreadWorkQueue
        pthread
        )

target_include_directories(waveform
        PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        PRIVATE
        ${sds_SOURCE_DIR}
        ${utlist_SOURCE_DIR}/src
        )

