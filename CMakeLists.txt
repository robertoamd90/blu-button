cmake_minimum_required(VERSION 3.22)

execute_process(
    COMMAND git describe --tags --always --dirty
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_DESC
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE GIT_RC
)

if(GIT_RC EQUAL 0 AND GIT_DESC)
    if(GIT_DESC MATCHES "^v?[0-9]+\\.[0-9]+\\.[0-9]+([.-].+)?$")
        string(REGEX REPLACE "^v" "" PROJECT_VER "${GIT_DESC}")
    else()
        set(PROJECT_VER "${GIT_DESC}")
    endif()
else()
    set(PROJECT_VER "unknown")
endif()

set(COMPONENTS
    main
    button_event
    board_config
    gpio_manager
    led_feedback
    device_identity
    ble_button_tx
    system_runtime
)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(BluButton)
