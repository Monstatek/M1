################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (12.3.rel1)
################################################################################

-include ../makefile.init

RM := rm -rf

# All of the sources participating in the build are defined here
-include sources.mk
-include m1_csrc/subdir.mk
-include lfrfid/subdir.mk
-include USB/Core/Src/subdir.mk
-include USB/Class/MSC/Src/subdir.mk
-include USB/Class/CompositeBuilder/Src/subdir.mk
-include USB/Class/CDC/Src/subdir.mk
-include Sub_Ghz/protocols/subdir.mk
-include Sub_Ghz/subdir.mk
-include NFC/NFC_drv/legacy/subdir.mk
-include NFC/NFC_drv/common/subdir.mk
-include NFC/Middlewares/ST/rfal/Src/subdir.mk
-include NFC/subdir.mk
-include Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/subdir.mk
-include Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM33_NTZ/non_secure/subdir.mk
-include Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/subdir.mk
-include Middlewares/Third_Party/FreeRTOS/Source/subdir.mk
-include Infrared/irmp-irsnd/subdir.mk
-include FatFs/R015/subdir.mk
-include Esp_spi_at/examples/at_spi_master/spi/stm32/main/subdir.mk
-include Esp_spi_at/esp-idf/components/esp_driver_spi/src/gpspi/subdir.mk
-include Esp32_serial_flasher/src/subdir.mk
-include Esp32_serial_flasher/port/subdir.mk
-include Esp32_serial_flasher/app/common/subdir.mk
-include Drivers/u8g2_csrc/subdir.mk
-include Drivers/STM32H5xx_HAL_Driver/Src/subdir.mk
-include Drivers/BSP/Components/ST25R3916/subdir.mk
-include Core/ThreadSafe/subdir.mk
-include Core/Startup/subdir.mk
-include Core/Src/subdir.mk
-include Battery/subdir.mk
-include objects.mk

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(S_DEPS)),)
-include $(S_DEPS)
endif
ifneq ($(strip $(S_UPPER_DEPS)),)
-include $(S_UPPER_DEPS)
endif
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
endif

-include ../makefile.defs

OPTIONAL_TOOL_DEPS := \
$(wildcard ../makefile.defs) \
$(wildcard ../makefile.init) \
$(wildcard ../makefile.targets) \


BUILD_ARTIFACT_NAME := MonstaTek_M1_v0800
BUILD_ARTIFACT_EXTENSION := elf
BUILD_ARTIFACT_PREFIX :=
BUILD_ARTIFACT := $(BUILD_ARTIFACT_PREFIX)$(BUILD_ARTIFACT_NAME)$(if $(BUILD_ARTIFACT_EXTENSION),.$(BUILD_ARTIFACT_EXTENSION),)

# Add inputs and outputs from these tool invocations to the build variables 
EXECUTABLES += \
MonstaTek_M1_v0800.elf \

MAP_FILES += \
MonstaTek_M1_v0800.map \

SIZE_OUTPUT += \
default.size.stdout \

OBJDUMP_LIST += \
MonstaTek_M1_v0800.list \

OBJCOPY_HEX += \
MonstaTek_M1_v0800.hex \

OBJCOPY_BIN += \
MonstaTek_M1_v0800.bin \


# All Target
all:
	+@$(MAKE) --no-print-directory main-build && $(MAKE) --no-print-directory post-build

# Main-build Target
main-build: MonstaTek_M1_v0800.elf secondary-outputs

# Tool invocations
MonstaTek_M1_v0800.elf MonstaTek_M1_v0800.map: $(OBJS) $(USER_OBJS) C:\Working\MONSTATEK\Firmware_Codes\Backup\v0_8_Feb_19_2026_OS\STM32H573VITX_FLASH.ld makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-gcc -o "MonstaTek_M1_v0800.elf" @"objects.list" $(USER_OBJS) $(LIBS) -mcpu=cortex-m33 -T"C:\Working\MONSTATEK\Firmware_Codes\Backup\v0_8_Feb_19_2026_OS\STM32H573VITX_FLASH.ld" --specs=nosys.specs -Wl,-Map="MonstaTek_M1_v0800.map" -Wl,--gc-sections -static --specs=nano.specs -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -Wl,--start-group -lc -lm -Wl,--end-group
	@echo 'Finished building target: $@'
	@echo ' '

default.size.stdout: $(EXECUTABLES) makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-size  $(EXECUTABLES)
	@echo 'Finished building: $@'
	@echo ' '

MonstaTek_M1_v0800.list: $(EXECUTABLES) makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-objdump -h -S $(EXECUTABLES) > "MonstaTek_M1_v0800.list"
	@echo 'Finished building: $@'
	@echo ' '

MonstaTek_M1_v0800.hex: $(EXECUTABLES) makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-objcopy  -O ihex $(EXECUTABLES) "MonstaTek_M1_v0800.hex"
	@echo 'Finished building: $@'
	@echo ' '

MonstaTek_M1_v0800.bin: $(EXECUTABLES) makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-objcopy  -O binary $(EXECUTABLES) "MonstaTek_M1_v0800.bin"
	@echo 'Finished building: $@'
	@echo ' '

# Other Targets
clean:
	-$(RM) MonstaTek_M1_v0800.bin MonstaTek_M1_v0800.elf MonstaTek_M1_v0800.hex MonstaTek_M1_v0800.list MonstaTek_M1_v0800.map default.size.stdout
	-@echo ' '

post-build:
	srec_cat $(OBJCOPY_BIN) -binary -crop 0x0 0x000FFC14 −STM32_Little_Endian 0x000FFC14 -o $(BUILD_ARTIFACT_NAME)_wCRC.bin -binary
	-@echo ' '

secondary-outputs: $(SIZE_OUTPUT) $(OBJDUMP_LIST) $(OBJCOPY_HEX) $(OBJCOPY_BIN)

fail-specified-linker-script-missing:
	@echo 'Error: Cannot find the specified linker script. Check the linker settings in the build configuration.'
	@exit 2

warn-no-linker-script-specified:
	@echo 'Warning: No linker script specified. Check the linker settings in the build configuration.'

.PHONY: all clean dependents main-build fail-specified-linker-script-missing warn-no-linker-script-specified post-build

-include ../makefile.targets
