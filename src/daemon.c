#define _GNU_SOURCE

#include "daemon.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "rframe.h"

#define FRF_DAEMON_REQUEST_MAX_DATA 255
#define FRF_DAEMON_WIRE_REQUEST_BYTES (sizeof(uint16_t) + sizeof(uint8_t))
#define FRF_DAEMON_WIRE_RESPONSE_BYTES (sizeof(uint8_t) + sizeof(int32_t))

typedef struct frf_daemon_request {
	rframe_payload_t payload;
	int client_fd;
	struct frf_daemon_request *next;
} frf_daemon_request_t;

typedef struct frf_daemon_client {
	int fd;
	struct frf_daemon_client *next;
} frf_daemon_client_t;

struct frf_daemon {
	tty_driver_t *drv;
	int listen_fd;
	char *socket_path;
	size_t max_pending_requests;
	int running;
	int accept_thread_started;
	int worker_thread_started;
	pthread_t accept_thread;
	pthread_t worker_thread;
	pthread_mutex_t queue_lock;
	pthread_cond_t queue_not_empty;
	pthread_cond_t queue_not_full;
	frf_daemon_request_t *queue_head;
	frf_daemon_request_t *queue_tail;
	size_t pending_count;
	pthread_mutex_t clients_lock;
	pthread_cond_t clients_empty;
	frf_daemon_client_t *clients;
	size_t active_clients;
};

typedef struct frf_daemon_client_thread_arg {
	struct frf_daemon *daemon;
	int client_fd;
} frf_daemon_client_thread_arg_t;

static void daemon_rx_sink(rframe_payload_t payload, void *user_ctx)
{
	(void)payload;
	(void)user_ctx;
}

static int set_cloexec(int fd)
{
	int flags = fcntl(fd, F_GETFD);
	if (flags < 0) {
		return -1;
	}
	if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) {
		return -1;
	}
	return 0;
}

static int write_all(int fd, const void *buf, size_t len)
{
	const uint8_t *p = (const uint8_t *)buf;
	size_t offset = 0;
	int send_flags = 0;

#ifdef MSG_NOSIGNAL
	send_flags = MSG_NOSIGNAL;
#endif

	while (offset < len) {
		ssize_t written = send(fd, p + offset, len - offset, send_flags);
		if (written < 0) {
			if (errno == EINTR) {
				continue;
			}
			return -1;
		}
		offset += (size_t)written;
	}

	return 0;
}

static void daemon_send_response(int client_fd, uint8_t status, int error_code)
{
	uint8_t response[FRF_DAEMON_WIRE_RESPONSE_BYTES];
	int32_t error_value = (int32_t)error_code;

	response[0] = status;
	error_value = htonl((uint32_t)error_value);
	memcpy(response + 1, &error_value, sizeof(error_value));

	if (write_all(client_fd, response, sizeof(response)) != 0) {
		(void)errno;
	}
}

static int enqueue_request(frf_daemon_t *daemon, frf_daemon_request_t *request)
{
	pthread_mutex_lock(&daemon->queue_lock);
	while (daemon->running && daemon->pending_count >= daemon->max_pending_requests) {
		pthread_cond_wait(&daemon->queue_not_full, &daemon->queue_lock);
	}

	if (!daemon->running) {
		pthread_mutex_unlock(&daemon->queue_lock);
		errno = ESHUTDOWN;
		return -1;
	}

	request->next = NULL;
	if (daemon->queue_tail != NULL) {
		daemon->queue_tail->next = request;
	} else {
		daemon->queue_head = request;
	}
	daemon->queue_tail = request;
	daemon->pending_count++;

	pthread_cond_signal(&daemon->queue_not_empty);
	pthread_mutex_unlock(&daemon->queue_lock);
	return 0;
}

