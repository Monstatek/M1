/*
 * Source-seam test for Wi-Fi/MonstaShark presentation sharing.
 *
 * The production files require STM32 HAL, FreeRTOS and u8g2, so this test
 * checks the exact integration seam in those files. It intentionally does
 * not model or replace transport behavior.
 *
 * Build and run from the repository root:
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined \
 *      m1_csrc/test/m1_wifi_capture_ui_unification_test.c \
 *      -o /tmp/m1_wifi_capture_ui_unification_test && \
 *      /tmp/m1_wifi_capture_ui_unification_test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int passed;
static int failed;
#define CHECK(c, m) do { if (c) passed++; else { failed++; printf("  FAIL: %s\n", (m)); } } while (0)

static char *read_all(const char *path)
{
    FILE *f = fopen(path, "rb");
    long size;
    char *text;
    size_t got;

    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    text = malloc((size_t)size + 1u);
    if (!text) { fclose(f); return NULL; }
    got = fread(text, 1u, (size_t)size, f);
    fclose(f);
    text[got] = '\0';
    return text;
}

static int has(const char *text, const char *needle)
{
    return text && strstr(text, needle) != NULL;
}

int main(void)
{
    char *wifi = read_all("m1_csrc/m1_wifi.c");
    char *capture = read_all("m1_csrc/m1_capture_link.c");
    char *header = read_all("m1_csrc/m1_wifi.h");
    char *menu = read_all("m1_csrc/m1_menu.c");

    CHECK(wifi != NULL && capture != NULL && header != NULL && menu != NULL,
          "production sources are readable");
    if (!wifi || !capture || !header || !menu) return 1;

    CHECK(has(header, "void wifi_ui_draw_scanning(const char *msg);"),
          "shared scanning renderer is public to the two UI modules");
    CHECK(has(header, "void wifi_ui_draw_ap_card("),
          "shared AP-card renderer is public to the two UI modules");
    CHECK(has(wifi, "wifi_ui_draw_ap_card(heading, *sel, count,"),
          "standard Wi-Fi AP browser uses the shared AP-card renderer");
    CHECK(has(capture, "wifi_ui_draw_ap_card(\"Networks\""),
          "MonstaShark AP picker uses the same AP-card renderer");
    CHECK(has(capture, "wifi_ui_draw_scanning(\"Scanning Networks...\")"),
          "MonstaShark scan uses the shared Wi-Fi scanning renderer");
    CHECK(!has(capture, "static void cap_footer_bar("),
          "duplicate MonstaShark AP footer implementation is removed");
    CHECK(has(menu, "\"Wi-Fi\", menu_wifi_init"),
          "main menu uses the canonical Wi-Fi label");

    /* Transport and feature behavior remains on the original command path. */
    CHECK(has(capture, "#define CAP_SCAN_CMD             \"scan -a\\r\\n\""),
          "AP scan command is unchanged");
    CHECK(has(capture, "snprintf(cmd, sizeof(cmd), \"PCAP_START %u %lu\\r\\n\""),
          "capture start command is unchanged");
    CHECK(has(capture, "cap_run_and_show_result((uint8_t)cur_ch, k_cap_secs[dur_idx]);"),
          "selected AP still reaches the existing capture runner");
    CHECK(has(capture, "return CAP_AP_PICK_REFRESH;"),
          "UP/DOWN refresh outcome remains present");
    CHECK(has(capture, "return CAP_AP_PICK_BACK;"),
          "physical BACK outcome remains present");

    free(wifi);
    free(capture);
    free(header);
    free(menu);
    printf("m1_wifi_capture_ui_unification_test: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
