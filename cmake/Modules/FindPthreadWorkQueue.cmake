find_path(PthreadWorkQueue_INCLUDE_DIRS NAMES pthread_workqueue.h)
find_library(PthreadWorkQueue_LIBRARIES NAMES pthread_workqueue)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(PthreadWorkQueue DEFAULT_MSG PthreadWorkQueue_LIBRARIES PthreadWorkQueue_INCLUDE_DIRS)
if (PthreadWorkQueue_FOUND AND NOT TARGET PthreadWorkQueue::PthreadWorkQueue)
    add_library(PthreadWorkQueue::PthreadWorkQueue INTERFACE IMPORTED)
    set_target_properties(PthreadWorkQueue::PthreadWorkQueue PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${PthreadWorkQueue_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${PthreadWorkQueue_LIBRARIES}")
endif ()
