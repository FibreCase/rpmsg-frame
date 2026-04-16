
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <pthread.h>

#include "daemon.h"

int main(int argc, char *argv[])
{
    const char *device_path = argc > 1 ? argv[1] : "/dev/ttyRPMSG0";
    const char *socket_path = argc > 2 ? argv[2] : FRF_DAEMON_DEFAULT_SOCKET_PATH;

    if (argc > 3) {
        fprintf(stderr, "Usage: %s [device_path] [socket_path]\n", argv[0]);
        return EXIT_FAILURE;
    }

    sigset_t signal_set;
    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGINT);
    sigaddset(&signal_set, SIGTERM);

    int mask_result = pthread_sigmask(SIG_BLOCK, &signal_set, NULL);
    if (mask_result != 0) {
        fprintf(stderr, "Failed to block signal handlers: %s\n", strerror(mask_result));
        return EXIT_FAILURE;
    }

    frf_daemon_config_t cfg = {
        .device_path = device_path,
        .socket_path = socket_path,
        .max_pending_requests = FRF_DAEMON_DEFAULT_MAX_PENDING,
    };

    frf_daemon_t *daemon = NULL;
    if (frf_daemon_start(&daemon, &cfg) != 0) {
        fprintf(stderr, "Failed to start daemon for %s on %s: %s\n",
                device_path, socket_path, strerror(errno));
        return EXIT_FAILURE;
    }

    int received_signal = 0;
    int wait_result = sigwait(&signal_set, &received_signal);
    if (wait_result != 0) {
        fprintf(stderr, "Failed while waiting for shutdown signal: %s\n", strerror(wait_result));
        frf_daemon_stop(daemon);
        return EXIT_FAILURE;
    }

    frf_daemon_stop(daemon);
    return EXIT_SUCCESS;
}