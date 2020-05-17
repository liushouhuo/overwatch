cmake_minimum_required(VERSION 3.14.0 FATAL_ERROR)
project(deus DESCRIPTION "DEUS" VERSION 1.1.1 LANGUAGES C CXX)

set(PROJECT_VENDOR "Xiphos")
set(PROJECT_COPYRIGHT "2019 Alexej Harm")
configure_file(res/version.h.in ${CMAKE_BINARY_DIR}/src/version.h LF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_definitions(-D_UNICODE -DUNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00)
add_definitions(-D_CRT_SECURE_NO_DEPRECATE -D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE)
add_definitions(-D_ATL_SECURE_NO_DEPRECATE -D_SCL_SECURE_NO_WARNINGS -DUMDF_USING_NTSTATUS)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER build)
set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT main)

list(INSERT CMAKE_MODULE_PATH 0 ${CMAKE_SOURCE_DIR}/res/cmake)

# DSEFix
file(GLOB_RECURSE dsefix CONFIGURE_DEPENDS src/dsefix/*.h src/dsefix/*.c src/dsefix/*.hpp src/dsefix/*.cpp)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX "" FILES ${dsefix})

# Library
file(GLOB headers CONFIGURE_DEPENDS src/deus.hpp)
file(GLOB sources CONFIGURE_DEPENDS src/deus.cpp src/deus.rc)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX "" FILES ${headers} ${sources})

add_library(deus SHARED ${headers} ${sources} ${dsefix})
target_compile_definitions(deus PRIVATE DEUS_EXPORTS=1)
target_include_directories(deus PRIVATE ${CMAKE_BINARY_DIR}/src src src/dsefix)
target_link_libraries(deus PRIVATE legacy_stdio_definitions ntdll)

find_package(TBB CONFIG REQUIRED)
target_link_libraries(deus PRIVATE TBB::tbb)

install(FILES src/deus.hpp CONFIGURATIONS Release DESTINATION include)
install(TARGETS deus CONFIGURATIONS Release ARCHIVE DESTINATION lib RUNTIME DESTINATION lib)

install(CODE [[
  file(GLOB libraries ${CMAKE_BINARY_DIR}/Release/tbb*.dll)
  file(INSTALL ${libraries} DESTINATION ${CMAKE_INSTALL_PREFIX}/lib)
]])

# Driver
file(GLOB headers CONFIGURE_DEPENDS src/deus.hxx)
file(GLOB sources CONFIGURE_DEPENDS src/deus.cxx)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX "" FILES ${headers} ${sources})

find_package(WDK REQUIRED)
wdk_add_driver(deus.sys KMDF 1.15 WINVER 0x0A00 ${headers} ${sources})
set_target_properties(deus.sys PROPERTIES OUTPUT_NAME deus)
target_include_directories(deus.sys PRIVATE ${CMAKE_BINARY_DIR}/src src)
target_compile_definitions(deus.sys PRIVATE DEUS_DRIVER=1 _WIN10_=1)
add_dependencies(deus deus.sys)

# "C:\Program Files (x86)\Windows Kits\10\bin\10.0.18362.0\x64\makecert.exe" ^
#   -r -pe -a sha256 -eku "1.3.6.1.5.5.7.3.3" -ss "My" -n "CN=Xiphos"

#add_custom_command(TARGET deus.sys POST_BUILD COMMAND
#  ${WDK_ROOT}/bin/${WDK_VERSION}/x64/signtool.exe sign
#  -ph -fd sha256 -s My -n Xiphos $<TARGET_FILE:deus.sys>)

install(TARGETS deus.sys CONFIGURATIONS Release RUNTIME DESTINATION lib)

# Executable
add_executable(main EXCLUDE_FROM_ALL src/main.cpp)
source_group("" FILES src/main.cpp)
set_target_properties(main PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
target_include_directories(main PRIVATE ${CMAKE_BINARY_DIR}/src src)
target_link_libraries(main PRIVATE deus)

find_package(fmt CONFIG REQUIRED)
target_link_libraries(main PRIVATE fmt::fmt)
