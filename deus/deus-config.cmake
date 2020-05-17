# Usage:
#
#  find_package(deus REQUIRED HINTS path/to/deus)
#  target_link_libraries(main PRIVATE deus::deus)
#  add_custom_command(TARGET main POST_BUILD
#    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${DEUS} $<TARGET_FILE_DIR:test>)
# 

if(NOT TARGET deus::deus)
  add_library(deus::deus SHARED IMPORTED)
  set_target_properties(deus::deus PROPERTIES
    INTERFACE_COMPILE_DEFINITIONS "UMDF_USING_NTSTATUS=1"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/include"
    IMPORTED_LOCATION "${CMAKE_CURRENT_LIST_DIR}/lib/deus.dll"
    IMPORTED_IMPLIB "${CMAKE_CURRENT_LIST_DIR}/lib/deus.lib"
    IMPORTED_LINK_INTERFACE_LANGUAGES "C")
endif()

get_target_property(DEUS_DLL deus::deus IMPORTED_LOCATION)
get_target_property(DEUS_LIB deus::deus IMPORTED_IMPLIB)
set(DEUS_SYS "${CMAKE_CURRENT_LIST_DIR}/lib/deus.sys")
set(DEUS_TBB "${CMAKE_CURRENT_LIST_DIR}/lib/tbb.dll")
set(DEUS ${DEUS_DLL} ${DEUS_SYS} ${DEUS_TBB})
