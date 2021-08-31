#!/bin/bash

SCRIPTDIR=$(dirname $0)

n=${n:-32}
b=${b:-100}
d=${d:-20}

EXTRA_CC_TXT=""
if [ ! "x${EXTRA_CC}" = "x" ]; then
	EXTRA_CC_TXT="_${EXTRA_CC}"
fi

ecns="\
DCTCPfb \
AccECN-noopt \
AccECN-minopt \
AccECN-alwaysopt \
"

echo -n "n "
for e in $ecns; do
	etxt=$(echo "$e" | sed -e 's|^.*ackimmediate_||g')
	for i in imm reqgrant; do
		echo -n "$etxt/$i "
	done
done
echo ""

final=""
for t in Prague; do
res=$(seq 0 $n | tr ' ' '\n' | sort -k 1b,1)
for e in $ecns; do
for s in 1 2; do
res=$(echo "$res" | join -e0 -a1 -j1 -o auto - <(${SCRIPTDIR}/ecn_after_blocks_n.sh qdelay_s${s}_${t}${e}ackimmediate_${t}${e}ackreqgrant${EXTRA_CC_TXT}_${b}_${d}.qdelay 60 "$n" | awk '{print $2,$1}' | sort -k '1b,1'))
done
done
res=$(echo "$res" | sed -e "s|^|$t |g")
final=$(echo -e "$final\n$res" | sort -k 2,2n -k1,1)
done
echo "$final" | sed -e 's| |/|1'
