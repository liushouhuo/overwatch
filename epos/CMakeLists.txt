cmake_minimum_required(VERSION 3.14.0 FATAL_ERROR)
project(epos DESCRIPTION "EPOS" VERSION 1.1.1 LANGUAGES CXX)

set(PROJECT_VENDOR "Xiphos")
set(PROJECT_COPYRIGHT "2019 Alexej Harm")
configure_file(res/version.h.in ${CMAKE_BINARY_DIR}/src/version.h LF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

string(REGEX REPLACE "/Z[7Ii]" "" CMAKE_C_FLAGS_DEBUG ${CMAKE_C_FLAGS_DEBUG})
string(REGEX REPLACE "/Z[7Ii]" "" CMAKE_CXX_FLAGS_DEBUG ${CMAKE_CXX_FLAGS_DEBUG})

add_definitions(-D_UNICODE -DUNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00)
add_definitions(-D_CRT_SECURE_NO_DEPRECATE -D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE)
add_definitions(-D_ATL_SECURE_NO_DEPRECATE -D_SCL_SECURE_NO_WARNINGS)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER build)
set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT epos)

# Executable
file(GLOB headers CONFIGURE_DEPENDS src/*.hpp)
file(GLOB sources CONFIGURE_DEPENDS src/*.cpp src/main.rc src/main.manifest)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX "" FILES ${headers} ${sources})

add_executable(epos WIN32 ${headers} ${sources})
set_target_properties(epos PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
target_include_directories(epos PRIVATE ${CMAKE_BINARY_DIR}/src src)
target_link_libraries(epos PRIVATE d2d1 d3d10 d3d11 dxgi dxguid dcomp dwrite)
target_compile_options(epos PRIVATE $<$<CONFIG:Debug>:/ZI>)

find_package(deus REQUIRED HINTS ../deus)
target_link_libraries(epos PRIVATE deus::deus)

find_package(fmt CONFIG REQUIRED)
target_link_libraries(epos PRIVATE fmt::fmt)

add_custom_command(TARGET epos POST_BUILD COMMAND
  ${CMAKE_COMMAND} -E copy_if_different ${DEUS} $<TARGET_FILE_DIR:epos>)

install(TARGETS epos RUNTIME DESTINATION .)
install(FILES ${DEUS} DESTINATION .)

# Info
file(GLOB headers CONFIGURE_DEPENDS src/info/*.hpp)
file(GLOB sources CONFIGURE_DEPENDS src/info/*.cpp)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src/info PREFIX "" FILES ${headers} ${sources})

add_executable(info EXCLUDE_FROM_ALL ${headers} ${sources})
set_target_properties(info PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
target_compile_options(info PRIVATE $<$<CONFIG:Debug>:/ZI>)

target_link_libraries(info PRIVATE deus::deus)
target_link_libraries(info PRIVATE fmt::fmt)

add_custom_command(TARGET info POST_BUILD COMMAND
  ${CMAKE_COMMAND} -E copy_if_different ${DEUS} $<TARGET_FILE_DIR:info>)
