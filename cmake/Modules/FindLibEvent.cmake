find_library(LibEvent_LIBRARY NAMES event)
find_library(LibEvent_THREAD NAMES event_pthreads)

include(FindPackageHandleStandardArgs)

set(LibEvent_LIBRARIES
        ${LibEvent_LIBRARY}
        ${LibEvent_THREAD}
)

find_package_handle_standard_args(LibEvent DEFAULT_MSG LibEvent_LIBRARIES)

if (LibEvent_FOUND AND NOT TARGET LibEvent::LibEvent)
    add_library(LibEvent::LibEvent INTERFACE IMPORTED)
    set_target_properties(LibEvent::LibEvent PROPERTIES
            INTERFACE_LINK_LIBRARIES "${LibEvent_LIBRARIES}")
endif ()