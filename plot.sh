#!/bin/bash

function extract_parameters()
{
  inbase="$(basename "$1" .txt)"
  from_width=$(echo $inbase | sed 's/[a-z]\+-[0-9]\+-\([0-9]\+\).*/\1/')
  from_height=$(echo $inbase | sed 's/[a-z]\+-[0-9]\+-[0-9]\+-\([0-9]\+\).*/\1/')
  outpath="results/$(basename "$inpath" .txt)".png
}

for inpath in results/*-samples.txt; do
  extract_parameters "$inpath"
  gnuplot -c gnuplot/plot-elapsed-size.plt "$inpath" "$outpath" "Resize from ${from_width}x${from_height}"
done

for inpath in results/*-average.txt; do
  extract_parameters "$inpath"
  gnuplot -c gnuplot/plot-averages.plt "$inpath" "$outpath" "Resize from ${from_width}x${from_height}"
done
