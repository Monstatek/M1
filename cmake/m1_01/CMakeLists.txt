# See COPYING.txt for license details.

cmake_minimum_required(VERSION 3.22)

project(m1_01)
add_library(m1_01 INTERFACE)

# Enable CMake support for ASM and C languages
enable_language(C ASM)

target_compile_definitions(m1_01 INTERFACE
	USE_HAL_DRIVER
	STM32H573xx
    STM32_THREAD_SAFE_STRATEGY=4
    $<$<CONFIG:Debug>:DEBUG>
)

# --- MIFARE Classic raw card-emulation build toggles (Scope B) --------------
# M1_MFC_RAW_EMULATION and M1_MFC_DMA_AT are the ONE supported production
# configuration -- both ON, always. There is no supported OFF/generic-
# ISO14443A-only firmware variant: a build that cannot emulate MIFARE
# Classic is not a legitimate release candidate, so configuration FAILS
# below rather than silently producing one. Both default to ON (a bare
# `cmake ..` with no -D flags already configures the one supported build);
# the option() cache entries exist only so an explicit override is possible
# to TEST -- and is then immediately refused by the guard beneath this
# block, not honored.
option(M1_MFC_RAW_EMULATION "Master gate for all MFC raw-emulation code (must stay ON -- see the guard below)" ON)
option(M1_MFC_DMA_AT "{At} via TIM7 + dual-GPDMA1 transport (must stay ON -- see the guard below)" ON)

foreach(_m1_mfc_flag
        M1_MFC_RAW_EMULATION   # master gate for all raw-emulation code
        M1_MFC_DMA_AT          # {At} via TIM7 + dual-GPDMA1 transport
        MFC_ROUTE_PROBE        # unconditional RF-path probe (no crypto/cache)
        M1_MFC_AT_RX_DIAG)     # reader-side [AT-RX] capture diagnostic
    if(${_m1_mfc_flag})
        target_compile_definitions(m1_01 INTERFACE ${_m1_mfc_flag})
    endif()
endforeach()

# Configure-time regression guard, unconditional: EITHER flag left off
# (explicitly, or by whatever default a future edit might introduce) fails
# configuration outright -- one guard, not two, so there is a single place
# that defines what "supported configuration" means. Without a production
# {At} transmitter, MIFARE Classic AUTH is detected and dispatched but the
# device can never answer it (nfc_listener.c's {At} TX site has no fallback
# branch when M1_MFC_DMA_AT is not defined; the TIM7 + dual-GPDMA1 transport
# is the SOLE production {At} transport -- the former bit-bang concept-proof
# alternative has been physically removed) -- and without RAW_EMULATION
# itself, the shipped firmware
# cannot emulate MIFARE Classic at all, which this project no longer treats
# as a legitimate release variant.
if(NOT M1_MFC_RAW_EMULATION OR NOT M1_MFC_DMA_AT)
    message(FATAL_ERROR
        "M1_MFC_RAW_EMULATION=ON and M1_MFC_DMA_AT=ON are REQUIRED -- there "
        "is exactly one supported production configuration and generic "
        "ISO14443A-only firmware (MFC raw emulation and/or its {At} "
        "transmitter disabled) is not a legitimate build. Do not pass "
        "-DM1_MFC_RAW_EMULATION=OFF or -DM1_MFC_DMA_AT=OFF; both already "
        "default to ON.")
endif()

# --- LF RFID PSK acquisition architecture + Honeywell NexWatch ----------
# Keri and Honeywell/NexWatch read/emulate/write are intended production
# functionality, so a fresh development/release configure must include the
# ASK<->PSK dual-pass acquisition path and the NexWatch protocol without
# relying on remembered command-line flags.
option(LFRFID_PSK_PASS_ENABLED "Enable ASK<->PSK dual-pass LF acquisition" ON)
option(LFRFID_NEXWATCH_ENABLED "Enable Honeywell NexWatch LF protocol" ON)

if(LFRFID_NEXWATCH_ENABLED AND NOT LFRFID_PSK_PASS_ENABLED)
    message(FATAL_ERROR
        "LFRFID_NEXWATCH_ENABLED requires LFRFID_PSK_PASS_ENABLED; "
        "NexWatch must not be built without its PSK acquisition path.")
endif()

foreach(_lfrfid_flag
        LFRFID_PSK_PASS_ENABLED
        LFRFID_NEXWATCH_ENABLED)
    if(${_lfrfid_flag})
        target_compile_definitions(m1_01 INTERFACE ${_lfrfid_flag})
    endif()
endforeach()

