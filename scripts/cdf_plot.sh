#!/bin/bash

SCRIPTDIR=$(dirname $0)

if [ $# -lt 4 ]; then
	echo "Usage: $0 tcp bw delay destionation_dir"
	exit 1
fi

t="$1"
b="$2"
d="$3"
DST="$4"
AFTER=60

if [ ! "x${EXTRA_CC}" = "x" ]; then
	EXTRA_CC_TXT="${EXTRA_CC}_"
else
	EXTRA_CC_TXT=""
fi


f="delaycdf_${t}_${b}_${d}.pdf"
echo "${f}"

(cat << EOF
set terminal pdfcairo color dashed font "Sans,24" size 9in,10in
set output "$DST/${f}"

set title "${b}Mbps/${d}ms / ${t} CC"
set key bottom right

set xlabel "Queuing delay (usecs)"
set xrange [0:3000]
set ylabel "CDF (starting from 60s)"

plot \\
EOF
SEP=""
for e in DCTCPfb AccECN-noopt AccECN-minopt AccECN-alwaysopt; do
	for s in 1 2; do
		stxt="imm"
		[ "$s" = "2" ] && stxt=reqgrant
	cat << EOF
	${SEP} \\
	"<${SCRIPTDIR}/delay_after.sh qdelay_s${s}_${t}${e}ackimmediate_${t}${e}ackreqgrant_${EXTRA_CC_TXT}${b}_${d}.qdelay $AFTER | sort -n | ${SCRIPTDIR}/cdf" \
		using 2:1 with lines lw 3 title "${e}/${stxt}" \\
EOF
	SEP=","
	done
done
echo ""
) | \
gnuplot
