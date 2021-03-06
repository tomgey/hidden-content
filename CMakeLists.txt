project(LinksSystem)
cmake_minimum_required(VERSION 2.8.8)

set(PROJROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(LINKS_INCLUDE_DIR ${PROJROOT}/include ${Qt5Widgets_INCLUDE_DIRS})
set(COMPONENTS_DIR ${PROJROOT}/components)
set(COMPONENTS_INC_DIR )
set(COMPONENTS_LIBS )
set(BIN_DIR ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJROOT}/cmake)
set(LIBRARY_OUTPUT_PATH ${BIN_DIR}/lib) 

#include macros
include( cmake/CopyFiles.cmake )
include( cmake/AddComponent.cmake )
include( cmake/AddSubdirOption.cmake )

# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)

find_package(Qt5Core REQUIRED)
find_package(Qt5Widgets REQUIRED)
find_package(Qt5Script REQUIRED)
find_package(Qt5OpenGL REQUIRED)
find_package(Qt5WebSockets REQUIRED)
find_package(Qt5X11Extras REQUIRED)

# ------------------------------------------------------------------------------
# Warning when using in-source build
# ------------------------------------------------------------------------------

if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
    message(WARNING  "Avoid building inside the source tree!")
    message(WARNING  "Create a separate build directory instead (i.e. 'build') and call CMake from there: ")
    message(WARNING  "  mkdir ../build && cd ../build && cmake ${CMAKE_SOURCE_DIR}")
endif()

# ------------------------------------------------------------------------------
# custom options
# ------------------------------------------------------------------------------

message("--------------------")
message("Build configuration:")
message("--------------------")

# direct desktop blending
set( RenderUseDesktopBlend_desc
     "Use direct alpha blending with desktop (doesn't work on windows)" )
if(WIN32)
  option( RenderUseDesktopBlend "${RenderUseDesktopBlend_desc}" false)
else()
  option( RenderUseDesktopBlend "${RenderUseDesktopBlend_desc}" true)
endif()

message(" * Direct alphablending with desktop: ${RenderUseDesktopBlend}")
if(RenderUseDesktopBlend)
  add_definitions(-DUSE_DESKTOP_BLEND)
endif()

# ------------------------------------------------------------------------------

#set interface files
set( INTERFACE_FILES
  ${LINKS_INCLUDE_DIR}/core.h
  ${LINKS_INCLUDE_DIR}/component.h
  ${LINKS_INCLUDE_DIR}/config.h
  ${LINKS_INCLUDE_DIR}/costanalysis.h
  ${LINKS_INCLUDE_DIR}/renderer.h
  ${LINKS_INCLUDE_DIR}/routing.h
  ${LINKS_INCLUDE_DIR}/transparencyanalysis.h
  ${LINKS_INCLUDE_DIR}/color_helpers.h
  ${LINKS_INCLUDE_DIR}/linkdescription.h
)

#set common files
set(COMMON_FILES  ${LINKS_INCLUDE_DIR}/common/plainlinkdescription.h
                  ${LINKS_INCLUDE_DIR}/common/hashedmap.h
                  ${LINKS_INCLUDE_DIR}/common/componentarguments.h
                  )

set(OpenGL_GL_PREFERENCE LEGACY)

if(WIN32)
  add_definitions(/wd4250 /wd4800 -DNOMINMAX -DWIN32_LEAN_AND_MEAN -D_USE_MATH_DEFINES)
endif(WIN32)

if(CMAKE_COMPILER_IS_GNUCXX)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fmessage-length=0 -Wall -O2 -g -std=c++0x")
endif()

add_subdirectory(${COMPONENTS_DIR}/tools)
include_directories(
  ${COMPONENTS_DIR}/tools
  libs/libqxt/include
)

add_subdirectory(libs/libqxt)

#TODO we should do that in a better way, so we can get the include directories and libs to link out of the subdirectory..
#add components
add_component(staticcore)
add_component(xmlconfig)
add_component(ipc_server)
add_component(costanalysis)
add_component(renderer_gl)
add_component(routing_cpu)
add_component(routing_cpu_dijkstra)
add_component(routing_dummy)
add_component(routing_gpu off)
#add_component(transparencyanalysis off)

#message("comp inc:\n${COMPONENTS_INC_DIR} \ncomp libs:\n${COMPONENTS_LIBS}\ndepeping files:\n${COMPONENTS_DEPENDING_FILES}")

# OpenCL GPU routing
if(LinksComponent_routing_gpu)
  add_definitions(-DUSE_GPU_ROUTING)
endif()

message("--------------------")

#add default applications
add_subdir_option(qtfullscreensystem QTFullscreenSystem)
add_subdir_option(qtsearch QtSearch FALSE)

#add interface files to the project
add_custom_target(InterfaceFiles SOURCES ${INTERFACE_FILES})
#add interface files to the project
add_custom_target(CommonFiles SOURCES ${COMMON_FILES})
