cmake_minimum_required(VERSION 3.5)

# set(EXTRA_COMPONENT_DIRS "")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tapestry-node)

# Check if this is an OTA-only build
if(NOT DEFINED OTA_ONLY_BUILD)
    set(OTA_ONLY_BUILD OFF)
endif()

# Only create NVS partition for full builds
if(NOT OTA_ONLY_BUILD)
    nvs_create_partition_image(nvs ${CMAKE_CURRENT_SOURCE_DIR}/nvs_config.csv FLASH_IN_PROJECT)
endif()

# Custom target for OTA builds
add_custom_target(build-ota
    COMMAND ${CMAKE_COMMAND} -D OTA_ONLY_BUILD=ON -S ${CMAKE_SOURCE_DIR} -B ${CMAKE_BINARY_DIR}
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
    COMMENT "Building OTA-only firmware"
)
