/* Host test for the release-active input/bounds validation in
 * m1_csrc/m1_rf_spi.c m1_spi_hal_trans_req().
 *
 * Links the REAL, unmodified m1_rf_spi.c (via the stub HAL/FreeRTOS headers in
 * test/stub_rfspi/ and the counting mocks in rfspi_mock.c) so the actual
 * production preflight is what runs -- not a copy of it. The private statics
 * pspihdl / mutex_rf_spi_trans are driven through the real m1_spi_hal_init().
 *
 * Compiled with -DNDEBUG ON PURPOSE: that is exactly the shipped release
 * condition (compile_commands.json shows -DNDEBUG on every firmware compile),
 * under which assert() is erased. So this proves the NEW runtime fail-closed
 * check protects the SPI boundary even though the old asserts do not -- the
 * whole point of the fix. m1_spi_hal_init()'s own asserts are likewise no-ops
 * here, which is what lets the test place each static in the state it needs.
 *
 * Build & run (from repo root):
 *   cc -std=c11 -Wall -Wextra -DNDEBUG -fsanitize=address,undefined -fno-sanitize=enum \
 *      -I m1_csrc/test/stub_rfspi -I m1_csrc \
 *      m1_csrc/m1_rf_spi.c \
 *      m1_csrc/test/stub_rfspi/rfspi_mock.c \
 *      m1_csrc/test/m1_rf_spi_bounds_test.c \
 *      -o /tmp/rfspi_bounds && /tmp/rfspi_bounds
 *
 * -fno-sanitize=enum: the "very large dev_id" case deliberately stores an
 * out-of-enumerator value into the dev_id field to prove the runtime bound
 * rejects it; that injected value is precisely what the guard exists to catch,
 * so the enum-range sanitizer is disabled (only) to allow feeding it. Every
 * other ASan/UBSan check stays on.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "stm32h5xx_hal.h"
#include "m1_rf_spi.h"
#include "stub_rfspi/rfspi_mock.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); } \
} while (0)

static SPI_HandleTypeDef g_fake_spi;
static uint8_t g_tx[4] = {1, 2, 3, 4};
static uint8_t g_rx[4] = {0};

/* Put both private statics in the fully-valid state (real handle, real mutex). */
static void setup_valid_statics(void)
{
    g_mock_mutex_create_null = 0;
    m1_spi_hal_init(&g_fake_spi);   /* sets pspihdl=&g_fake_spi, mutex=non-NULL */
}

static S_M1_SPI_Trans_Inf make_inf(S_M1_SPI_DeviceId dev, S_M1_SPI_Transaction_Type type)
{
    S_M1_SPI_Trans_Inf inf;
    memset(&inf, 0, sizeof(inf));
    inf.dev_id     = dev;
    inf.trans_type = type;
    inf.data_len   = 4;
    inf.pdata_tx   = g_tx;
    inf.pdata_rx   = g_rx;
    inf.timeout    = 100;
    return inf;
}

