#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/types.h>
#include <unistd.h>

#define PORT 9000
#define BACKLOG 10
#define FILENAME "/var/tmp/aesdsocketdata"
#define FALSE 0
#define TRUE 1

typedef struct pthread_arg_t {
    int new_socket_fd;
    struct sockaddr_in client_address;
} pthread_arg_t;

int socket_fd = -1;
int file_fd = -1;
pthread_mutex_t fileFD;
atomic_bool timeStamp = FALSE;

void *pthread_routine(void *arg);

static void timerSetup();

static void tmpfileOpen();

void *fileWrite(char* textbuffer);

void signal_handler(int sig, siginfo_t *si, void *uc);

int main(int argc, char *argv[]) {
    int new_socket_fd; //port
    struct sockaddr_in address;
    pthread_attr_t pthread_attr;
    pthread_arg_t *pthread_arg;
    pthread_t pthread;
    socklen_t client_address_len;
    static int yes = 1;

    struct sigaction sa = { 0 };
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = signal_handler;

    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_port = htons(PORT);
    address.sin_addr.s_addr = INADDR_ANY;

    if ((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        syslog(LOG_ERR, "ERROR with socket %s", strerror(errno));
        exit(1);
    }
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1){

        syslog(LOG_ERR, "ERROR sock options");
        close(socket_fd);
        return -1;
    }
    if (bind(socket_fd, (struct sockaddr *)&address, sizeof address) == -1) {
        syslog(LOG_ERR, "ERROR with bind: %s", strerror(errno));
        exit(1);
    }

    if (listen(socket_fd, BACKLOG) == -1) {
        syslog(LOG_ERR, "ERROR with listen");
        exit(1);
    }

    if (pthread_attr_init(&pthread_attr) != 0) {
        syslog(LOG_ERR, "ERROR with pthread attribute initialization");
        exit(1);
    }
    if (pthread_attr_setdetachstate(&pthread_attr, PTHREAD_CREATE_DETACHED) != 0) {
        syslog(LOG_ERR, "ERROR with setting pthread state");
        exit(1);
    }
    if(pthread_mutex_init(&fileFD, NULL) != 0){ 
        syslog(LOG_ERR, "ERROR mutex init fail");
    }

    if (argc > 1){
        if (strcmp(argv[1], "-d") == 0){
            
            int pid = fork();

            if (pid == -1){
                syslog(LOG_ERR, "ERROR fork");
                close(socket_fd);
                return -1;
            }
            else if (pid != 0){
                close(socket_fd);
                exit(EXIT_SUCCESS);
            }
        }
        else {
            syslog(LOG_ERR, "ERROR Invalid argument specified, running in foreground");
            printf("ERROR Invalid argument specified, running in foreground\n");
        }
    }

    tmpfileOpen();
    timerSetup();

    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        syslog(LOG_ERR, "ERROR with signal: %s", strerror(errno));
        exit(1);
    }
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        syslog(LOG_ERR, "ERROR signal: %s", strerror(errno));
        exit(1);
    }
    if (sigaction(SIGRTMIN, &sa, NULL) == -1){
        syslog(LOG_ERR, "ERROR sigaction: %s", strerror(errno));
        exit(-1);
    }


    while (1) {

        if(timeStamp == TRUE){
            time_t rawtime;
            struct tm *info;
            char *buffer = (char*)calloc(31, sizeof(char));

            time(&rawtime);
            info = localtime(&rawtime);
            strftime(buffer,31,"timestamp:%F %H:%M:%S\n", info);

            pthread_mutex_lock(&fileFD);
            fileWrite(buffer); 
            pthread_mutex_unlock(&fileFD);
            syslog(LOG_DEBUG, "%s", buffer);

            free(buffer);           
            timeStamp = FALSE;
        }

        pthread_arg = (pthread_arg_t *)malloc(sizeof *pthread_arg);
        if (!pthread_arg) {
            syslog(LOG_ERR,"ERROR with pthread malloc");
            continue;
        }

        
        client_address_len = sizeof pthread_arg->client_address;
        new_socket_fd = accept(socket_fd, (struct sockaddr *)&pthread_arg->client_address, &client_address_len);
        if (new_socket_fd == -1) {
            syslog(LOG_ERR,"ERROR with accept");
            free(pthread_arg);
            continue;
        }

        pthread_arg->new_socket_fd = new_socket_fd;

        if (pthread_create(&pthread, &pthread_attr, pthread_routine, (void *)pthread_arg) != 0) {
            syslog(LOG_ERR,"ERROR with pthread_create");
            free(pthread_arg);
            continue;
        }
    }
    free(pthread_arg);
    return 0;
}

