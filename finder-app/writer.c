#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>

int main(int arguments, char *argv[]) {
    openlog(NULL, 0, LOG_USER); // logs
    syslog(LOG_INFO, "Starting the C writer app.");

    if (arguments < 2) { // insufficient args
        syslog(LOG_ERR, "Not enough arguments were passed.");
        syslog(LOG_INFO, "Expected a full file path and text string as first and second arguments, respectively.");
        printf("Exited with error: %d", EXIT_FAILURE);

        return EXIT_FAILURE;
    }

    for (int i = 0; i < arguments; i++) { // no text
        if (strlen(argv[i]) == 0) {
            syslog(LOG_INFO, "No text provided. Exiting");
            return EXIT_FAILURE;
        }
    }

    const char* full_file_path = argv[1];
    const char* data = argv[2];

    syslog(LOG_DEBUG, "Writing %s to %s\n", data, full_file_path);

    int stream = open(full_file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644); // attempt to open
    if (stream == -1) { // if fail
        syslog(LOG_ERR, "Cannot open the file. Please check args.");
        close(stream);

        return stream;
    }

    int code = write(stream, data, strlen(data)); // attempt to write
    if (code == -1) { // if fail
        syslog(LOG_ERR, "Cannot write to the file. Please check permissions.");
        close(stream);

        return code;
    }

    close(stream); // never forget
    return EXIT_SUCCESS;
}