static frf_daemon_request_t *dequeue_request(frf_daemon_t *daemon)
{
	pthread_mutex_lock(&daemon->queue_lock);
	while (daemon->running && daemon->queue_head == NULL) {
		pthread_cond_wait(&daemon->queue_not_empty, &daemon->queue_lock);
	}

	if (daemon->queue_head == NULL) {
		pthread_mutex_unlock(&daemon->queue_lock);
		return NULL;
	}

	frf_daemon_request_t *request = daemon->queue_head;
	daemon->queue_head = request->next;
	if (daemon->queue_head == NULL) {
		daemon->queue_tail = NULL;
	}
	if (daemon->pending_count > 0) {
		daemon->pending_count--;
	}
	pthread_cond_signal(&daemon->queue_not_full);
	pthread_mutex_unlock(&daemon->queue_lock);
	return request;
}

static int parse_request_packet(const uint8_t *buffer,
								ssize_t length,
								frf_daemon_request_t *request,
								int client_fd)
{
	if (length < (ssize_t)FRF_DAEMON_WIRE_REQUEST_BYTES) {
		errno = EINVAL;
		return -1;
	}

	uint16_t cmd_network = 0;
	uint8_t data_length = 0;

	memcpy(&cmd_network, buffer, sizeof(cmd_network));
	memcpy(&data_length, buffer + sizeof(cmd_network), sizeof(data_length));

	if (data_length > FRF_DAEMON_REQUEST_MAX_DATA) {
		errno = EINVAL;
		return -1;
	}

	if (length != (ssize_t)(FRF_DAEMON_WIRE_REQUEST_BYTES + data_length)) {
		errno = EINVAL;
		return -1;
	}

	request->payload.header = 0xAA55;
	request->payload.cmd = ntohs(cmd_network);
	request->payload.data_length = data_length;
	if (data_length > 0) {
		memcpy(request->payload.data, buffer + FRF_DAEMON_WIRE_REQUEST_BYTES, data_length);
	}
	request->client_fd = client_fd;
	request->next = NULL;
	return 0;
}

static int add_client_fd(frf_daemon_t *daemon, int client_fd)
{
	frf_daemon_client_t *client = (frf_daemon_client_t *)calloc(1, sizeof(*client));
	if (client == NULL) {
		errno = ENOMEM;
		return -1;
	}

	client->fd = client_fd;

	pthread_mutex_lock(&daemon->clients_lock);
	client->next = daemon->clients;
	daemon->clients = client;
	daemon->active_clients++;
	pthread_mutex_unlock(&daemon->clients_lock);
	return 0;
}

static void remove_client_fd(frf_daemon_t *daemon, int client_fd)
{
	pthread_mutex_lock(&daemon->clients_lock);

	frf_daemon_client_t **link = &daemon->clients;
	while (*link != NULL) {
		if ((*link)->fd == client_fd) {
			frf_daemon_client_t *client = *link;
			*link = client->next;
			free(client);
			if (daemon->active_clients > 0) {
				daemon->active_clients--;
			}
			if (daemon->active_clients == 0) {
				pthread_cond_broadcast(&daemon->clients_empty);
			}
			break;
		}
		link = &(*link)->next;
	}

	pthread_mutex_unlock(&daemon->clients_lock);
}

static void close_all_clients(frf_daemon_t *daemon)
{
	pthread_mutex_lock(&daemon->clients_lock);
	for (frf_daemon_client_t *client = daemon->clients; client != NULL; client = client->next) {
		(void)shutdown(client->fd, SHUT_RDWR);
	}
	pthread_mutex_unlock(&daemon->clients_lock);
}

static void *frf_daemon_worker_thread(void *arg)
{
	frf_daemon_t *daemon = (frf_daemon_t *)arg;

	while (1) {
		frf_daemon_request_t *request = dequeue_request(daemon);
		if (request == NULL) {
			pthread_mutex_lock(&daemon->queue_lock);
			bool should_exit = !daemon->running && daemon->queue_head == NULL;
			pthread_mutex_unlock(&daemon->queue_lock);
			if (should_exit) {
				break;
			}
			continue;
		}

		int send_errno = 0;
		uint8_t status = 0;

		if (rframe_send_payload(daemon->drv, &request->payload) != 0) {
			status = 1;
			send_errno = errno;
		}

		daemon_send_response(request->client_fd, status, send_errno);
		free(request);
	}

	return NULL;
}

