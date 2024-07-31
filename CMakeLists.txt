cmake_minimum_required(VERSION 3.5)

project(imnode-graph)

find_package(RapidJSON REQUIRED)

set(IMNODE_GRAPH_SOURCES
        imnode_graph.cpp
)

set(IMNODE_GRAPH_HEADERS
        imnode_graph.h
        imnode_graph_internal.h
        imnode_graph_math.h
)

add_library(imnode-graph STATIC
        ${IMNODE_GRAPH_SOURCES}
        ${IMNODE_GRAPH_HEADERS}
)

# DOXYGEN ==============================================================================================================
# https://vicrucann.github.io/tutorials/quick-cmake-doxygen/

find_package(Doxygen)

if(DOXYGEN_FOUND)
    get_filename_component(DOXYGEN_PROJECT_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
    set(DOXYGEN_CONFIG_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
    set(DOXYGEN_CONFIG_OUT ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile)

    configure_file(${DOXYGEN_CONFIG_IN} ${DOXYGEN_CONFIG_OUT} @ONLY)
    message("Doxygen Build Started.")

    if(WIN32)
        add_custom_target(imnode-graph-documentation ALL
                COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_CONFIG_OUT}
                COMMAND start firefox "${CMAKE_CURRENT_SOURCE_DIR}/Documentation/html/index.html"
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                COMMENT "Generating Doxygen Documentation"
                VERBATIM)
    else()
        add_custom_target(imnode-graph-documentation ALL
                COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_CONFIG_OUT}
                COMMAND firefox "${CMAKE_CURRENT_SOURCE_DIR}/Documentation/html/index.html"
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                COMMENT "Generating Doxygen Documentation"
                VERBATIM)
    endif()
else()
    message("Doxygen not found.")
endif()