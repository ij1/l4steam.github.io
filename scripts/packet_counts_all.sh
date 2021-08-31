#!/bin/bash

SCRIPTDIR=$(dirname $0)

t="Prague"
ecns="DCTCPfb AccECN-noopt AccECN-alwaysopt"
#AccECN-minopt

EXTRA_CC_TXT=""
if [ ! "x${EXTRA_CC}" = "x" ]; then
	EXTRA_CC_TXT="${EXTRA_CC}_"
fi

for b in 20 100; do
for d in 20 40 80; do
for e in $ecns; do
for s in 1 2; do
	f="qdelay_s${s}_${t}${e}ackimmediate_${t}${e}ackreqgrant_${EXTRA_CC_TXT}${b}_${d}.qdelay"
	echo "$b $d $e $s" $(${SCRIPTDIR}/counts_after.sh "$f" 60)
done
done
done
done | \
awk '{printf("%s %.02f %.02f\n", $0,$7*1.0/$6,$6*1.0/$5);}' | \
awk '{
if ($4 == 2) {
	printf("%s %.02f\n", $0, $5*1.0/prev);
} else {
	print
};
prev=$5;
}'
