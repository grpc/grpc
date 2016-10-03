#!/bin/bash

# Run this from the top directory of nanopb tree.
# e.g. user@localhost:~/nanopb$ tools/set_version.sh nanopb-0.1.9-dev
# It sets the version number in pb.h and generator/nanopb_generator.py.

sed -i -e 's/nanopb_version\s*=\s*"[^"]*"/nanopb_version = "'$1'"/' generator/nanopb_generator.py
sed -i -e 's/#define\s*NANOPB_VERSION\s*.*/#define NANOPB_VERSION '$1'/' pb.h


