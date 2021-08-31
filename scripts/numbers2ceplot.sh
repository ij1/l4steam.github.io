#!/bin/bash

f="numbers.txt"

echo -n "BWd "
grep "20  *20" "$f" | \
cut -d ' ' -f 3- | \
awk '{printf("%s/%s ", $1, $2);}' | \
sed -e 's|/1 |/imm |g' -e 's|/2 |/reqgrant |g'

cat "$f" | \
awk '{
	if ($1 "." $2 != prev) {printf("\n%sMbps/%sms", $1, $2);}
	printf(" %s",$8);
	prev = $1 "." $2;
}'
echo ""
