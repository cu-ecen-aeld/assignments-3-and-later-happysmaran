#!/bin/sh

case "$1" in
    start)
        echo "Loading aesdchar driver"
        # Make sure this path matches where Buildroot installs it!
        /usr/bin/aesdchar_load 
        
        echo "Starting aesdsocket"
        start-stop-daemon -S -n aesdsocket -a /usr/bin/aesdsocket -- -d
        ;;
    stop)
        echo "Stopping aesdsocket"
        start-stop-daemon -K -n aesdsocket
        /usr/bin/aesdchar_unload
        ;;
    *)
        echo "Usage: $0 {start|stop}"
    exit 1
esac

exit 0
