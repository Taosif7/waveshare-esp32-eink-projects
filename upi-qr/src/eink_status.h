#ifndef EINK_STATUS_H
#define EINK_STATUS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Power e-paper (TCA9554 EXIO0), init panel, clear. Call once in setup. */
void eink_status_begin(void);

/** Full refresh with title + up to three status lines (ASCII, upper/lower). */
void eink_status_show(const char *title, const char *line1, const char *line2,
                      const char *line3);

/**
 * Draw a QR for `payload` (e.g. upi://...) with `web_url` centered under it.
 * Returns false if the payload is too long for the panel.
 */
bool eink_show_qr(const char *payload, const char *web_url);

#ifdef __cplusplus
}
#endif

#endif
