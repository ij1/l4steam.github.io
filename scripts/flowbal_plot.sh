#!/bin/bash

SCRIPTDIR=$(dirname $0)

t="$1"
b="$2"
d="$3"
DST="$4"
AFTER=60

f="flowbal_${t}_${b}_${d}"

t="$t" b="$b" d="$d" ${SCRIPTDIR}/flowbal.sh > "${DST}/${f}.data"

cat << EOF | gnuplot
set terminal pdfcairo color dashed font "Sans,24" size 4.5in,10in
set output "$DST/${f}.pdf"

set key left Left top reverse
set xtics rotate by -45 out
set style data histogram
set style fill solid border

set title "${b}Mbps/${d}ms / ${t} CC"
set key bottom right

set yrange [0:*]
set ylabel "Flow balance (reqgrant pkts/imm pkts starting from 60s)"

plot '${DST}/${f}.data' using 2:xticlabels(1)
EOF
