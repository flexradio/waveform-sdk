include(CMakeFindDependencyMacro)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_LIST_DIR}/Modules")
find_dependency(LibEvent)
find_dependency(PthreadWorkQueue)

include("${CMAKE_CURRENT_LIST_DIR}/LibWaveformTargets.cmake")