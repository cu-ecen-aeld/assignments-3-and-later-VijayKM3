#!/bin/sh

# Script to start and stop the aesdsocket application using start-stop-daemon

# Name of the daemon
NAME="aesdsocket"

# Path to the daemon executable
DAEMON="/usr/bin/$NAME"

# Location of the PID file
PIDFILE="/var/run/$NAME.pid"

# Start function
start_daemon() {
    echo "Starting $NAME"
    start-stop-daemon --start --quiet --pidfile "$PIDFILE" --startas "$DAEMON" -- -d
    echo "$NAME started."
}

# Stop function
stop_daemon() {
    echo "Stopping $NAME"
    start-stop-daemon --stop --quiet --pidfile "$PIDFILE" --exec "$DAEMON" --signal TERM --retry 5
    echo "$NAME stopped."
}

case "$1" in
    start)
        start_daemon
        ;;
    stop)
        stop_daemon
        ;;
    restart)
        stop_daemon
        sleep 1
        start_daemon
        ;;
    status)
        start-stop-daemon --status --quiet --pidfile "$PIDFILE"
        exit $?
        ;;
    *)
        echo "Usage: $0 {start|stop|restart|status}"
        exit 1
        ;;
esac

exit 0

