#!/bin/sh

case "$1" in
    start)
        echo "Loading aesdchar driver"
        # Use absolute path to the load script. at this point I have no clue what is going on eeeeeeeeeeeeeeeeeeeeeeee
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
