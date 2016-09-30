#!/bin/bash

TMP=`tempfile`

echo $TMP
while true
do
   radamsa sample_data/* > $TMP
   $1 < $TMP
   test $? -gt 127 && break
done
 
