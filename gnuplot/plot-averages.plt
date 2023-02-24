#!/usr/bin/gnuplot

# Command line arguments

plot_input=ARG1
plot_output=ARG2
plot_heading=ARG3

# Variables, macros, subroutines

set macros

data_path="results/"
set loadpath "./gnuplot"

lastpoint="<awk '/./ {if (!last) print; last=$0} /^$/ {if (last) print last; print; last=$0}' "

linestyle="with lines title columnheader(1) ls IDX+1 lw 2"
pointoutlinestyle="with points ls IDX+1 lw 4 lc rgb '#000000' pt 7 notitle"
pointstyle="with points ls IDX+1 lw 2 pt 7 notitle"

# Program

load "common-elapsed-size.cfg";

set output plot_output;
set title plot_heading."\n{/*0.7 Pixels/s, higher is better}";
set xrange [-1:6];
plot plot_input u 0:2:(0.7):xticlabels(1) with boxes, '' u 0:2:3:4 w yerrorbars
