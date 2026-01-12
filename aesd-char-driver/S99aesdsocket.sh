#!/bin/sh

case "$1" in
    start)
        echo "Loading aesdchar driver"
        # 1. Load the driver and create the device node /dev/aesdchar
        /usr/bin/aesdchar_load
        
        echo "Starting aesdsocket"
        # 2. Start the daemon
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
