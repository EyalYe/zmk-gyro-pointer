if(CONFIG_ZMK_MPU6050_INPUT)
  zephyr_library()
  zephyr_library_sources(src/mpu6050_input.c)
  # ZMK app headers (event manager, keymap) for the layer-gated polling
  zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
endif()