# Build identity for the emulation [BUILD] banner. Pass the exact (already-
# committed) short SHA + clean flag at configure time, e.g.:
#   -DM1_GIT_COMMIT=<sha> -DM1_GIT_DIRTY=0
# Only forwarded when provided, so the OFF/normal build is unaffected.
if(DEFINED M1_GIT_COMMIT)
    target_compile_definitions(m1_01 INTERFACE M1_GIT_COMMIT="${M1_GIT_COMMIT}")
endif()
if(DEFINED M1_GIT_DIRTY)
    target_compile_definitions(m1_01 INTERFACE M1_GIT_DIRTY=${M1_GIT_DIRTY})
endif()

target_include_directories(m1_01 INTERFACE
    ../../Core/Inc
    ../../Drivers/STM32H5xx_HAL_Driver/Inc
    ../../Drivers/STM32H5xx_HAL_Driver/Inc/Legacy
    ../../Drivers/CMSIS/Device/ST/STM32H5xx/Include
    ../../Drivers/CMSIS/Include
    ../../Core/ThreadSafe
    ../../Middlewares/Third_Party/FreeRTOS/Source/include/
    ../../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM33_NTZ/non_secure/
    ../../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/
    ../../Middlewares/Third_Party/CMSIS/RTOS2/Include/
    ../../Drivers/u8g2_csrc
    ../../Drivers/u8g2_cppsrc
    ../../m1_csrc
    ../../Infrared    
    ../../Infrared/irmp-irsnd
    ../../FatFs/R015
    ../../Sub_Ghz
    ../../Sub_Ghz/protocols
    ../../Sub_Ghz/modules
    ../../Esp32_serial_flasher/include
    ../../Esp32_serial_flasher/port
    ../../Esp32_serial_flasher/app/common
    ../../Esp32_serial_flasher/private_include
    # Tracked, minimal stand-in for ESP-IDF's generated sdkconfig.h (see
    # m1_csrc/esp_stub/sdkconfig.h). Replaces the former dependency on the
    # git-ignored, generated ../../Esp_spi_at/build/include/ directory so a
    # clean checkout configures/builds without any Esp_spi_at/build/ present.
    ../../m1_csrc/esp_stub
    ../../Esp_spi_at/esp-idf/components/esp_common/include
    ../../Esp_spi_at/esp-idf/components/esp_driver_spi/include/driver
    ../../Esp_spi_at/esp-idf/components/hal/include/hal
    ../../Esp_spi_at/esp-idf/components/log/include/hal
    ../../Esp_spi_at/esp-idf/components/soc/esp32c6/include/soc
    ../../Esp_spi_at/esp-idf/components/soc/esp32c6/register/soc
    ../../Esp_spi_at/examples/at_spi_master/spi/stm32/main
    ../../Drivers/BSP/Components/ST25R3916/
    ../../NFC
    ../../NFC/Middlewares/ST/rfal/Inc
    ../../NFC/Middlewares/ST/rfal/Src/
    ../../NFC/NFC_drv
    ../../NFC/NFC_drv/common
    ../../NFC/NFC_drv/legacy
    ../../Battery
    ../../lfrfid
    ../../USB
    ../../USB/Class/CDC/Inc
    ../../USB/Class/CDC/Scr
    ../../USB/Class/CompositeBuilder/Inc
    ../../USB/Class/CompositeBuilder/Src
    ../../USB/Class/MSC/Inc
    ../../USB/Class/MSC/Src
    ../../USB/Class/HID/Inc
    ../../USB/Core/Inc
    ../../USB/Core/Scr
)

