#ifndef __PTS_H
#define __PTS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pts_session pts_session_t;

/* Create a PTY pair and start collecting input from the master side.
 * The returned device path is owned by the session and stays valid until pts_release().
 */
int pts_init(pts_session_t **session, const char **device_path, const char *bridge_path);

/* Take ownership of the accumulated RX bytes.
 * The caller owns the returned buffer and must free() it.
 */
int pts_take_rx_data(pts_session_t *session, uint8_t **data, size_t *len);

/* Stop the PTY, stop background collection, and free all session resources. */
void pts_release(pts_session_t *session);

#ifdef __cplusplus
}
#endif



#endif // __PTS_H