int main(void)
{
    /* ---- 1. NULL transaction -> reject, zero side effects ---- */
    setup_valid_statics();
    rfspi_mock_reset();
    CHECK(m1_spi_hal_trans_req(NULL) == HAL_ERROR, "NULL trans_inf must return HAL_ERROR");
    CHECK(rfspi_mock_total_side_effects() == 0, "NULL trans_inf must cause zero sem/GPIO/HAL calls");

    /* ---- 2. NULL SPI handle -> reject, zero side effects ---- */
    m1_spi_hal_init(NULL);          /* pspihdl = NULL (mutex created non-NULL) */
    rfspi_mock_reset();
    {
        S_M1_SPI_Trans_Inf inf = make_inf(SPI_DEVICE_NFC, SPI_TRANS_WRITE_DATA);
        CHECK(m1_spi_hal_trans_req(&inf) == HAL_ERROR, "NULL pspihdl must return HAL_ERROR");
        CHECK(rfspi_mock_total_side_effects() == 0, "NULL pspihdl must cause zero side effects");
    }

    /* ---- 3. NULL mutex -> reject, zero side effects ---- */
    g_mock_mutex_create_null = 1;
    m1_spi_hal_init(&g_fake_spi);   /* pspihdl=&g_fake_spi, mutex=NULL */
    rfspi_mock_reset();
    {
        S_M1_SPI_Trans_Inf inf = make_inf(SPI_DEVICE_NFC, SPI_TRANS_WRITE_DATA);
        CHECK(m1_spi_hal_trans_req(&inf) == HAL_ERROR, "NULL mutex must return HAL_ERROR");
        CHECK(rfspi_mock_total_side_effects() == 0, "NULL mutex must cause zero side effects");
    }

    /* ---- 4. dev_id exactly at first invalid value (SPI_DEVICE_END_OF_LIST) ---- */
    setup_valid_statics();
    rfspi_mock_reset();
    {
        S_M1_SPI_Trans_Inf inf = make_inf(SPI_DEVICE_END_OF_LIST, SPI_TRANS_WRITE_DATA);
        CHECK(m1_spi_hal_trans_req(&inf) == HAL_ERROR, "dev_id == END_OF_LIST must return HAL_ERROR");
        CHECK(rfspi_mock_total_side_effects() == 0, "first-invalid dev_id must cause zero side effects");
    }

    /* ---- 5. very large dev_id -> reject, zero side effects ---- */
    setup_valid_statics();
    rfspi_mock_reset();
    {
        S_M1_SPI_Trans_Inf inf = make_inf((S_M1_SPI_DeviceId)999999, SPI_TRANS_WRITE_DATA);
        CHECK(m1_spi_hal_trans_req(&inf) == HAL_ERROR, "huge dev_id must return HAL_ERROR");
        CHECK(rfspi_mock_total_side_effects() == 0, "huge dev_id must cause zero side effects");
    }

    /* ---- 6. every valid device, WRITE ---- */
    for (S_M1_SPI_DeviceId dev = SPI_DEVICE_NFC; dev < SPI_DEVICE_END_OF_LIST; dev++)
    {
        setup_valid_statics();
        rfspi_mock_reset();
        S_M1_SPI_Trans_Inf inf = make_inf(dev, SPI_TRANS_WRITE_DATA);
        CHECK(m1_spi_hal_trans_req(&inf) == HAL_OK, "valid device WRITE must return HAL_OK");
        CHECK(g_sem_take_count == 1 && g_sem_give_count == 1, "valid WRITE takes+gives the mutex exactly once");
        CHECK(g_spi_tx_count == 1, "valid WRITE issues exactly one HAL_SPI_Transmit");
        CHECK(g_gpio_write_count == 2, "valid WRITE drives NSS low then high (2 GPIO writes)");
    }

    /* ---- 7a. valid READ ---- */
    setup_valid_statics();
    rfspi_mock_reset();
    {
        S_M1_SPI_Trans_Inf inf = make_inf(SPI_DEVICE_NFC, SPI_TRANS_READ_DATA);
        CHECK(m1_spi_hal_trans_req(&inf) == HAL_OK, "valid READ must return HAL_OK");
        CHECK(g_sem_take_count == 1 && g_sem_give_count == 1, "valid READ takes+gives the mutex once");
        CHECK(g_spi_rx_count == 1, "valid READ issues exactly one HAL_SPI_Receive");
    }

    /* ---- 7b. valid WRITEREAD ---- */
    setup_valid_statics();
    rfspi_mock_reset();
    {
        S_M1_SPI_Trans_Inf inf = make_inf(SPI_DEVICE_NFC, SPI_TRANS_WRITEREAD_DATA);
        CHECK(m1_spi_hal_trans_req(&inf) == HAL_OK, "valid WRITEREAD must return HAL_OK");
        CHECK(g_spi_txrx_count == 1, "valid WRITEREAD issues exactly one HAL_SPI_TransmitReceive");
    }

    /* ---- 7c. valid NO_NSS op: no NSS pin driven, still one transfer ---- */
    setup_valid_statics();
    rfspi_mock_reset();
    {
        S_M1_SPI_Trans_Inf inf = make_inf(SPI_DEVICE_NFC, SPI_TRANS_WRITE_DATA_NO_NSS);
        CHECK(m1_spi_hal_trans_req(&inf) == HAL_OK, "valid NO_NSS WRITE must return HAL_OK");
        CHECK(g_gpio_write_count == 0, "NO_NSS op must not drive any NSS pin");
        CHECK(g_spi_tx_count == 1, "NO_NSS WRITE issues exactly one HAL_SPI_Transmit");
        CHECK(g_sem_take_count == 1 && g_sem_give_count == 1, "NO_NSS op still takes+gives the mutex once");
    }

    /* ---- 8. HAL transport failure still releases the mutex and returns the failure ---- */
    setup_valid_statics();
    rfspi_mock_reset();
    g_hal_spi_ret = HAL_TIMEOUT;
    {
        S_M1_SPI_Trans_Inf inf = make_inf(SPI_DEVICE_NFC, SPI_TRANS_WRITE_DATA);
        CHECK(m1_spi_hal_trans_req(&inf) == HAL_TIMEOUT, "HAL failure must be returned verbatim");
        CHECK(g_sem_take_count == 1 && g_sem_give_count == 1, "HAL failure still releases the mutex exactly once");
    }

    printf("m1_rf_spi_bounds_test: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
