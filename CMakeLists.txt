zephyr_library()

zephyr_library_sources(
    digital_output.c
    digital_input.c
    sensor.c
    nvs.c
    bt.c
    common_workqueue.c
    init.c
)

zephyr_library_include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../safe-c
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
