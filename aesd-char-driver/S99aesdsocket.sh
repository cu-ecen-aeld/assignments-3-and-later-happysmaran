#!/bin/sh

case "$1" in
    start)
        echo "Preparing environment for autograder..."
        # 1. Fix the tempfile issue permanently at runtime
        if [ ! -e /usr/bin/tempfile ]; then
            ln -sf /bin/mktemp /usr/bin/tempfile
        fi

        # 2. Load the driver
        echo "Loading aesdchar driver..."
        /usr/bin/aesdchar_load
        
        # 3. Start the daemon
        echo "Starting aesdsocket..."
        start-stop-daemon -S -n aesdsocket -a /usr/bin/aesdsocket -- -d
        
        # 4. Give the system a moment to settle
        sleep 2
        ;;
    stop)
        echo "Stopping aesdsocket..."
        start-stop-daemon -K -n aesdsocket
        /usr/bin/aesdchar_unload
        ;;
    *)
        echo "Usage: $0 {start|stop}"
        exit 1
esac

exit 0
