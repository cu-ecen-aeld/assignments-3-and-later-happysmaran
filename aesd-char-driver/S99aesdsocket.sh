#!/bin/sh

case "$1" in
    start)
        echo "Preparing environment for autograder..."
        
        # Create the tempfile alias at runtime
        # This ensures that even if the autograder pushes a broken script, 
        # the 'tempfile' command will work.
        ln -sf /bin/mktemp /usr/bin/tempfile

        # 2. Load the driver
        echo "Loading aesdchar driver..."
        /usr/bin/aesdchar_load
        
        # 3. Start the daemon
        echo "Starting aesdsocket..."
        start-stop-daemon -S -n aesdsocket -a /usr/bin/aesdsocket -- -d
        
        # 4. Synchronization Wait
        # GitHub Actions is slow; give the driver and socket time to init.
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
