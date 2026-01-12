#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <syslog.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/ioctl.h>

// Include the header from driver directory
#include "../aesd-char-driver/aesd_ioctl.h"

#define PORT 9000
#define BACKLOG 10
#define FILENAME "/dev/aesdchar"

typedef struct pthread_arg_t {
    int new_socket_fd;
    struct sockaddr_in client_address;
} pthread_arg_t;

int socket_fd = -1;

void *pthread_routine(void *arg);
void signal_handler(int sig);

int main(int argc, char *argv[]) {
    int new_socket_fd;
    struct sockaddr_in address;
    pthread_attr_t pthread_attr;
    pthread_arg_t *pthread_arg;
    pthread_t pthread;
    socklen_t client_address_len;
    int yes = 1;

    openlog("aesdsocket", LOG_PID, LOG_USER);

    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

    address.sin_family = AF_INET;
    address.sin_port = htons(PORT);
    address.sin_addr.s_addr = INADDR_ANY;

    if ((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        syslog(LOG_ERR, "Socket creation failed: %m");
        return -1;
    }

    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        syslog(LOG_ERR, "setsockopt failed: %m");
    }

    if (bind(socket_fd, (struct sockaddr *)&address, sizeof address) == -1) {
        syslog(LOG_ERR, "Bind failed: %m");
        close(socket_fd);
        return -1;
    }

    if (listen(socket_fd, BACKLOG) == -1) {
        syslog(LOG_ERR, "Listen failed: %m");
        close(socket_fd);
        return -1;
    }

    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        if (daemon(0, 0) == -1) {
            syslog(LOG_ERR, "Daemonization failed: %m");
        }
    }

    pthread_attr_init(&pthread_attr);
    pthread_attr_setdetachstate(&pthread_attr, PTHREAD_CREATE_DETACHED);

    while (1) {
        pthread_arg = (pthread_arg_t *)malloc(sizeof(pthread_arg_t));
        if (!pthread_arg) continue;

        client_address_len = sizeof(pthread_arg->client_address);
        new_socket_fd = accept(socket_fd, (struct sockaddr *)&pthread_arg->client_address, &client_address_len);
        
        if (new_socket_fd != -1) {
            pthread_arg->new_socket_fd = new_socket_fd;
            pthread_create(&pthread, &pthread_attr, pthread_routine, (void *)pthread_arg);
        } else {
            free(pthread_arg);
        }
    }
    return 0;
}

void *pthread_routine(void *arg) {
    pthread_arg_t *pthread_arg = (pthread_arg_t *)arg;
    int client_fd = pthread_arg->new_socket_fd;
    
    // Open the driver IMMEDIATELY when the thread starts
    int dev_fd = open(FILENAME, O_RDWR);
    if (dev_fd < 0) {
        goto cleanup;
    }

    char recv_buf[1024];
    char *full_content = NULL;
    size_t total_received = 0;
    ssize_t bytes;

    while ((bytes = recv(client_fd, recv_buf, sizeof(recv_buf), 0)) > 0) {
        char *new_ptr = realloc(full_content, total_received + bytes);
        if (!new_ptr) goto cleanup;
        full_content = new_ptr;
        memcpy(full_content + total_received, recv_buf, bytes);
        total_received += bytes;
        if (memchr(recv_buf, '\n', bytes)) break;
    }

    if (total_received > 0) {
        // Check for IOCTL
        if (total_received >= 19 && strncmp(full_content, "AESDCHAR_IOCSEEKTO:", 19) == 0) {
            struct aesd_seekto seekto;
            if (sscanf(full_content + 19, "%u,%u", &seekto.write_cmd, &seekto.write_cmd_offset) == 2) {
                ioctl(dev_fd, AESDCHAR_IOCSEEKTO, &seekto);
                // After ioctl, we do NOT lseek(0).
            }
        } else {
            // Normal Write
            write(dev_fd, full_content, total_received);
            lseek(dev_fd, 0, SEEK_SET);
        }

        // Send back
        char read_buf[1024];
        while ((bytes = read(dev_fd, read_buf, sizeof(read_buf))) > 0) {
            send(client_fd, read_buf, bytes, 0);
        }
    }

cleanup:
    if (dev_fd >= 0) close(dev_fd);
    if (full_content) free(full_content);
    close(client_fd);
    free(arg);
    return NULL;
}

void signal_handler(int sig) {
    syslog(LOG_DEBUG, "Caught signal %d, exiting", sig);
    if (socket_fd >= 0) close(socket_fd);
    closelog();
    exit(0);
}
