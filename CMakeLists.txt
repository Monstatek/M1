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
set(CMAKE_PROJECT_NAME MonstaTek_M1_v0802-ChrisUFO)

# Include toolchain file
include("cmake/gcc-arm-none-eabi.cmake")

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Core project settings
project(${CMAKE_PROJECT_NAME})
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

# +++Settings from STM32CubeIDE
#arm-none-eabi-size  MonstaTek_M1_v0800.elf 
#arm-none-eabi-objdump -h -S MonstaTek_M1_v0800.elf  > "MonstaTek_M1_v0800.list"
#arm-none-eabi-objcopy  -O ihex MonstaTek_M1_v0800.elf  "MonstaTek_M1_v0800.hex"
#arm-none-eabi-objcopy  -O binary MonstaTek_M1_v0800.elf  "MonstaTek_M1_v0800.bin"
# Specify the output ELF file
set(TARGET_ELF "${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.elf")
# Note: Optional post-build steps (size, objdump, md5) are handled by the build script

set(OBJCOPY_TOOL "${CMAKE_OBJCOPY}")
if(NOT IS_ABSOLUTE "${OBJCOPY_TOOL}")
    unset(OBJCOPY_TOOL)
    find_program(OBJCOPY_TOOL NAMES ${CMAKE_OBJCOPY} arm-none-eabi-objcopy)
endif()

if(OBJCOPY_TOOL)
    # Command to generate .bin file
    add_custom_command(
        COMMAND ${CMAKE_COMMAND} -E echo "Generating bin file ${CMAKE_PROJECT_NAME}.bin"
        TARGET ${CMAKE_PROJECT_NAME}
        POST_BUILD
        COMMAND ${OBJCOPY_TOOL}
    -O binary ${TARGET_ELF} ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
        #COMMENT "Generating .bin file"
    )

    # Command to append CRC32 to .bin file (required for STM32 firmware update)
    add_custom_command(
        COMMAND ${CMAKE_COMMAND} -E echo "Appending CRC32 to ${CMAKE_PROJECT_NAME}.bin"
        TARGET ${CMAKE_PROJECT_NAME}
        POST_BUILD
        COMMAND python ${CMAKE_SOURCE_DIR}/tools/append_crc32.py ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
    )

    # Command to generate .hex file
    add_custom_command(
        COMMAND ${CMAKE_COMMAND} -E echo "Generating hex file ${CMAKE_PROJECT_NAME}.hex"
        TARGET ${CMAKE_PROJECT_NAME}
        POST_BUILD
        COMMAND ${OBJCOPY_TOOL}
    -O ihex ${TARGET_ELF} ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.hex
        #COMMENT "Generating .hex file"
    )
else()
    message(WARNING "arm-none-eabi-objcopy not found; skipping .bin/.hex/CRC post-build steps")
endif()

# Optional: Command to display memory usage (only if size tool is available)
find_program(SIZE_TOOL arm-none-eabi-size)
if(SIZE_TOOL)
    add_custom_command(
        COMMAND ${CMAKE_COMMAND} -E echo "Memory usage:"
        TARGET ${CMAKE_PROJECT_NAME}
        POST_BUILD
        COMMAND ${SIZE_TOOL} ${TARGET_ELF}
    )
endif()

# Optional: Command to generate .list file (only if objdump is available)  
find_program(OBJDUMP_TOOL arm-none-eabi-objdump)
if(OBJDUMP_TOOL)
    add_custom_command(
        COMMAND ${CMAKE_COMMAND} -E echo "Generating list file ${CMAKE_PROJECT_NAME}.list"
        TARGET ${CMAKE_PROJECT_NAME}
        POST_BUILD
        COMMAND ${OBJDUMP_TOOL} -h -S ${TARGET_ELF} > ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.list
    )
endif()
