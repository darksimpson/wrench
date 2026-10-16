# ESP-IDF v5.0 or later. "driver" provides the UART and GPIO drivers on
# 5.0 to 5.2; from 5.3 on it is a deprecated umbrella component and
# "esp_driver_uart esp_driver_gpio" can replace it there.
idf_component_register(
	SRCS "main.cpp" "wrench.cpp"
	INCLUDE_DIRS "."
	REQUIRES driver esp_timer lwip esp_netif esp_wifi esp_event nvs_flash
)
