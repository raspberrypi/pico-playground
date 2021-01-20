add_executable(stdio_pio)

pico_generate_pio_header(stdio_pio ${CMAKE_CURRENT_LIST_DIR}/uart_tx.pio)

target_sources(stdio_pio PRIVATE stdio_pio.c)

target_link_libraries(stdio_pio PRIVATE pico_stdlib hardware_pio)
pico_add_extra_outputs(stdio_pio)
