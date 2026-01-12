#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <syslog.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/ioctl.h>

// Include the header from your driver directory
#include "../aesd-char-driver/aesd_ioctl.h"

#define PORT 9000
#define BACKLOG 10
#define FILENAME "/dev/aesdchar"
#define FALSE 0
#define TRUE 1

typedef struct pthread_arg_t {
    int new_socket_fd;
    struct sockaddr_in client_address;
} pthread_arg_t;

int socket_fd = -1;
int file_fd = -1;
pthread_mutex_t fileFD;

void *pthread_routine(void *arg);
void signal_handler(int sig, siginfo_t *si, void *uc);

int main(int argc, char *argv[]) {
    int new_socket_fd;
    struct sockaddr_in address;
    pthread_attr_t pthread_attr;
    pthread_arg_t *pthread_arg;
    pthread_t pthread;
    socklen_t client_address_len;
    static int yes = 1;

    openlog("aesdsocket", LOG_PID, LOG_USER);

    struct sigaction sa = { 0 };
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = signal_handler;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    address.sin_family = AF_INET;
    address.sin_port = htons(PORT);
    address.sin_addr.s_addr = INADDR_ANY;

    if ((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        syslog(LOG_ERR, "Socket creation failed: %m");
        exit(1);
    }

    setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    if (bind(socket_fd, (struct sockaddr *)&address, sizeof address) == -1) {
        syslog(LOG_ERR, "Bind failed: %m");
        exit(1);
    }

    if (listen(socket_fd, BACKLOG) == -1) {
        syslog(LOG_ERR, "Listen failed: %m");
        exit(1);
    }

    pthread_attr_init(&pthread_attr);
    pthread_attr_setdetachstate(&pthread_attr, PTHREAD_CREATE_DETACHED);
    pthread_mutex_init(&fileFD, NULL);

    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        if (daemon(0, 0) == -1) {
            syslog(LOG_ERR, "Daemonization failed: %m");
        }
    }

    while (1) {
        pthread_arg = (pthread_arg_t *)malloc(sizeof *pthread_arg);
        client_address_len = sizeof pthread_arg->client_address;
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
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(pthread_arg->client_address.sin_addr), client_ip, INET_ADDRSTRLEN);
    syslog(LOG_DEBUG, "Accepted connection from %s", client_ip);

    char *textbuffer = (char*)calloc(1024, sizeof(char));
    size_t buffer_size = 1024;
    ssize_t bytes_received;

    // Open the driver for this thread
    int dev_fd = open(FILENAME, O_RDWR);
    if (dev_fd < 0) {
        syslog(LOG_ERR, "Could not open %s: %m", FILENAME);
        close(client_fd);
        free(textbuffer);
        free(arg);
        return NULL;
    }

    const char *ioctl_header = "AESDCHAR_IOCSEEKTO:";
    
    int ioctl_performed = FALSE; // Add this flag

    while ((bytes_received = recv(client_fd, textbuffer, buffer_size - 1, 0)) > 0) {
        textbuffer[bytes_received] = '\0';

        if (strncmp(textbuffer, ioctl_header, strlen(ioctl_header)) == 0) {
            struct aesd_seekto seekto;
            if (sscanf(textbuffer + strlen(ioctl_header), "%u,%u", 
                       &seekto.write_cmd, &seekto.write_cmd_offset) == 2) {
                
                if (ioctl(dev_fd, AESDCHAR_IOCSEEKTO, &seekto) == 0) {
                    ioctl_performed = TRUE; // Mark that we did a seek
                } else {
                    syslog(LOG_ERR, "IOCTL failed: %m");
                }
            }
        } else {
            pthread_mutex_lock(&fileFD);
            write(dev_fd, textbuffer, bytes_received);
            pthread_mutex_unlock(&fileFD);
        }

        if (strchr(textbuffer, '\n')) break;
    }

    if (!ioctl_performed) {
        // If no seek was commanded, we must rewind to read all data for Assignment 8/9 tests
        lseek(dev_fd, 0, SEEK_SET);
    }

    char read_buf[1024];
    ssize_t bytes_read;
    while ((bytes_read = read(dev_fd, read_buf, sizeof(read_buf))) > 0) {
        send(client_fd, read_buf, bytes_read, 0);
    }

    close(dev_fd);
    close(client_fd);
    free(textbuffer);
    free(arg);
    syslog(LOG_DEBUG, "Closed connection from %s", client_ip);
    return NULL;
}

void signal_handler(int sig, siginfo_t *si, void *uc) {
    (void)si; (void)uc;
    syslog(LOG_DEBUG, "Caught signal, exiting");
    if (socket_fd >= 0) close(socket_fd);
    closelog();
    exit(0);
}
