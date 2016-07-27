#!/bin/sh
set -ev

./bootstrap.sh
./configure $*
make dist
