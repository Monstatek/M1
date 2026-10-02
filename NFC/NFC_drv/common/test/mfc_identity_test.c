/* Host test for mfc_identity.c's mfc_identity_matches() -- the real
 * production card-identity check a Find-Missing-Keys continuation uses
 * before touching any existing acquisition progress (required test #13:
 * "Card identity mismatch refuses continuation without corrupting the
 * original context").
 *
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined \
 *      ../mfc_identity.c mfc_identity_test.c -o /tmp/mid && /tmp/mid
 */
#include "../mfc_identity.h"
#include <stdio.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s (line %d)\n", (m), __LINE__); } } while (0)

int main(void)
{
    const uint8_t uid4a[4] = {0x01, 0x02, 0x03, 0x04};
    const uint8_t uid4b[4] = {0x01, 0x02, 0x03, 0x04};
    const uint8_t uid4c[4] = {0x01, 0x02, 0x03, 0x05};
    const uint8_t uid7[7]  = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};

    CHECK(mfc_identity_matches(uid4a, 4, uid4b, 4), "identical 4-byte UIDs match");
    CHECK(!mfc_identity_matches(uid4a, 4, uid4c, 4), "differing 4-byte UIDs do not match");
    CHECK(!mfc_identity_matches(uid4a, 4, uid7, 7), "different lengths never match, even with a shared prefix");
    CHECK(!mfc_identity_matches(uid7, 7, uid4a, 4), "different lengths never match, reversed argument order");
    CHECK(mfc_identity_matches(uid7, 7, uid7, 7), "identical 7-byte UIDs match");

    CHECK(!mfc_identity_matches(NULL, 4, uid4a, 4), "a NULL first UID never matches, even with a plausible length");
    CHECK(!mfc_identity_matches(uid4a, 4, NULL, 4), "a NULL second UID never matches, even with a plausible length");
    CHECK(!mfc_identity_matches(NULL, 0, NULL, 0), "two NULL/zero-length UIDs never match -- 'no card yet' is never an identity");

    printf("mfc_identity_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
