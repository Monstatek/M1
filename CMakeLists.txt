# See COPYING.txt for license details.

cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME MonstaTek_M1_v0800)

# Include toolchain file
include("cmake/gcc-arm-none-eabi.cmake")

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

message("Build type: " ${CMAKE_BUILD_TYPE})

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/m1_01)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    m1_01

    # Add user defined libraries
)

# Specify the output ELF file
set(TARGET_ELF "${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.elf")
# Specify the output BIN file
set(TARGET_BIN "${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin")
# Command to display memory usage
add_custom_command(
    COMMAND ${CMAKE_COMMAND} -E echo "Memory usage:"    
    TARGET ${CMAKE_PROJECT_NAME}
    POST_BUILD
    COMMAND ${CMAKE_SIZE} ${TARGET_ELF}
    #COMMENT "Memory usage:"
)

# Command to generate .list file
add_custom_command(
    COMMAND ${CMAKE_COMMAND} -E echo "Generating list file ${CMAKE_PROJECT_NAME}.list"
    TARGET ${CMAKE_PROJECT_NAME}
    POST_BUILD
    COMMAND ${CMAKE_OBJDUMP}
-h -S ${TARGET_ELF} > ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.list
    #COMMENT "Generating .list file"
)

# Command to generate .bin file
add_custom_command(
    COMMAND ${CMAKE_COMMAND} -E echo "Generating bin file ${CMAKE_PROJECT_NAME}.bin"
    TARGET ${CMAKE_PROJECT_NAME}
    POST_BUILD
    COMMAND ${CMAKE_OBJCOPY}
-O binary ${TARGET_ELF} ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
    #COMMENT "Generating .bin file"
)

# Command to generate .hex file
add_custom_command(
    COMMAND ${CMAKE_COMMAND} -E echo "Generating hex file ${CMAKE_PROJECT_NAME}.hex"
    TARGET ${CMAKE_PROJECT_NAME}
    POST_BUILD
    COMMAND ${CMAKE_OBJCOPY}
-O ihex ${TARGET_ELF} ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.hex
    #COMMENT "Generating .hex file"
)

# Command to generate .bin file with CRC
add_custom_command(
    COMMAND ${CMAKE_COMMAND} -E echo "Generating bin file with CRC ${CMAKE_PROJECT_NAME}_wCRC.bin"
    TARGET ${CMAKE_PROJECT_NAME}
    POST_BUILD
    COMMAND srec_cat
${TARGET_BIN} -binary -crop 0x0 0x000FFC14 -STM32_Little_Endian 0x000FFC14 -o ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}_wCRC.bin -binary
    #COMMENT "Generating .bin file with CRC"
)