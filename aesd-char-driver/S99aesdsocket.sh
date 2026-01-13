case "$1" in
    start)
        # 1. Load driver first
        /usr/bin/aesdchar_load
        
        # 2. Fix the tempfile deprecation (The "Fish" fix)
        ln -sf /bin/mktemp /usr/bin/tempfile
        
        # 3. Start the daemon ONLY after driver is ready
        start-stop-daemon -S -n aesdsocket -a /usr/bin/aesdsocket -- -d
        ;;
    stop)
        start-stop-daemon -K -n aesdsocket
        /usr/bin/aesdchar_unload
        ;;
    *)
        exit 1
esac
