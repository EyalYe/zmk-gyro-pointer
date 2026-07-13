if(CONFIG_ZMK_MPU6050_INPUT)
  zephyr_library()
  zephyr_library_sources(src/mpu6050_input.c)
endif()
