#!/bin/bash

if [ $# -lt 2 ]; then
	echo "Usage $0 filename seconds"
	exit 1
fi

f="$1"
s="$2"

firstts=$(jq '.results[0]."ts-us"' < "$f")
startts=$(echo "$firstts+$s*1000*1000" | bc)

jq ".results[] | select(.\"ts-us\" > $startts) | .\"delay-us\"" < "$f"

exit 0
