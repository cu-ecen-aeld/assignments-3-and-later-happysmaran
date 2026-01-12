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
    char recv_buf[1024];
    char *full_content = NULL;
    size_t total_received = 0;
    ssize_t bytes_received;
    int ioctl_performed = 0;
    struct aesd_seekto seekto;

    // 1. Accumulate data
    while ((bytes_received = recv(client_fd, recv_buf, sizeof(recv_buf), 0)) > 0) {
        char *new_ptr = realloc(full_content, total_received + bytes_received);
        if (!new_ptr) {
            syslog(LOG_ERR, "Realloc failed");
            goto cleanup;
        }
        full_content = new_ptr;
        memcpy(full_content + total_received, recv_buf, bytes_received);
        total_received += bytes_received;

        // Break early if newline is found, as per Assignment requirements
        if (memchr(recv_buf, '\n', bytes_received)) {
            break;
        }
    }

    // 2. Only process if we actually got something
    if (total_received > 0) {
        int dev_fd = open(FILENAME, O_RDWR);
        if (dev_fd < 0) {
            syslog(LOG_ERR, "Could not open %s: %m", FILENAME);
            goto cleanup;
        }

        // Check for IOCTL string: "AESDCHAR_IOCSEEKTO:X,Y"
        if (total_received >= 19 && strncmp(full_content, "AESDCHAR_IOCSEEKTO:", 19) == 0) {
            char *tmp = malloc(total_received + 1);
            if (tmp) {
                memcpy(tmp, full_content, total_received);
                tmp[total_received] = '\0';
                if (sscanf(tmp + 19, "%u,%u", &seekto.write_cmd, &seekto.write_cmd_offset) == 2) {
                    if (ioctl(dev_fd, AESDCHAR_IOCSEEKTO, &seekto) == 0) {
                        ioctl_performed = 1;
                    }
                }
                free(tmp);
            }
        }

        if (!ioctl_performed) {
            // Write the string (including the newline we just verified)
            if (write(dev_fd, full_content, total_received) != total_received) {
                syslog(LOG_ERR, "Partial write to driver");
            }
            // Rewind to 0 to send the full history as expected by swrite tests
            lseek(dev_fd, 0, SEEK_SET);
        }

        // 3. Send back the driver contents
        char read_buf[1024];
        ssize_t bytes_read;
        // sockettest.sh is very sensitive to these bytes
        while ((bytes_read = read(dev_fd, read_buf, sizeof(read_buf))) > 0) {
            if (send(client_fd, read_buf, bytes_read, 0) < 0) {
                syslog(LOG_ERR, "Send failed: %m");
                break;
            }
        }
        close(dev_fd);
    }

cleanup:
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
