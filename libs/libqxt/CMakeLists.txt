# files
set( HEADER_FILES
  include/QxtGui/QxtWindowSystem
  include/QxtGui/qxtwindowsystem.h
#  include/QxtGui/QxtGlobalShortcut
#  include/QxtGui/qxtglobalshortcut.h
  include/qxtglobal.h
#  src/widgets/qxtglobalshortcut_p.h
)

set( SOURCE_FILES
  src/core/qxtglobal.cpp
#  src/widgets/qxtglobalshortcut.cpp
  src/widgets/qxtwindowsystem.cpp
)

set( USED_QT_MODULES
  Qt5::Network
  Qt5::Widgets
)

if(WIN32)
  set(SOURCE_FILES ${SOURCE_FILES} src/widgets/win/qxtwindowsystem_win.cpp)
elseif(APPLE)
  set(SOURCE_FILES ${SOURCE_FILES} src/widgets/x11/qxtwindowsystem_mac.cpp)
  set(HEADER_FILES ${HEADER_FILES} src/widgets/x11/qxtwindowsystem_mac.h)
else()
  set(SOURCE_FILES ${SOURCE_FILES} src/widgets/x11/qxtwindowsystem_x11.cpp)
#                                   src/widgets/x11/qxtglobalshortcut_x11.cpp )
  list(APPEND USED_QT_MODULES Qt5::X11Extras)
endif()

# ...
include_directories(
  include
  include/QxtGui
  src/core
  src/widgets
)
add_definitions(-DQT_NO_DEBUG -DQXT_STATIC)

ADD_LIBRARY(qxt STATIC
  ${SOURCE_FILES}
  ${HEADER_FILES}
)
target_link_libraries(qxt
  X11
  ${USED_QT_MODULES}
)