
set(COMPONENTROOT ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(
  ${LINKS_INCLUDE_DIR}
)
message("inc=${LINKS_INCLUDE_DIR}")

if(WIN32)
find_package(GLEW REQUIRED)
include_directories(
  ${GLEW_INCLUDE_DIR}
)
endif(WIN32)

set(HEADER_FILES
  fbo.h
)

set(SOURCE_FILES
  AnimatedPopup.cxx
  fbo.cxx
  HierarchicTileMap.cxx
  JSONParser.cxx
  JSON.cxx
  linkdescription.cpp
  NodeRenderer.cxx
  PartitionHelper.cxx
  qt_helper.cxx
  Rect.cxx
  routing.cxx
)

add_subdirectory(glsl)

add_definitions(-DNOMULTISAMPLING)
add_library(tools ${HEADER_FILES} ${SOURCE_FILES})
target_link_libraries(tools
  Qt5::Script
  Qt5::Gui
)