add_library(pthread_workqueue STATIC
        src/linux/load.c
        src/linux/thread_info.c
        src/linux/thread_rt.c
        src/posix/manager.c
        src/api.c
        src/witem_cache.c
)
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
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        )