set(CMAKE_CXX_STANDARD 20)

idf_component_register(
    SRC_DIRS "src" "src/modules" "src/services"
    INCLUDE_DIRS "src"
    PRIV_REQUIRES "esp_driver_gpio" "esp_driver_spi" "esp_timer"
)