#!/bin/bash

# Having a parent process is necessary because if the server binary is PID 1, it
# will not have a default SIGTERM handler installed and will therefore not respond
# well to termination events.

handler() {
  echo "Caught SIGTERM signal!"
  kill -TERM "$CHILD" 2>/dev/null
}

trap handler SIGTERM

/xds_interop_server &

CHILD=$!
wait "$CHILD"
