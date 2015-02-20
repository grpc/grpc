#!/bin/bash

# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Where to put resulting .deb packages.
deb_dest="deb"
mkdir -p $deb_dest

# First use make to install gRPC to a temporary directory
#tmp_install_dir=`mktemp -d`
#echo "Installing gRPC C core to temp dir $tmp_install_dir"
#(cd ..; make install_c prefix=$tmp_install_dir)

# Build debian packages
for pkg_name in libgrpc libgrpc-dev
do
  echo
  echo "Building package $pkg_name"
  tmp_dir=`mktemp -d`
  echo "Using tmp dir $tmp_dir to build the package"

  cp -a templates/$pkg_name $tmp_dir

  if [ $pkg_name == "libgrpc" ]
  then
    # Copy libraries
    (cd ..; make install-static_c install-shared_c prefix=$tmp_dir/$pkg_name/usr)
  fi

  if [ $pkg_name == "libgrpc-dev" ]
  then
    # Copy headers
    (cd ..; make install-headers_c prefix=$tmp_dir/$pkg_name/usr)
  fi

  # Adjust mode for some files in the package
  find $tmp_dir/$pkg_name -type d | xargs chmod 755
  find $tmp_dir/$pkg_name -type f | xargs chmod 644
  chmod 755 $tmp_dir/$pkg_name/DEBIAN/{postinst,postrm}

  # Build the debian package
  fakeroot dpkg-deb --build $tmp_dir/$pkg_name || { echo "dpkg-deb failed"; exit 1; }

  # Copy the .deb file to destination dir
  cp $tmp_dir/$pkg_name.deb $deb_dest

  echo "Resulting package: $deb_dest/$pkg_name.deb"
  echo "Package info:"
  dpkg-deb -I $deb_dest/$pkg_name.deb
  echo "Package contents:"
  dpkg-deb -c $deb_dest/$pkg_name.deb
  echo "Problems reported by lintian:"
  lintian $deb_dest/$pkg_name.deb

  echo
done


