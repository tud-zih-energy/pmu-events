cmake_minimum_required(VERSION 3.11)
project(pmu-events VERSION 0.0.1)

if(NOT ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "x86_64")
    message(SEND_ERROR "Sorry, pmu-events is currently only available for x86!")
endif()

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/pmu-events.c
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/jevents.py all all ${CMAKE_CURRENT_SOURCE_DIR}/arch ${CMAKE_CURRENT_BINARY_DIR}/pmu-events.c
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/jevents.py)


add_library(pmu-events ${CMAKE_CURRENT_BINARY_DIR}/pmu-events.c)
target_include_directories(pmu-events PUBLIC include)

if(PROJECT_IS_TOP_LEVEL)
    add_executable(example examples/main.c)
    target_link_libraries(example pmu-events)
endif()

add_library(PMUEvents::pmu-events ALIAS pmu-events)
