find_file(waveform_INCLUDE_DIRS NAMES waveform_api.h
        PATHS ${CMAKE_SOURCE_DIR}/include)
find_library(waveform_LIBRARIES NAMES waveform
        PATHS ${CMAKE_BINARY_DIR})
find_package_handle_standard_args(waveform DEFAULT_MSG waveform_LIBRARIES waveform_INCLUDE_DIRS)

if (waveform_FOUND AND NOT TARGET Waveform::Waveform)
    add_library(Waveform::Waveform INTERFACE IMPORTED)
    set_target_properties(Waveform::Waveform PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${waveform_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${waveform_LIBRARIES}")
endif ()

add_executable(waveform-test EXCLUDE_FROM_ALL main.c)
target_link_libraries(waveform-test PRIVATE Waveform::Waveform)