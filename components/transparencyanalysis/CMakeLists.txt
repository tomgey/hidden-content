

set(COMPONENTROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(COMPONENTSRC_DIR ${COMPONENTROOT}/src)
set(COMPONENTINC_DIR ${COMPONENTROOT}/include)
include_directories(${LINKS_INCLUDE_DIR} ${COMPONENTINC_DIR})

set(HEADER_FILES ${COMPONENTINC_DIR}/tanalysis.h
    )


set(SOURCE_FILES ${COMPONENTSRC_DIR}/tanalysis.cpp
    )

add_library(transparencyanalysis ${HEADER_FILES} ${SOURCE_FILES})
add_component_data(${COMPONENTINC_DIR} transparencyanalysis)
