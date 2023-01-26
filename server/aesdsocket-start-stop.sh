#! /bin/sh

DAEMON=aesdsocket
NAME=aesdsocket
DAEMON_PATH= /usr/bin/aesdsocket

case "$1" in

start)
	echo "Starting ${DAEMON}"
	start-stop-daemon -S -o --d -n ${NAME} -a ${DAEMON_PATH}
	;;
stop)
	echo "Stopping ${DAEMONE}"
	start-stop-daemon -K -n ${NAME}
	;;
*)
	echo "Usage: $0 {start|stop}"
	exit 1
esac


exit 0
