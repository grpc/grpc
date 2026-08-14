#! /usr/bin/env bash

set -o errexit
set -o pipefail
set -x

# sha256 from https://lists.freedesktop.org/archives/pkg-config/2017-March/001084.html
TARFILE=pkg-config-0.29.2.tar.gz
DIR=pkg-config-0.29.2
CHECKSUM=6fc69c01688c9458a57eb9a1664c9aba372ccda420a02bf4429fe610e7e7d591

cd /tmp

wget https://pkgconfig.freedesktop.org/releases/${TARFILE}
sha256sum ${TARFILE} | grep "${CHECKSUM}"

tar -xzvf ${TARFILE}
cd $DIR

./configure --prefix=/usr/local
make install

pkg-config --version
echo "OK"
