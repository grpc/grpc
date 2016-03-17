#!/bin/sh

# Run this to produce the snipplet of data to insert in the Dockerfile.

echo 'RUN echo \\'
tar cz build | base64 | sed 's/$/\\/'
echo '| base64 -d | tar xzC /tmp'
