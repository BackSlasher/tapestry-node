cmake_minimum_required(VERSION 3.16)

# set(EXTRA_COMPONENT_DIRS "")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tapestry-node)

# Only create NVS partition if CSV file exists
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/nvs_config.csv)
    nvs_create_partition_image(nvs ${CMAKE_CURRENT_SOURCE_DIR}/nvs_config.csv FLASH_IN_PROJECT)
endif()
