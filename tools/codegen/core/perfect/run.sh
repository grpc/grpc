#!/bin/bash
set -e
cd $(dirname $0)
gcc -o perfect perfect.c recycle.c lookupa.c perfhex.c 2> compile.txt
fn=$1
shift
./perfect $* < $fn &> hash.txt
