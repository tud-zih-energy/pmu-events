cmake_minimum_required(VERSION 3.11)
project(pmu-events VERSION 0.0.1)

if(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "x86_64")
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/pmu-events.c
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/jevents.py x86 all ${CMAKE_CURRENT_SOURCE_DIR}/arch ${CMAKE_CURRENT_BINARY_DIR}/pmu-events.c
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/jevents.py)
elseif(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "aarch64")
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/pmu-events.c
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/jevents.py arm64 all ${CMAKE_CURRENT_SOURCE_DIR}/arch ${CMAKE_CURRENT_BINARY_DIR}/pmu-events.c
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/jevents.py)
else()
    message(SEND_ERROR "Sorry, pmu-events is currently only available for x86_64 or aarch64!")
endif()

add_library(pmu-events ${CMAKE_CURRENT_BINARY_DIR}/pmu-events.c src/pmu-events.c)
target_include_directories(pmu-events PUBLIC include)

if(PROJECT_IS_TOP_LEVEL)
    add_executable(tests tests/test.c)
    target_link_libraries(tests pmu-events)

    enable_testing()
    add_test(NAME Tests COMMAND ./tests)
    
    add_executable(pmu-events-example examples/main.c)
    target_link_libraries(pmu-events-example pmu-events)
endif()

add_library(PMUEvents::pmu-events ALIAS pmu-events)
