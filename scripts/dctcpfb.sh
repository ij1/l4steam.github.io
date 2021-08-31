#!/bin/bash

SCRIPTDIR=$(dirname $0)

f="dump_e0_s2_PragueDCTCPfbackimmediate_PragueDCTCPfbackreqgrant_CubicDCTCPfbackimmediate_20_80.dump.bz2"

"${SCRIPTDIR}/tcpdump.sh" "$f" | \
grep -e '> 10.0.1.2.48802' | \
awk '{$1-=1627376676.509990; print}' | \
sed -e 's| IP .*Flags| Flags|g' | \
sed -e 's|, seq [0-9][0-9]*,|,|g' | \
grep -v 'Flags .[SR]' | \
sed -e 's| cksum [^,]*,||g' | \
sed -e 's|Flags [[]\.*E],|Flags E|g' | \
sed -e 's|^ *||g' | \
awk '
BEGIN {prevack=1; ce=0}
{
	ack=$5-prevack;
	if ($3 == "E") {
		if (ack <= 1448) {
			ce += 1
		} else {
			ce += ack/1448
		}
	}
	print ce, ack, $0;
	prevack=$5
}'