target_sources(m1_01 INTERFACE
    ../../Battery/battery.c
    ../../Battery/soh_filter.c
    ../../Core/Src/FreeRTOS_CLI.c
    ../../Core/Src/app_freertos.c
    ../../Core/Src/cli_app.c
    ../../Core/Src/main.c
    ../../Core/Src/stm32h5xx_hal_msp.c
    ../../Core/Src/stm32h5xx_hal_timebase_tim.c
    ../../Core/Src/stm32h5xx_it.c
    ../../Core/Src/syscalls.c
    ../../Core/Src/sysmem.c
    ../../Core/Src/system_stm32h5xx.c
    ../../Core/Startup/startup_stm32h573vitx.s
    #../../Core/ThreadSafe/newlib_lock_glue.c
    ../../Drivers/BSP/Components/ST25R3916/rfal_rfst25r3916.c
    ../../Drivers/BSP/Components/ST25R3916/st25r3916.c
    ../../Drivers/BSP/Components/ST25R3916/st25r3916_aat.c
    ../../Drivers/BSP/Components/ST25R3916/st25r3916_com.c
    ../../Drivers/BSP/Components/ST25R3916/st25r3916_irq.c
    ../../Drivers/BSP/Components/ST25R3916/st25r3916_led.c
    ../../Drivers/BSP/Components/ST25R3916/timer.c
    ../../Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal.c
    ../../Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_cortex.c
    ../../Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_crc.c
    ../../Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_crc_ex.c
    ../../Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_dma.c
    ../../Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_dma_ex.c
    ../../Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_exti.c
    ../../Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_flash.c
    ../../Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_flash_ex.c
    ../../Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_gpio.c
    ../../Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_hcd.c
    ../../Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_i2c.c
    ../../Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_i2c_ex.c
    ../../Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_icache.c
    ../../Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_iwdg.c
    ../../Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_pcd.c
    ../../Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_pcd_ex.c
    ../../Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_pwr.c
    ../../Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_pwr_ex.c
    ../../Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_rcc.c
    ../../Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_rcc_ex.c
    ../../Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_rtc.c
    ../../Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_rtc_ex.c
    ../../Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_sd.c
    ../../Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_sd_ex.c
    ../../Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_spi.c
    ../../Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_spi_ex.c
    ../../Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_tim.c
    ../../Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_tim_ex.c
    ../../Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_uart.c
    ../../Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_uart_ex.c
    ../../Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_ll_dlyb.c
    ../../Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_ll_sdmmc.c
    ../../Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_ll_usb.c
    ../../Drivers/u8g2_csrc/mui.c
    ../../Drivers/u8g2_csrc/mui_u8g2.c
    ../../Drivers/u8g2_csrc/u8g2_arc.c
    ../../Drivers/u8g2_csrc/u8g2_bitmap.c
    ../../Drivers/u8g2_csrc/u8g2_box.c
    ../../Drivers/u8g2_csrc/u8g2_buffer.c
    ../../Drivers/u8g2_csrc/u8g2_button.c
    ../../Drivers/u8g2_csrc/u8g2_circle.c
    ../../Drivers/u8g2_csrc/u8g2_cleardisplay.c
    ../../Drivers/u8g2_csrc/u8g2_d_memory.c
    ../../Drivers/u8g2_csrc/u8g2_d_setup.c
    ../../Drivers/u8g2_csrc/u8g2_font.c
    # u8g2_fonts.c (the full ~1987-font upstream catalog, ~38 MB source,
    # ~58 s to compile alone) is intentionally NOT built. Only the 13 fonts
    # this firmware actually links are compiled, via a byte-exact generated
    # extraction -- see Drivers/u8g2_csrc/tools/extract_selected_fonts.py
    # for exactly how, why, and the verification (byte-for-byte identical
    # compiled font data vs. the full catalog, confirmed via objcopy) this
    # is safe. u8g2_fonts.c itself is untouched in the tree for provenance
    # and as the source for adding a new font later.
    ../../Drivers/u8g2_csrc/u8g2_fonts_selected.c
    ../../Drivers/u8g2_csrc/u8g2_hvline.c
    ../../Drivers/u8g2_csrc/u8g2_input_value.c
    ../../Drivers/u8g2_csrc/u8g2_intersection.c
    ../../Drivers/u8g2_csrc/u8g2_kerning.c
    ../../Drivers/u8g2_csrc/u8g2_line.c
    ../../Drivers/u8g2_csrc/u8g2_ll_hvline.c
    ../../Drivers/u8g2_csrc/u8g2_message.c
    ../../Drivers/u8g2_csrc/u8g2_polygon.c
    ../../Drivers/u8g2_csrc/u8g2_selection_list.c
    ../../Drivers/u8g2_csrc/u8g2_setup.c
    ../../Drivers/u8g2_csrc/u8log.c
    ../../Drivers/u8g2_csrc/u8log_u8g2.c
    ../../Drivers/u8g2_csrc/u8log_u8x8.c
    ../../Drivers/u8g2_csrc/u8x8_8x8.c
    ../../Drivers/u8g2_csrc/u8x8_byte.c
    ../../Drivers/u8g2_csrc/u8x8_cad.c
    ../../Drivers/u8g2_csrc/u8x8_capture.c
    ../../Drivers/u8g2_csrc/u8x8_d_a2printer.c
    ../../Drivers/u8g2_csrc/u8x8_d_ch1120.c
    ../../Drivers/u8g2_csrc/u8x8_d_gp1247ai.c
    ../../Drivers/u8g2_csrc/u8x8_d_gp1287ai.c
    ../../Drivers/u8g2_csrc/u8x8_d_gp1294ai.c
    ../../Drivers/u8g2_csrc/u8x8_d_gu800.c
    ../../Drivers/u8g2_csrc/u8x8_d_hd44102.c
    ../../Drivers/u8g2_csrc/u8x8_d_il3820_296x128.c
    ../../Drivers/u8g2_csrc/u8x8_d_ist3020.c
    ../../Drivers/u8g2_csrc/u8x8_d_ist3088.c
    ../../Drivers/u8g2_csrc/u8x8_d_ist7920.c
    ../../Drivers/u8g2_csrc/u8x8_d_ks0108.c
    ../../Drivers/u8g2_csrc/u8x8_d_lc7981.c
    ../../Drivers/u8g2_csrc/u8x8_d_ld7032_60x32.c
    ../../Drivers/u8g2_csrc/u8x8_d_ls013b7dh03.c
    ../../Drivers/u8g2_csrc/u8x8_d_max7219.c
    ../../Drivers/u8g2_csrc/u8x8_d_pcd8544_84x48.c
    ../../Drivers/u8g2_csrc/u8x8_d_pcf8812.c
    ../../Drivers/u8g2_csrc/u8x8_d_pcf8814_hx1230.c
    ../../Drivers/u8g2_csrc/u8x8_d_s1d15300.c
    ../../Drivers/u8g2_csrc/u8x8_d_s1d15721.c
    ../../Drivers/u8g2_csrc/u8x8_d_s1d15e06.c
    ../../Drivers/u8g2_csrc/u8x8_d_sbn1661.c
    ../../Drivers/u8g2_csrc/u8x8_d_sed1330.c
    ../../Drivers/u8g2_csrc/u8x8_d_sh1106_64x32.c
    ../../Drivers/u8g2_csrc/u8x8_d_sh1106_72x40.c
    ../../Drivers/u8g2_csrc/u8x8_d_sh1107.c
    ../../Drivers/u8g2_csrc/u8x8_d_sh1108.c
    ../../Drivers/u8g2_csrc/u8x8_d_sh1122.c
    ../../Drivers/u8g2_csrc/u8x8_d_ssd1305.c
    ../../Drivers/u8g2_csrc/u8x8_d_ssd1306_128x32.c
    ../../Drivers/u8g2_csrc/u8x8_d_ssd1306_128x64_noname.c
    ../../Drivers/u8g2_csrc/u8x8_d_ssd1306_2040x16.c
    ../../Drivers/u8g2_csrc/u8x8_d_ssd1306_48x64.c
    ../../Drivers/u8g2_csrc/u8x8_d_ssd1306_64x32.c
    ../../Drivers/u8g2_csrc/u8x8_d_ssd1306_64x48.c
    ../../Drivers/u8g2_csrc/u8x8_d_ssd1306_72x40.c
    ../../Drivers/u8g2_csrc/u8x8_d_ssd1306_96x16.c
    ../../Drivers/u8g2_csrc/u8x8_d_ssd1306_96x40.c
    ../../Drivers/u8g2_csrc/u8x8_d_ssd1309.c
    ../../Drivers/u8g2_csrc/u8x8_d_ssd1312.c
    ../../Drivers/u8g2_csrc/u8x8_d_ssd1316.c
    ../../Drivers/u8g2_csrc/u8x8_d_ssd1317.c
    ../../Drivers/u8g2_csrc/u8x8_d_ssd1318.c
    ../../Drivers/u8g2_csrc/u8x8_d_ssd1320.c
    ../../Drivers/u8g2_csrc/u8x8_d_ssd1322.c
    ../../Drivers/u8g2_csrc/u8x8_d_ssd1325.c
    ../../Drivers/u8g2_csrc/u8x8_d_ssd1326.c
    ../../Drivers/u8g2_csrc/u8x8_d_ssd1327.c
    ../../Drivers/u8g2_csrc/u8x8_d_ssd1329.c
    ../../Drivers/u8g2_csrc/u8x8_d_ssd1362.c
    ../../Drivers/u8g2_csrc/u8x8_d_ssd1363.c
    ../../Drivers/u8g2_csrc/u8x8_d_ssd1606_172x72.c
    ../../Drivers/u8g2_csrc/u8x8_d_ssd1607_200x200.c
    ../../Drivers/u8g2_csrc/u8x8_d_st7302.c
    ../../Drivers/u8g2_csrc/u8x8_d_st7511.c
    ../../Drivers/u8g2_csrc/u8x8_d_st75160.c
    ../../Drivers/u8g2_csrc/u8x8_d_st75161.c
    ../../Drivers/u8g2_csrc/u8x8_d_st75256.c
    ../../Drivers/u8g2_csrc/u8x8_d_st7528.c
    ../../Drivers/u8g2_csrc/u8x8_d_st75320.c
    ../../Drivers/u8g2_csrc/u8x8_d_st7539.c
    ../../Drivers/u8g2_csrc/u8x8_d_st7565.c
    ../../Drivers/u8g2_csrc/u8x8_d_st7567.c
    ../../Drivers/u8g2_csrc/u8x8_d_st7571.c
    ../../Drivers/u8g2_csrc/u8x8_d_st7586s_erc240160.c
    ../../Drivers/u8g2_csrc/u8x8_d_st7586s_jlx320160.c
    ../../Drivers/u8g2_csrc/u8x8_d_st7586s_jlx384160.c
    ../../Drivers/u8g2_csrc/u8x8_d_st7586s_md240128.c
    ../../Drivers/u8g2_csrc/u8x8_d_st7586s_s028hn118a.c
    ../../Drivers/u8g2_csrc/u8x8_d_st7586s_ymc240160.c
    ../../Drivers/u8g2_csrc/u8x8_d_st7588.c
    ../../Drivers/u8g2_csrc/u8x8_d_st7920.c
    ../../Drivers/u8g2_csrc/u8x8_d_stdio.c
    ../../Drivers/u8g2_csrc/u8x8_d_t6963.c
    ../../Drivers/u8g2_csrc/u8x8_d_uc1601.c
    ../../Drivers/u8g2_csrc/u8x8_d_uc1604.c
    ../../Drivers/u8g2_csrc/u8x8_d_uc1608.c
    ../../Drivers/u8g2_csrc/u8x8_d_uc1609.c
    ../../Drivers/u8g2_csrc/u8x8_d_uc1610.c
    ../../Drivers/u8g2_csrc/u8x8_d_uc1611.c
    ../../Drivers/u8g2_csrc/u8x8_d_uc1617.c
    ../../Drivers/u8g2_csrc/u8x8_d_uc1628.c
    ../../Drivers/u8g2_csrc/u8x8_d_uc1638.c
    ../../Drivers/u8g2_csrc/u8x8_d_uc1701_dogs102.c
    ../../Drivers/u8g2_csrc/u8x8_d_uc1701_mini12864.c
    ../../Drivers/u8g2_csrc/u8x8_debounce.c
    ../../Drivers/u8g2_csrc/u8x8_display.c
    ../../Drivers/u8g2_csrc/u8x8_gpio.c
    ../../Drivers/u8g2_csrc/u8x8_input_value.c
    ../../Drivers/u8g2_csrc/u8x8_message.c
    ../../Drivers/u8g2_csrc/u8x8_selection_list.c
    ../../Drivers/u8g2_csrc/u8x8_setup.c
    ../../Drivers/u8g2_csrc/u8x8_string.c
    ../../Drivers/u8g2_csrc/u8x8_u16toa.c
    ../../Drivers/u8g2_csrc/u8x8_u8toa.c
    ../../Esp32_serial_flasher/app/common/app_common.c
    ../../Esp32_serial_flasher/port/stm32_port.c
    ../../Esp32_serial_flasher/src/esp_loader.c
    ../../Esp32_serial_flasher/src/esp_stubs.c
    ../../Esp32_serial_flasher/src/esp_targets.c
    ../../Esp32_serial_flasher/src/md5_hash.c
    ../../Esp32_serial_flasher/src/protocol_serial.c
    ../../Esp32_serial_flasher/src/protocol_uart.c
    ../../Esp32_serial_flasher/src/slip.c
    ../../Esp_spi_at/esp-idf/components/esp_driver_spi/src/gpspi/spi_master.c
    ../../Esp_spi_at/examples/at_spi_master/spi/stm32/main/esp_app_main.c
    ../../Esp_spi_at/examples/at_spi_master/spi/stm32/main/esp_queue.c
    ../../Esp_spi_at/examples/at_spi_master/spi/stm32/main/m1_at_response_parser.c
    ../../FatFs/R015/diskio.c
    ../../FatFs/R015/ff.c
    ../../FatFs/R015/ff_gen_drv.c
    ../../FatFs/R015/ffsystem.c
    ../../FatFs/R015/ffsystem_baremetal.c
    ../../FatFs/R015/ffsystem_cmsis_os.c
    ../../FatFs/R015/ffunicode.c
    ../../Infrared/m1_ir_remotes.c
    ../../Infrared/m1_ir_signals.c    
    ../../Infrared/irmp-irsnd/irmp.c
    ../../Infrared/irmp-irsnd/irmpextlog.c
    ../../Infrared/irmp-irsnd/irsnd.c
    ../../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/cmsis_os2.c
    ../../Middlewares/Third_Party/FreeRTOS/Source/croutine.c
    ../../Middlewares/Third_Party/FreeRTOS/Source/event_groups.c
    ../../Middlewares/Third_Party/FreeRTOS/Source/list.c
    ../../Middlewares/Third_Party/FreeRTOS/Source/queue.c
    ../../Middlewares/Third_Party/FreeRTOS/Source/stream_buffer.c
    ../../Middlewares/Third_Party/FreeRTOS/Source/tasks.c
    ../../Middlewares/Third_Party/FreeRTOS/Source/timers.c
    ../../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM33_NTZ/non_secure/port.c
    ../../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM33_NTZ/non_secure/portasm.c
    ../../Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_4.c
    ../../NFC/Middlewares/ST/rfal/Src/rfal_analogConfig.c
    ../../NFC/Middlewares/ST/rfal/Src/rfal_cd.c
    ../../NFC/Middlewares/ST/rfal/Src/rfal_crc.c
    ../../NFC/Middlewares/ST/rfal/Src/rfal_dpo.c
    ../../NFC/Middlewares/ST/rfal/Src/rfal_iso15693_2.c
    ../../NFC/Middlewares/ST/rfal/Src/rfal_isoDep.c
    ../../NFC/Middlewares/ST/rfal/Src/rfal_nfc.c
    ../../NFC/Middlewares/ST/rfal/Src/rfal_nfcDep.c
    ../../NFC/Middlewares/ST/rfal/Src/rfal_nfca.c
    ../../NFC/Middlewares/ST/rfal/Src/rfal_nfcb.c
    ../../NFC/Middlewares/ST/rfal/Src/rfal_nfcf.c
    ../../NFC/Middlewares/ST/rfal/Src/rfal_nfcv.c
    ../../NFC/Middlewares/ST/rfal/Src/rfal_st25tb.c
    ../../NFC/Middlewares/ST/rfal/Src/rfal_st25xv.c
    ../../NFC/Middlewares/ST/rfal/Src/rfal_t1t.c
    ../../NFC/Middlewares/ST/rfal/Src/rfal_t2t.c
    ../../NFC/Middlewares/ST/rfal/Src/rfal_t4t.c
    ../../NFC/NFC_drv/common/nfc_ctx.c
    ../../NFC/NFC_drv/common/mf_desfire_parse.c
    ../../NFC/NFC_drv/common/nfc_transit_clipper.c
    ../../NFC/NFC_drv/common/crypto1.c
    ../../NFC/NFC_drv/common/crypto1_recover.c
    ../../NFC/NFC_drv/common/mfc_harvest.c
    ../../NFC/NFC_drv/common/mfc_harvest_storage.c
    ../../NFC/NFC_drv/common/mfc_dict_solver.c
    ../../NFC/NFC_drv/common/mfc_access.c
    ../../NFC/NFC_drv/common/nfc_add_image.c
    ../../NFC/NFC_drv/common/ce_mfc_xport.c
    ../../NFC/NFC_drv/common/m1_nfc_raw_hal.c
    ../../NFC/NFC_drv/common/m1_mfc_raw_listener.c
    ../../NFC/NFC_drv/common/m1_mfc_session.c
    ../../NFC/NFC_drv/common/m1_mfc_raw_session_hw.c
    ../../NFC/NFC_drv/common/m1_mfc_emu_image.c
    ../../NFC/NFC_drv/common/m1_t2t_emu_image.c
    ../../NFC/NFC_drv/common/m1_mfc_fast.c
    ../../NFC/NFC_drv/common/m1_at_wave.c
    ../../NFC/NFC_drv/common/m1_at_edges.c
    ../../NFC/NFC_drv/common/m1_mfc_dma.c
    ../../NFC/NFC_drv/common/ce_mfc.c
    ../../NFC/NFC_drv/common/mfc_capture.c
    ../../NFC/NFC_drv/common/mfc_key_source.c
    ../../NFC/NFC_drv/common/nfc_dict_line.c
    ../../NFC/NFC_drv/common/mfc_key_source_sd.c
    ../../NFC/NFC_drv/common/mfc_result.c
    ../../NFC/NFC_drv/common/mfc_identity.c
    ../../NFC/NFC_drv/common/mfc_dict_resume.c
    ../../NFC/NFC_drv/common/mfc_dict_phase.c
    ../../NFC/NFC_drv/common/nfc_hex_utils.c
    ../../NFC/NFC_drv/common/mfc_block_render.c
    ../../NFC/NFC_drv/common/mfc_keys.c
    ../../NFC/NFC_drv/common/ulc_keys.c
    ../../NFC/NFC_drv/common/ntag_pwd_keys.c
    ../../NFC/NFC_drv/common/nfc_file.c
    ../../NFC/NFC_drv/common/nfc_fileio.c
    ../../NFC/NFC_drv/common/nfc_storage.c
    ../../NFC/NFC_drv/legacy/nfc_driver.c
    ../../NFC/NFC_drv/legacy/nfc_listener.c
    ../../NFC/NFC_drv/legacy/m1_t2t_transport.c
    ../../NFC/NFC_drv/legacy/mfc_detect.c
    ../../NFC/NFC_drv/legacy/m1_desfire.c
    ../../NFC/NFC_drv/legacy/nfc_poller.c
    ../../NFC/app_x-cube-nfcx.c
    ../../Sub_Ghz/datatypes_utils.c
    ../../Sub_Ghz/m1_sub_ghz_api.c
    ../../Sub_Ghz/m1_sub_ghz_decenc.c
    ../../Sub_Ghz/protocols/m1_princeton_decode.c
    ../../Sub_Ghz/protocols/m1_subghz_pwm_decode.c
    ../../Sub_Ghz/modules/subghz_mod_linear.c
    ../../Sub_Ghz/modules/subghz_mod_fixedcode.c
    ../../Sub_Ghz/modules/subghz_modules.c
    ../../Sub_Ghz/protocols/m1_secplus_v2_decode.c
    ../../USB/Class/CDC/Src/usbd_cdc.c
    ../../USB/Class/CDC/Src/usbd_cdc_if.c
    ../../USB/Class/CompositeBuilder/Src/usbd_composite_builder.c
    ../../USB/Class/MSC/Src/usbd_msc.c
    ../../USB/Class/MSC/Src/usbd_msc_bot.c
    ../../USB/Class/MSC/Src/usbd_msc_data.c
    ../../USB/Class/MSC/Src/usbd_msc_scsi.c
    ../../USB/Class/MSC/Src/usbd_msc_storage.c
    ../../USB/Class/HID/Src/usbd_hid_m1.c
    ../../USB/Core/Src/usbd_conf.c
    ../../USB/Core/Src/usbd_core.c
    ../../USB/Core/Src/usbd_ctlreq.c
    ../../USB/Core/Src/usbd_desc.c
    ../../USB/Core/Src/usbd_ioreq.c
    ../../lfrfid/lfrfid.c
    ../../lfrfid/lfrfid_dma_tx.c
    ../../lfrfid/lfrfid_psk1_wave.c
    ../../lfrfid/lfrfid_file.c
    ../../lfrfid/lfrfid_hal.c
    ../../lfrfid/lfrfid_protocol.c
    ../../lfrfid/lfrfid_protocol_em4100.c
    ../../lfrfid/lfrfid_protocol_h10301.c
    ../../lfrfid/lfrfid_protocol_pyramid.c
    ../../lfrfid/lfrfid_protocol_ioprox.c
    ../../lfrfid/lfrfid_protocol_awid.c
    ../../lfrfid/lfrfid_protocol_securakey.c
    ../../lfrfid/lfrfid_protocol_jablotron.c
    ../../lfrfid/lfrfid_protocol_fdx_b.c
    ../../lfrfid/lfrfid_protocol_hid_generic.c
    ../../lfrfid/lfrfid_protocol_hid_ex_generic.c
    ../../lfrfid/lfrfid_protocol_keri.c
    ../../lfrfid/lfrfid_protocol_nexwatch.c
    ../../lfrfid/t5577.c
    ../../m1_csrc/res_string.c
    ../../m1_csrc/bit_util.c
    ../../m1_csrc/logger.c
    ../../m1_csrc/m1_bq25896.c
    ../../m1_csrc/m1_bq27421.c
    ../../m1_csrc/m1_bq27421_boot_guard.c
    ../../m1_csrc/m1_bt.c
    ../../m1_csrc/m1_buzzer.c
    ../../m1_csrc/m1_cli.c
    ../../m1_csrc/m1_cli_help.c
    ../../m1_csrc/m1_core_config.c
    ../../m1_csrc/m1_capture_frame.c
    ../../m1_csrc/m1_capture_link.c
    ../../m1_csrc/m1_capture_pcap.c
    ../../m1_csrc/m1_capture_scan_parse.c
    ../../m1_csrc/m1_capture_transport.c
    ../../m1_csrc/m1_cobs.c
    ../../m1_csrc/m1_crc32c.c
    ../../m1_csrc/m1_display.c
    ../../m1_csrc/m1_display_data.c
    ../../m1_csrc/m1_esp32_flash_session.c
    ../../m1_csrc/m1_esp32_perf.c
    ../../m1_csrc/m1_esp32_fw_update.c
    ../../m1_csrc/m1_esp32_hal.c
    ../../m1_csrc/m1_esp_hosted_dummy.c
    ../../m1_csrc/m1_fault_report.c
    ../../m1_csrc/m1_feedback_hw_adapter.c
    ../../m1_csrc/m1_feedback_manager.c
    ../../m1_csrc/m1_feedback_orchestration.c
    ../../m1_csrc/m1_feedback_sequences.c
    ../../m1_csrc/m1_feedback_task.c
    ../../m1_csrc/m1_file_browser.c
    ../../m1_csrc/m1_file_util.c
    ../../m1_csrc/m1_save_filename.c
    ../../m1_csrc/m1_fusb302.c
    ../../m1_csrc/m1_fw_update.c
    ../../m1_csrc/m1_fw_update_bl.c
    ../../m1_csrc/m1_gpio.c
    ../../m1_csrc/m1_i2c.c
    ../../m1_csrc/m1_infrared.c
    ../../m1_csrc/m1_int_hdl.c
    ../../m1_csrc/m1_isr_drop_counters.c
    ../../m1_csrc/m1_lcd.c
    ../../m1_csrc/m1_led_indicator.c
    ../../m1_csrc/m1_led_priority.c
    ../../m1_csrc/m1_lib.c
    ../../m1_csrc/m1_log_debug.c
    ../../m1_csrc/m1_low_power.c
    ../../m1_csrc/m1_lp5814.c
    ../../m1_csrc/m1_md5_hash.c
    ../../m1_csrc/m1_menu.c
    ../../m1_csrc/m1_nfc.c
    ../../m1_csrc/m1_pcapng.c
    ../../m1_csrc/m1_radiotap.c
    ../../m1_csrc/m1_read_icon.c
    ../../m1_csrc/m1_power_ctl.c
    ../../m1_csrc/m1_rf_spi.c
    ../../m1_csrc/m1_rfid.c
    ../../m1_csrc/m1_ring_buffer.c
    ../../m1_csrc/m1_sdcard.c
    ../../m1_csrc/m1_sdcard_man.c
    ../../m1_csrc/m1_sdcard_provision.c
    ../../m1_csrc/m1_settings.c
    ../../m1_csrc/m1_sound.c
    ../../m1_csrc/m1_sound_policy.c
    ../../m1_csrc/m1_storage.c
    ../../m1_csrc/m1_sub_ghz.c
    ../../m1_csrc/m1_subghz_recordraw_draw.c
    ../../m1_csrc/m1_subghz_scan_draw.c
    ../../m1_csrc/m1_sys_init.c
    ../../m1_csrc/m1_system.c
    ../../m1_csrc/m1_tasks.c
    ../../m1_csrc/m1_usb_cdc_msc.c
    ../../m1_csrc/m1_heap_stack_report.c
    ../../m1_csrc/m1_hid_keyboard.c
    ../../m1_csrc/m1_manager_protocol.c
    ../../m1_csrc/m1_manager_fs.c
    ../../m1_csrc/m1_manager_fw.c
    ../../m1_csrc/m1_manager_update_coordinator.c
    ../../m1_csrc/m1_manager_fwupdate.c
    ../../m1_csrc/m1_manager_espupdate.c
    ../../m1_csrc/m1_esp_version.c
    ../../m1_csrc/m1_virtual_kb.c
    ../../m1_csrc/m1_watchdog.c
    ../../m1_csrc/m1_wifi.c
    ../../m1_csrc/m1_esp_uart_transport.c
    ../../m1_csrc/m1_wifi_session_cache.c
    ../../m1_csrc/m1_wifi_client_id.c
    ../../m1_csrc/privateprofilestring.c
    ../../m1_csrc/uiView.c
)

target_link_directories(m1_01 INTERFACE
)

target_link_libraries(m1_01 INTERFACE
)

# Validate that STM32CubeMX code is compatible with C standard
if(CMAKE_C_STANDARD LESS 11)
    message(ERROR "Generated code requires C11 or higher")
endif()