static void *frf_daemon_client_thread(void *arg)
{
	frf_daemon_client_thread_arg_t *thread_arg = (frf_daemon_client_thread_arg_t *)arg;
	frf_daemon_t *daemon = thread_arg->daemon;
	int client_fd = thread_arg->client_fd;
	uint8_t buffer[512];

	free(thread_arg);

	while (1) {
		ssize_t received = recv(client_fd, buffer, sizeof(buffer), 0);
		if (received < 0) {
			if (errno == EINTR) {
				continue;
			}
			break;
		}
		if (received == 0) {
			break;
		}

		frf_daemon_request_t *request = (frf_daemon_request_t *)calloc(1, sizeof(*request));
		if (request == NULL) {
			daemon_send_response(client_fd, 1, ENOMEM);
			continue;
		}

		if (parse_request_packet(buffer, received, request, client_fd) != 0) {
			daemon_send_response(client_fd, 1, errno);
			free(request);
			continue;
		}

		if (enqueue_request(daemon, request) != 0) {
			daemon_send_response(client_fd, 1, errno != 0 ? errno : ESHUTDOWN);
			free(request);
			break;
		}
	}

	close(client_fd);
	remove_client_fd(daemon, client_fd);
	return NULL;
}

static void *frf_daemon_accept_thread(void *arg)
{
	frf_daemon_t *daemon = (frf_daemon_t *)arg;

	while (1) {
		struct sockaddr_un client_addr;
		socklen_t client_len = sizeof(client_addr);
		int client_fd = accept(daemon->listen_fd, (struct sockaddr *)&client_addr, &client_len);
		if (client_fd < 0) {
			if (errno == EINTR) {
				continue;
			}
			if (!daemon->running) {
				break;
			}
			continue;
		}

		if (set_cloexec(client_fd) != 0) {
			close(client_fd);
			continue;
		}

		if (add_client_fd(daemon, client_fd) != 0) {
			close(client_fd);
			continue;
		}

		frf_daemon_client_thread_arg_t *thread_arg =
			(frf_daemon_client_thread_arg_t *)calloc(1, sizeof(*thread_arg));
		if (thread_arg == NULL) {
			remove_client_fd(daemon, client_fd);
			close(client_fd);
			continue;
		}

		thread_arg->daemon = daemon;
		thread_arg->client_fd = client_fd;

		pthread_t client_thread;
		if (pthread_create(&client_thread, NULL, frf_daemon_client_thread, thread_arg) != 0) {
			free(thread_arg);
			remove_client_fd(daemon, client_fd);
			close(client_fd);
			continue;
		}

		pthread_detach(client_thread);
	}

	return NULL;
}

static int create_listening_socket(const char *socket_path)
{
	if (socket_path == NULL) {
		errno = EINVAL;
		return -1;
	}

	size_t socket_path_len = strlen(socket_path);
	if (socket_path_len == 0 || socket_path_len >= sizeof(((struct sockaddr_un *)0)->sun_path)) {
		errno = ENAMETOOLONG;
		return -1;
	}

	int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	if (fd < 0) {
		return -1;
	}

	if (set_cloexec(fd) != 0) {
		close(fd);
		return -1;
	}

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

	(void)unlink(socket_path);
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		close(fd);
		return -1;
	}

	if (listen(fd, 16) != 0) {
		close(fd);
		(void)unlink(socket_path);
		return -1;
	}

	if (chmod(socket_path, 0660) != 0) {
		close(fd);
		(void)unlink(socket_path);
		return -1;
	}

	return fd;
}

static void destroy_daemon(frf_daemon_t *daemon)
{
	if (daemon == NULL) {
		return;
	}

	if (daemon->listen_fd >= 0) {
		close(daemon->listen_fd);
		daemon->listen_fd = -1;
	}

	if (daemon->socket_path != NULL) {
		(void)unlink(daemon->socket_path);
	}

	if (daemon->drv != NULL) {
		(void)rframe_close(daemon->drv);
		daemon->drv = NULL;
	}

	frf_daemon_request_t *request = daemon->queue_head;
	while (request != NULL) {
		frf_daemon_request_t *next = request->next;
		free(request);
		request = next;
	}

	frf_daemon_client_t *client = daemon->clients;
	while (client != NULL) {
		frf_daemon_client_t *next = client->next;
		free(client);
		client = next;
	}

	pthread_cond_destroy(&daemon->clients_empty);
	pthread_mutex_destroy(&daemon->clients_lock);
	pthread_cond_destroy(&daemon->queue_not_full);
	pthread_cond_destroy(&daemon->queue_not_empty);
	pthread_mutex_destroy(&daemon->queue_lock);

	free(daemon->socket_path);
	free(daemon);
}

