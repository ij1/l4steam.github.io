#/bin/bash

SCRIPTDIR=$(dirname $0)
TARGETDIR="compare"

DIR=$(pwd)
if [ ! "x${EXTRA_CC}" = "x" ]; then
	EXTRA_CC_TXT="${EXTRA_CC}_"
else
	EXTRA_CC_TXT=""
fi

for t in Prague; do
for b in 20 100; do
for d in 20 40 80; do
	cd ..
	"${SCRIPTDIR}/cdf_plot.sh" "$t" "$b" "$d" "${TARGETDIR}"
	"${SCRIPTDIR}/flowbal_plot.sh" "$t" "$b" "$d" "${TARGETDIR}"
	cd "${DIR}"

	pdfnup --nup 3x2 --outfile \
../${TARGETDIR}/${t}_${b}_${d}.pdf \
${t}DCTCPfbackimmediate_${t}DCTCPfbackreqgrant_${EXTRA_CC_TXT}${b}_${d}.pdf \
${t}AccECN-nooptackimmediate_${t}AccECN-nooptackreqgrant_${EXTRA_CC_TXT}${b}_${d}.pdf \
../${TARGETDIR}/delaycdf_${t}_${b}_${d}.pdf \
${t}AccECN-minoptackimmediate_${t}AccECN-minoptackreqgrant_${EXTRA_CC_TXT}${b}_${d}.pdf \
${t}AccECN-alwaysoptackimmediate_${t}AccECN-alwaysoptackreqgrant_${EXTRA_CC_TXT}${b}_${d}.pdf \
../${TARGETDIR}/flowbal_${t}_${b}_${d}.pdf

done
done
done

for t in Prague; do
pdfunite ../${TARGETDIR}/${t}_{20,100}_{20,40,80}.pdf ../${TARGETDIR}/${t}_all.pdf
done
