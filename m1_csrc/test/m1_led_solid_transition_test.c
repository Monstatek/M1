/*
 * Source-seam regression test for the LP5814 blink-to-solid transition.
 *
 * The production driver is coupled to STM32 HAL, I2C and FreeRTOS. This test
 * verifies the critical register-programming seam in the real source: every
 * RGB solid request must stop the hardware animation engine before reading
 * and updating the output-enable register.
 *
 * Build and run from the repository root:
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined \
 *      m1_csrc/test/m1_led_solid_transition_test.c \
 *      -o /tmp/m1_led_solid_transition_test && \
 *      /tmp/m1_led_solid_transition_test
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

static char *function_body(const char *source, const char *signature)
{
    const char *search = source;
    const char *start = NULL;
    const char *open = NULL;
    const char *end;
    size_t len;
    char *body;

    while ((search = strstr(search, signature)) != NULL) {
        const char *after = search + strlen(signature);
        if (strncmp(after, "\r\n{", 3) == 0 || strncmp(after, "\n{", 2) == 0) {
            start = search;
            open = after;
            break;
        }
        search = after;
    }
    if (!start || !open) return NULL;
    end = strstr(open, "\n}\n\n\n\n/*============================================================================*/");
    if (!end) return NULL;
    len = (size_t)(end - start);
    body = malloc(len + 1u);
    if (!body) return NULL;
    memcpy(body, start, len);
    body[len] = '\0';
    return body;
}

int main(int argc, char **argv)
{
    const char *source_path = (argc > 1) ? argv[1] : "m1_csrc/m1_lp5814.c";
    char *source = read_all(source_path);
    char *body;
    const char *stop;
    const char *read_config;
    const char *enable;

    CHECK(source != NULL, "LP5814 production source is readable");
    if (!source) return 1;

    body = function_body(source, "void lp5814_led_on_rgb(uint8_t led_rgb, uint8_t value)");
    CHECK(body != NULL, "lp5814_led_on_rgb definition is found");
    if (!body) { free(source); return 1; }

    stop = strstr(body, "lp5814_fastblink_on_R_G_B(0, 0, 0);");
    read_config = strstr(body, "lp5814_readRegister(LP5814_REG_DEV_CONFIG1)");
    enable = strstr(body, "lp5814_writeRegister(LP5814_REG_DEV_CONFIG1, stat)");
    CHECK(stop != NULL, "solid RGB request explicitly stops the animation engine");
    CHECK(read_config != NULL && enable != NULL,
          "solid RGB request still reads and writes the output-enable register");
    CHECK(stop && read_config && stop < read_config,
          "animation engine is stopped before output configuration is read");
    CHECK(read_config && enable && read_config < enable,
          "output-enable update remains ordered after the register read");

    free(body);
    free(source);
    printf("m1_led_solid_transition_test: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