void *pthread_routine(void *arg) {
    pthread_arg_t *pthread_arg = (pthread_arg_t *)arg;
    int new_socket_fd = pthread_arg->new_socket_fd;
    struct sockaddr_in client_address = pthread_arg->client_address;
    
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_address.sin_addr), client_ip, INET_ADDRSTRLEN);
    syslog(LOG_DEBUG, "Accepted connection from %s", client_ip);


    free(arg);

    ssize_t bytes_read = 0;
    char *textbuffer = (char*)calloc(1024, sizeof(char));

    while ((bytes_read = recv(new_socket_fd, textbuffer, 1024, 0)) > 0) {   //Buffer set to 1kB
        if (bytes_read == -1){
            syslog(LOG_ERR, "ERROR with recv");
            raise(SIGINT);
        }
        
        tmpfileOpen();

        pthread_mutex_lock(&fileFD);
        fileWrite(textbuffer);

        if (textbuffer[bytes_read-1] == '\n') break;
    }
    if (lseek(file_fd, (off_t) 0, SEEK_SET) == (off_t) -1){ //Ch
        syslog(LOG_ERR, "ERROR with seek");
        raise(SIGINT);
    }

    int bytes_send = 0;
    while ((bytes_read = read(file_fd, textbuffer, 1024)) > 0){
        while ((bytes_send = send(new_socket_fd, textbuffer, bytes_read, 0)) < bytes_read){
            syslog(LOG_ERR, "ERROR with send");
            raise(SIGINT);
        }
    }
    pthread_mutex_unlock(&fileFD);  

    free(textbuffer);
    close(new_socket_fd);
    syslog(LOG_DEBUG, "Closed connection from %s", client_ip);
    return NULL;
}

void signal_handler(int sig, siginfo_t *si, void *uc) {
    (void)uc;

    if(si->si_code == SI_TIMER){
        timeStamp = TRUE;

    } else{ 

        if (file_fd >= 0 && close(file_fd)) syslog(LOG_ERR, "%s: %m", "Close file"); 
        if (socket_fd >= 0 && close(socket_fd)) syslog(LOG_ERR, "%s: %m", "Close server descriptor"); 
        if ((unlink(FILENAME)) == -1 ) syslog(LOG_ERR, "%s: %m", "Error deleting tmp file"); 
        if((sig == SIGINT) | (sig ==SIGTERM)){
            syslog(LOG_DEBUG,"%s", "Caught signal, exiting"); 
            exit(EXIT_SUCCESS);
        }

        exit(EXIT_FAILURE);
    }
}

void *fileWrite(char* textbuffer){
    if ((write(file_fd, textbuffer, strnlen(textbuffer, 1024))) == -1){
        syslog(LOG_ERR, "ERROR with write");
    }
    return(0);
}

static void timerSetup(){
    timer_t timerId = 0;

    struct sigevent sev = { 0 };
    struct itimerspec its = {   .it_interval.tv_sec  = 10,
                                .it_interval.tv_nsec = 0,
                                .it_value.tv_sec  = 1,
                                .it_value.tv_nsec = 0
                            };
    
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGRTMIN;

    if (timer_create(CLOCK_REALTIME, &sev, &timerId) != 0){
        syslog(LOG_ERR,"Error timer_create: %s\n", strerror(errno));
        exit(-1);
    }
        
    if (timer_settime(timerId, 0, &its, NULL) != 0){
        syslog(LOG_ERR,"Error timer_settime: %s\n", strerror(errno));
        exit(-1);
    }
    free(timerId);
}

static void tmpfileOpen(){
    if (file_fd < 0) {
        file_fd = open(FILENAME, O_CREAT | O_RDWR | O_APPEND, 0644);
        if (file_fd < 0) {
            syslog(LOG_ERR, "ERROR with file open");
            raise(SIGINT);
        }
    }
}
