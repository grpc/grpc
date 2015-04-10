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
set -x
deb_dest="/tmp/deb_out"
mkdir -p $deb_dest

# Where the grpc disto is
grpc_root="/var/local/git/grpc"

# Update version from default values if the file /version.txt exists
#
# - when present, /version.txt will added by the docker build.
pkg_version='0.5.0'
if [ -f /version.txt ]; then
  pkg_version=$(cat /version.txt)
fi
version="${pkg_version}.0"
release_tag="release-${pkg_version//./_}"
echo "Target release => $pkg_version, will checkout tag $release_tag"

# Switch grpc_root to the release tag
pushd $grpc_root
git checkout $release_tag || { echo "bad release tag ${release_tag}"; exit 1; }
popd

if [ -f /.dockerinit ]; then
  # We're in Docker where uname -p returns "unknown".
  arch=x86_64
else
  arch=`uname -p`
fi

if [ $arch != "x86_64" ]
then
  echo Unsupported architecture.
  exit 1
fi

# Build debian packages
for pkg_name in libgrpc libgrpc-dev
do
  echo
  echo "Building package $pkg_name"
  tmp_dir=`mktemp -d`
  echo "Using tmp dir $tmp_dir to build the package"

  cp -a templates/$pkg_name $tmp_dir

  arch_lib_dir=$tmp_dir/$pkg_name/usr/lib/$arch-linux-gnu

  if [ $pkg_name == "libgrpc" ]
  then
    # Copy shared libraries
    pushd $grpc_root
    make install-shared_c prefix=$tmp_dir/$pkg_name/usr/lib
    popd
    mv $tmp_dir/$pkg_name/usr/lib/lib $arch_lib_dir

    # non-dev package should contain so.0 symlinks
    for symlink in $arch_lib_dir/*.so
    do
      mv $symlink $symlink.0
    done
  fi

  if [ $pkg_name == "libgrpc-dev" ]
  then
    # Copy headers and static libraries
    pushd $grpc_root
    make install-headers_c install-static_c prefix=$tmp_dir/$pkg_name/usr/lib
    popd

    mv $tmp_dir/$pkg_name/usr/lib/include $tmp_dir/$pkg_name/usr/include
    mv $tmp_dir/$pkg_name/usr/lib/lib $arch_lib_dir

    # create symlinks to shared libraries
    for libname in $arch_lib_dir/*.a
    do
      base=`basename $libname .a`
      ln -s $base.so.$version $arch_lib_dir/$base.so
    done
  fi

  # Adjust mode for some files in the package
  find $tmp_dir/$pkg_name -type d | xargs chmod 755
  find $tmp_dir/$pkg_name -type d | xargs chmod a-s
  find $tmp_dir/$pkg_name -type f | xargs chmod 644
  chmod 755 $tmp_dir/$pkg_name/DEBIAN/{postinst,postrm}

  # Build the debian package
  fakeroot dpkg-deb --build $tmp_dir/$pkg_name || { echo "dpkg-deb failed"; exit 1; }

  deb_path=$deb_dest/${pkg_name}_${pkg_version}_amd64.deb

  # Copy the .deb file to destination dir
  cp $tmp_dir/$pkg_name.deb $deb_path

  echo "Resulting package: $deb_path"
  echo "Package info:"
  dpkg-deb -I $deb_path
  echo "Package contents:"
  dpkg-deb -c $deb_path
  echo "Problems reported by lintian:"
  lintian $deb_path
  echo
done
