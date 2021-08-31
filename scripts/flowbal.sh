#!/bin/bash

SCRIPTDIR=$(dirname $0)

ecns="DCTCPfb AccECN-noopt AccECN-minopt AccECN-alwaysopt"

AFTER=${AFTER:-60}

EXTRA_CC_TXT=""
if [ ! "x${EXTRA_CC}" = "x" ]; then
	EXTRA_CC_TXT="_${EXTRA_CC}"
fi

for e in $ecns; do 
	echo -n "$e "
	for s in 2 1; do
		f="qdelay_s${s}_${t}${e}ackimmediate_${t}${e}ackreqgrant${EXTRA_CC_TXT}_${b}_${d}.qdelay"
		${SCRIPTDIR}/counts_after.sh "$f" ${AFTER} | head -n1
	done | \
	paste -d '/' -s | \
	sed -e 's|^|scale=10;|g' | \
	bc
done
