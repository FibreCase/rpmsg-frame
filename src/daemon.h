#ifndef __DAEMON_H
#define __DAEMON_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FRF_DAEMON_DEFAULT_SOCKET_PATH "/tmp/frf.sock"
#define FRF_DAEMON_DEFAULT_MAX_PENDING 64

typedef struct frf_daemon frf_daemon_t;

typedef struct frf_daemon_config {
	const char *device_path;
	const char *socket_path;
	size_t max_pending_requests;
} frf_daemon_config_t;

int frf_daemon_start(frf_daemon_t **daemon_out, const frf_daemon_config_t *cfg);
void frf_daemon_stop(frf_daemon_t *daemon);

#ifdef __cplusplus
}
#endif

#endif // __DAEMON_H