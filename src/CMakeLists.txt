set(WAVEFORM_SRCS
        utils.c
        waveform.c radio.c vita.c vita.h)

set(WAVEFORM_HDRS
        utils.h
        )

set(EVENT__LIBRARY_TYPE "STATIC" CACHE STRING "")
set(EVENT__DISABLE_OPENSSL ON CACHE BOOL "")
set(EVENT__DISABLE_BENCHMARK ON CACHE BOOL "")
set(EVENT__DISABLE_TESTS ON CACHE BOOL "")
set(EVENT__DISABLE_SAMPLES ON CACHE BOOL "")

FetchContent_Declare(event
        GIT_REPOSITORY https://github.com/libevent/libevent.git
        GIT_TAG release-2.1.11-stable
)
FetchContent_MakeAvailable(event)

FetchContent_Declare(sds
        GIT_REPOSITORY https://github.com/antirez/sds.git
        GIT_TAG 2.0.0
)
FetchContent_GetProperties(sds)
if(NOT sds_POPULATED)
    FetchContent_Populate(sds)
endif()
set(sds_SOURCES ${sds_SOURCE_DIR}/sds.c ${sds_SOURCE_DIR}/sds.h ${sds_SOURCE_DIR}/sdsalloc.h )

set(STATIC_WORKQUEUE ON CACHE BOOL "")
FetchContent_Declare(pthread_wq
        GIT_REPOSITORY https://github.com/mheily/libpwq.git
        GIT_TAG v0.9.1
)
if(NOT pthread_wq_POPULATED)
    FetchContent_Populate(pthread_wq)
endif()
file(GLOB pthread_wq_src
        ${pthread_wq_SOURCE_DIR}/src/linux/*.h
        ${pthread_wq_SOURCE_DIR}/src/linux/*.c
        ${pthread_wq_SOURCE_DIR}/src/posix/*.h
        ${pthread_wq_SOURCE_DIR}/src/posix/*.c
        ${pthread_wq_SOURCE_DIR}/src/*.h
        ${pthread_wq_SOURCE_DIR}/src/*.c
)
add_library(pthread_workqueue STATIC ${pthread_wq_src})
target_compile_definitions(pthread_workqueue PRIVATE
        MAKE_STATIC
        HAVE_ERR_H=1
        _XOPEN_SOURCE=600
        _GNU_SOURCE
)
target_compile_options(pthread_workqueue PRIVATE
        -std=c99
        -fPIC
)
target_include_directories(pthread_workqueue
        PUBLIC
            ${pthread_wq_SOURCE_DIR}/include
        PRIVATE
            ${pthread_wq_SOURCE_DIR}/src
        )

add_library(waveform STATIC ${WAVEFORM_SRCS} ${WAVEFORM_HDRS} ${sds_SOURCES})
target_link_libraries(waveform PUBLIC
        event_core_static
        event_extra_static
        event_pthreads_static
        pthread pthread_workqueue
)
target_include_directories(waveform PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${event_SOURCE_DIR}/include
        ${event_BINARY_DIR}/include
        ${pthread_wq_SOURCE_DIR}/include
        ${sds_SOURCE_DIR}
)