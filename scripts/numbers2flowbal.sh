#!/bin/bash

f="numbers.txt"

echo -n "BWd "
grep "20  *20" "$f" | \
cut -d ' ' -f 3- | \
awk '{if ($2 == 2) {printf("%s ", $1);}}'

cat "$f" | \
awk '{
	if ($4 != 2) { next; }
	if ($1 "." $2 != prev) {printf("\n%sMbps/%sms", $1, $2);}
	printf(" %s",$10);
	prev = $1 "." $2;
}'
echo ""
