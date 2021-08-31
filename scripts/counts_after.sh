#!/bin/bash

if [ $# -lt 2 ]; then
	echo "Usage $0 filename seconds"
	exit 1
fi

f="$1"
s="$2"
f2="$(echo "$f" | sed -e 's|^qdelay_s|iperf_c|g' -e 's|\.qdelay|\.json|g')"

firstts=$(jq '.results[0]."ts-us"' < "$f")
startts=$(echo "$firstts+$s*1000*1000" | bc)

jq ".results[] | select(.\"ts-us\" > $startts) | .ce" < "$f" | wc -l
jq ".results[] | select(.\"ts-us\" > $startts) | .ce" < "$f" | grep "1" | wc -l

jq "[.intervals[].streams[0] | select(.end > $s)] | ((first | .delivered_ce), (last | .delivered_ce))" < "$f2" | \
tac | paste -s -d '-' | bc

exit 0
