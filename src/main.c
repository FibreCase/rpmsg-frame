
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "daemon.h"

static volatile sig_atomic_t g_stop_requested = 0;

static void handle_signal(int signum)
{
    (void)signum;
    g_stop_requested = 1;
}

static int install_signal_handlers(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_signal;

    if (sigaction(SIGINT, &action, NULL) != 0) {
        return -1;
    }
    if (sigaction(SIGTERM, &action, NULL) != 0) {
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    const char *device_path = argc > 1 ? argv[1] : "/dev/pts/7";
    const char *socket_path = argc > 2 ? argv[2] : FRF_DAEMON_DEFAULT_SOCKET_PATH;

    if (argc > 3) {
        fprintf(stderr, "Usage: %s [device_path] [socket_path]\n", argv[0]);
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

    if (install_signal_handlers() != 0) {
        fprintf(stderr, "Failed to install signal handlers: %s\n", strerror(errno));
        frf_daemon_stop(daemon);
        return EXIT_FAILURE;
    }

    while (!g_stop_requested) {
        pause();
    }

    frf_daemon_stop(daemon);
    return EXIT_SUCCESS;
}