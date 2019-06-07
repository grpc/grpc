#!/bin/bash

VERSION_REGEX="v3.8.*"
REPO="python/cpython"

LATEST=$(curl -s https://api.github.com/repos/$REPO/tags | \
          jq -r '.[] | select(.name|test("'$VERSION_REGEX'")) | .name' \
          | sort | tail -n1)

wget https://github.com/$REPO/archive/$LATEST.tar.gz
tar xzvf *.tar.gz
( cd cpython*
  ./configure
  make install
)
