# See COPYING.txt for license details.
#
# Host unit tests for the M1CP firmware protocol + Add Files modules. Compiles
# the REAL m1_csrc sources natively with -DM1CP_HOST_TEST (firmware HAL/FreeRTOS/
# SD/flash deps replaced by the small stubs here, plus a real-file storage
# backend), so the parser, session logic, error model, command builders and the
# file-transfer state machine + FS-safety gate are exercised without hardware.
# Run:  make -C tools/m1cp_host_test test

CC      ?= cc
CFLAGS  ?= -DM1CP_HOST_TEST -Wall -Wextra -std=c11 -O2
SRC_DIR := ../../m1_csrc
INCS    := -I. -I$(SRC_DIR)
CORE    := $(SRC_DIR)/m1_manager_protocol.c $(SRC_DIR)/m1_manager_fs.c $(SRC_DIR)/m1_manager_fw.c $(SRC_DIR)/m1_manager_update_coordinator.c $(SRC_DIR)/m1_manager_fwupdate.c $(SRC_DIR)/m1_manager_espupdate.c $(SRC_DIR)/m1_esp_version.c
DEPS    := m1cp_test_stubs.h FreeRTOS.h $(SRC_DIR)/m1_manager_protocol.h \
           $(SRC_DIR)/m1_manager_fs.h $(SRC_DIR)/m1_manager_fw.h $(SRC_DIR)/m1_manager_update_coordinator.h \
           $(SRC_DIR)/m1_manager_fwupdate.h $(SRC_DIR)/m1_manager_espupdate.h $(SRC_DIR)/m1_esp_version.h

test: test_m1cp test_m1cp_fs test_m1cp_fw test_m1cp_update_coordinator test_m1cp_fwupdate test_m1cp_espver test_m1cp_espupdate m1_manager_espupdate_seam_test m1_manager_fwupdate_seam_test
	./test_m1cp
	./test_m1cp_fs
	./test_m1cp_fw
	./test_m1cp_update_coordinator
	./test_m1cp_fwupdate
	./test_m1cp_espver
	./test_m1cp_espupdate
	./m1_manager_espupdate_seam_test
	./m1_manager_fwupdate_seam_test

test_m1cp: test_m1cp.c $(CORE) $(DEPS)
	$(CC) $(CFLAGS) $(INCS) test_m1cp.c $(CORE) -o $@

test_m1cp_fs: test_m1cp_fs.c $(CORE) $(DEPS)
	$(CC) $(CFLAGS) $(INCS) test_m1cp_fs.c $(CORE) -o $@

test_m1cp_fw: test_m1cp_fw.c $(CORE) $(DEPS)
	$(CC) $(CFLAGS) $(INCS) test_m1cp_fw.c $(CORE) -o $@

# Standalone mock-backend test of the generic coordinator itself (not
# through either target's M1CP dispatch shim, and not linking the rest of
# m1_manager_protocol.c -- the test provides its own minimal stand-ins for
# the 3 status-publish calls the coordinator makes) -- see
# m1_manager_update_coordinator.h for what it's unifying.
test_m1cp_update_coordinator: test_m1cp_update_coordinator.c $(SRC_DIR)/m1_manager_update_coordinator.c $(DEPS)
	$(CC) $(CFLAGS) $(INCS) test_m1cp_update_coordinator.c $(SRC_DIR)/m1_manager_update_coordinator.c -o $@

test_m1cp_fwupdate: test_m1cp_fwupdate.c $(CORE) $(DEPS)
	$(CC) $(CFLAGS) $(INCS) test_m1cp_fwupdate.c $(CORE) -o $@

test_m1cp_espver: test_m1cp_espver.c $(CORE) $(DEPS)
	$(CC) $(CFLAGS) $(INCS) test_m1cp_espver.c $(CORE) -o $@

test_m1cp_espupdate: test_m1cp_espupdate.c $(CORE) $(DEPS)
	$(CC) $(CFLAGS) $(INCS) test_m1cp_espupdate.c $(CORE) -o $@

# Source-seam tests (bounded string search, no linking) for the device-only
# firmware backends (real HAL/esp_loader/flash deps) -- see each backend
# file's own header comment.
m1_manager_espupdate_seam_test: m1_manager_espupdate_seam_test.c
	$(CC) -std=c11 -Wall -Wextra m1_manager_espupdate_seam_test.c -o $@

m1_manager_fwupdate_seam_test: m1_manager_fwupdate_seam_test.c
	$(CC) -std=c11 -Wall -Wextra m1_manager_fwupdate_seam_test.c -o $@

# Harness that runs a .m1pkg file through the real validator (fixture check).
validate_file: validate_file.c $(CORE) $(DEPS)
	$(CC) $(CFLAGS) $(INCS) validate_file.c $(CORE) -o $@

clean:
	rm -f test_m1cp test_m1cp_fs test_m1cp_fw test_m1cp_update_coordinator test_m1cp_fwupdate test_m1cp_espver test_m1cp_espupdate validate_file m1_manager_espupdate_seam_test m1_manager_fwupdate_seam_test
	rm -rf fs_sandbox

.PHONY: test clean
