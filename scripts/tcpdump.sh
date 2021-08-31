#!/bin/bash

FILE="$1"
shift 1
IPERF=$(echo "$FILE" | sed -e 's|dump\(_e[0-9]\)*_|iperf_|g' -e 's|iperf_aqm_|iperf_c2_|g' -e 's|iperf_delay_|iperf_c2_|g' -e 's|iperf_s|iperf_c|g' -e 's|dump.bz2$|json|g')

PORT=$(jq .start.connected[]."local_port" < "$IPERF")

bzcat "$FILE" | \
~/src/tcpdump/tcpdump -n -tt -v "$@" -r - port "$PORT" 2> /dev/null | \
sed -e '/^[^ ]/ {N;s|\n *| |g}'
