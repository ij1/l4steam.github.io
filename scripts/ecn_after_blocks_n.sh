#!/bin/bash

if [ $# -lt 3 ]; then
	echo "Usage $0 filename seconds blocksize"
	exit 1
fi

f="$1"
s="$2"
n="$3"

firstts=$(jq '.results[0]."ts-us"' < "$f")
startts=$(echo "$firstts+$s*1000*1000" | bc)

jq ".results[] | select(.\"ts-us\" > $startts) | .ce" < "$f" | \
pr -T -a -w "$((n*10))" -"$n" | sed -e 's|[\t ][\t ]*|+|g' | bc | sort -n | uniq -c

exit 0
