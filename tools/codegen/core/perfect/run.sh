#!/bin/bash
set -e
cd $(dirname $0)
fn=$1
shift
./perfect $* < $fn
