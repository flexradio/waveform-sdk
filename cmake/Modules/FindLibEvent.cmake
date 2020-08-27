find_path(LibEvent_INCLUDE_DIRS NAMES event.h)
find_library(LibEvent_LIBRARY NAMES event)
find_library(LibEvent_CORE NAMES event_core)
find_library(LibEvent_EXTRA NAMES event_extra)
find_library(LibEvent_THREAD NAMES event_pthreads)

include(FindPackageHandleStandardArgs)

set(LibEvent_LIBRARIES
        ${LibEvent_LIBRARY}
        ${LibEvent_CORE}
        ${LibEvent_EXTRA}
        ${LibEvent_THREAD}
        ${LibEvent_EXTRA})

find_package_handle_standard_args(LibEvent DEFAULT_MSG LibEvent_LIBRARIES LibEvent_INCLUDE_DIRS)

if (LibEvent_FOUND AND NOT TARGET LibEvent::LibEvent)
    add_library(LibEvent::LibEvent INTERFACE IMPORTED)
    set_target_properties(LibEvent::LibEvent PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${LibEvent_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${LibEvent_LIBRARIES}")
endif ()