int frf_daemon_start(frf_daemon_t **daemon_out, const frf_daemon_config_t *cfg)
{
	if (daemon_out == NULL || cfg == NULL || cfg->device_path == NULL || cfg->socket_path == NULL) {
		errno = EINVAL;
		return -1;
	}

	*daemon_out = NULL;

	frf_daemon_t *daemon = (frf_daemon_t *)calloc(1, sizeof(*daemon));
	if (daemon == NULL) {
		errno = ENOMEM;
		return -1;
	}

	daemon->listen_fd = -1;
	daemon->max_pending_requests =
		cfg->max_pending_requests == 0 ? FRF_DAEMON_DEFAULT_MAX_PENDING : cfg->max_pending_requests;

	if (pthread_mutex_init(&daemon->queue_lock, NULL) != 0 ||
		pthread_cond_init(&daemon->queue_not_empty, NULL) != 0 ||
		pthread_cond_init(&daemon->queue_not_full, NULL) != 0 ||
		pthread_mutex_init(&daemon->clients_lock, NULL) != 0 ||
		pthread_cond_init(&daemon->clients_empty, NULL) != 0) {
		destroy_daemon(daemon);
		return -1;
	}

	daemon->socket_path = strdup(cfg->socket_path);
	if (daemon->socket_path == NULL) {
		destroy_daemon(daemon);
		errno = ENOMEM;
		return -1;
	}

	daemon->listen_fd = create_listening_socket(cfg->socket_path);
	if (daemon->listen_fd < 0) {
		destroy_daemon(daemon);
		return -1;
	}

	daemon->drv = rframe_init((char *)cfg->device_path, daemon_rx_sink, NULL);
	if (daemon->drv == NULL) {
		destroy_daemon(daemon);
		return -1;
	}

	daemon->running = 1;

	if (pthread_create(&daemon->worker_thread, NULL, frf_daemon_worker_thread, daemon) != 0) {
		daemon->running = 0;
		destroy_daemon(daemon);
		return -1;
	}
	daemon->worker_thread_started = 1;

	if (pthread_create(&daemon->accept_thread, NULL, frf_daemon_accept_thread, daemon) != 0) {
		daemon->running = 0;
		pthread_cond_broadcast(&daemon->queue_not_empty);
		pthread_join(daemon->worker_thread, NULL);
		destroy_daemon(daemon);
		return -1;
	}
	daemon->accept_thread_started = 1;

	*daemon_out = daemon;
	return 0;
}

void frf_daemon_stop(frf_daemon_t *daemon)
{
	if (daemon == NULL) {
		return;
	}

	pthread_mutex_lock(&daemon->queue_lock);
	daemon->running = 0;
	pthread_cond_broadcast(&daemon->queue_not_empty);
	pthread_cond_broadcast(&daemon->queue_not_full);
	pthread_mutex_unlock(&daemon->queue_lock);

	close_all_clients(daemon);

	if (daemon->listen_fd >= 0) {
		close(daemon->listen_fd);
		daemon->listen_fd = -1;
	}

	if (daemon->accept_thread_started) {
		pthread_join(daemon->accept_thread, NULL);
		daemon->accept_thread_started = 0;
	}

	if (daemon->worker_thread_started) {
		pthread_join(daemon->worker_thread, NULL);
		daemon->worker_thread_started = 0;
	}

	pthread_mutex_lock(&daemon->clients_lock);
	while (daemon->active_clients > 0) {
		pthread_cond_wait(&daemon->clients_empty, &daemon->clients_lock);
	}
	pthread_mutex_unlock(&daemon->clients_lock);

	destroy_daemon(daemon);
}
