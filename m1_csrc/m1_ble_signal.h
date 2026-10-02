/* Advertising-only Signal Meter presentation helpers. No target connection. */
#ifndef M1_BLE_SIGNAL_H
#define M1_BLE_SIGNAL_H
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
static inline const char *m1_ble_category(int avg) {
    return avg >= -55 ? "Excellent" : avg >= -67 ? "Good" :
           avg >= -77 ? "Fair" : avg >= -87 ? "Weak" : "Very Weak";
}
static inline int m1_ble_rssi_valid(int rssi) { return rssi >= -127 && rssi <= 20; }
static inline void m1_ble_label(char *out, unsigned size, const char *name,
                               const char *mac, unsigned company) {
    const char *vendor = company == 0x004c ? "Apple" : company == 0x0006 ? "Microsoft" :
                         company == 0x0075 ? "Samsung" : NULL;
    if (name && *name) snprintf(out, size, "%s", name);
    else snprintf(out, size, "%s \267 %.5s", vendor ? vendor : "Unnamed", strlen(mac) == 17 ? mac + 12 : "--:--");
}
/* Accept only complete numeric RSSI fields. START does not erase an observation. */
static inline int m1_ble_signal_record(const char *line, int *avg) {
    const char *p = strstr(line, "[BLE:SIG]");
    if (!p || !(p = strstr(p, "avg="))) return 0;
    char *end;
    long value = strtol(p + 4, &end, 10);
    if (end == p + 4 || (*end && *end != ' ' && *end != '\r' && *end != '\n') ||
        value < -127 || value > 20) return 0;
    *avg = (int)value; return 1;
}
#endif
