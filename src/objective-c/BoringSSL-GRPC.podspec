

# This file has been automatically generated from a template file.
# Please make modifications to
# `templates/src/objective-c/BoringSSL-GRPC.podspec.template` instead. This
# file can be regenerated from the template by running
# `tools/buildgen/generate_projects.sh`.

# BoringSSL CocoaPods podspec

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

Pod::Spec.new do |s|
  s.name     = 'BoringSSL-GRPC'
  version = '0.0.30'
  s.version  = version
  s.summary  = 'BoringSSL is a fork of OpenSSL that is designed to meet Google\'s needs.'
  # Adapted from the homepage:
  s.description = <<-DESC
    BoringSSL is a fork of OpenSSL that is designed to meet Google's needs.

    Although BoringSSL is an open source project, it is not intended for general use, as OpenSSL is.
    We don't recommend that third parties depend upon it. Doing so is likely to be frustrating
    because there are no guarantees of API stability. Only the latest version of this pod is
    supported, and every new version is a new major version.

    We update Google libraries and programs that use BoringSSL as needed when deciding to make API
    changes. This allows us to mostly avoid compromises in the name of compatibility. It works for
    us, but it may not work for you.

    As a Cocoapods pod, it has the advantage over OpenSSL's pods that the library doesn't need to
    be precompiled. This eliminates the 10 - 20 minutes of wait the first time a user does "pod
    install", lets it be used as a dynamic framework (pending solution of Cocoapods' issue #4605),
    and works with bitcode automatically. It's also thought to be smaller than OpenSSL (which takes
    1MB - 2MB per ARM architecture), but we don't have specific numbers yet.

    BoringSSL arose because Google used OpenSSL for many years in various ways and, over time, built
    up a large number of patches that were maintained while tracking upstream OpenSSL. As Google's
    product portfolio became more complex, more copies of OpenSSL sprung up and the effort involved
    in maintaining all these patches in multiple places was growing steadily.

    Currently BoringSSL is the SSL library in Chrome/Chromium, Android (but it's not part of the
    NDK) and a number of other apps/programs.
  DESC
  s.homepage = 'https://github.com/google/boringssl'
  s.license  = { :type => 'Mixed', :file => 'LICENSE' }
  # "The name and email addresses of the library maintainers, not the Podspec maintainer."
  s.authors  = 'Adam Langley', 'David Benjamin', 'Matt Braithwaite'

  s.source = {
    :git => 'https://github.com/google/boringssl.git',
    :commit => "2ff4b968a7e0cfee66d9f151cb95635b43dc1d5b",
  }

  s.ios.deployment_target = '10.0'
  s.osx.deployment_target = '10.12'
  s.tvos.deployment_target = '12.0'
  s.watchos.deployment_target = '6.0'

  name = 'openssl_grpc'

  # When creating a dynamic framework, name it openssl.framework instead of BoringSSL.framework.
  # This lets users write their includes like `#include <openssl/ssl.h>` as opposed to `#include
  # <BoringSSL/ssl.h>`.
  s.module_name = name

  # When creating a dynamic framework, copy the headers under `include/openssl/` into the root of
  # the `Headers/` directory of the framework (i.e., not under `Headers/include/openssl`).
  #
  # TODO(jcanizales): Debug why this doesn't work on macOS.
  s.header_mappings_dir = 'src/include/openssl'

  # The above has an undesired effect when creating a static library: It forces users to write
  # includes like `#include <BoringSSL/ssl.h>`. `s.header_dir` adds a path prefix to that, and
  # because Cocoapods lets omit the pod name when including headers of static libraries, the
  # following lets users write `#include <openssl/ssl.h>`.
  s.header_dir = name

  # We don't need to inhibit all warnings; only -Wno-shorten-64-to-32. But Cocoapods' linter doesn't
  # want that for some reason.
  s.compiler_flags = '-DOPENSSL_NO_ASM', '-GCC_WARN_INHIBIT_ALL_WARNINGS', '-w', '-DBORINGSSL_PREFIX=GRPC'
  s.requires_arc = false

  # Like many other C libraries, BoringSSL has its public headers under `include/<libname>/` and its
  # sources and private headers in other directories outside `include/`. Cocoapods' linter doesn't
  # allow any header to be listed outside the `header_mappings_dir` (even though doing so works in
  # practice). Because we need our `header_mappings_dir` to be `include/openssl/` for the reason
  # mentioned above, we work around the linter limitation by dividing the pod into two subspecs, one
  # for public headers and the other for implementation. Each gets its own `header_mappings_dir`,
  # making the linter happy.
  s.subspec 'Interface' do |ss|
    ss.header_mappings_dir = 'src/include/openssl'
    ss.source_files = 'src/include/openssl/*.h'
  end
  s.subspec 'Implementation' do |ss|
    ss.header_mappings_dir = 'src'
    ss.source_files = 'src/ssl/*.{h,c,cc}',
                      'src/ssl/**/*.{h,c,cc}',
                      'src/crypto/*.{h,c,cc}',
                      'src/crypto/**/*.{h,c,cc}',
                      # We have to include fiat because spake25519 depends on it
                      'src/third_party/fiat/*.{h,c,cc}',
                      # Include the err_data.c pre-generated in boringssl's master-with-bazel branch
                      'err_data.c'

    ss.private_header_files = 'src/ssl/*.h',
                              'src/ssl/**/*.h',
                              'src/crypto/*.h',
                              'src/crypto/**/*.h',
                              'src/third_party/fiat/*.h'
    # bcm.c includes other source files, creating duplicated symbols. Since it is not used, we
    # explicitly exclude it from the pod.
    # TODO (mxyan): Work with BoringSSL team to remove this hack.
    ss.exclude_files = 'src/crypto/fipsmodule/bcm.c',
                       'src/**/*_test.*',
                       'src/**/test_*.*',
                       'src/**/test/*.*'

    ss.dependency "#{s.name}/Interface", version
  end

  s.pod_target_xcconfig = {
    # Do not let src/include/openssl/time.h override system API
    'USE_HEADERMAP' => 'NO',
  }

  s.prepare_command = <<-END_OF_COMMAND
    set -e

    # To avoid symbol conflict with OpenSSL, gRPC needs to rename all the BoringSSL symbols with a
    # prefix. This is done with BoringSSL's BORINGSSL_PREFIX mechanism
    # (https://github.com/google/boringssl/blob/75148d7abf12bdd1797fec3c5da9a21963703516/BUILDING.md#building-with-prefixed-symbols).
    # The required prefix header file boringssl_prefix_symbols.h is not part of BoringSSL repo at
    # this moment. It has to be generated by BoringSSL's users and be injected to BoringSSL build.
    # gRPC generates this file in script /tools/distrib/upgrade_boringssl_objc.sh. This script
    # outputs a gzip+base64 encoded version of boringssl_prefix_symbols.h because of Cocoapods'
    # limit on the 'prepare_command' field length. The encoded header is generated from
    # /src/boringssl/boringssl_prefix_symbols.h. Here we decode the content and inject the header to
    # the correct location in BoringSSL.
    case "$(uname)" in
      Darwin) opts="" ;;
           *) opts="--ignore-garbage" ;;
    esac
    base64 --decode $opts <<EOF | gunzip > src/include/openssl/boringssl_prefix_symbols.h
      H4sICAAAAAAC/2JvcmluZ3NzbF9wcmVmaXhfc3ltYm9scy5oALS9XXPbuJaofT+/wnXm5kzVrpnYaafd
      751iKx1NO7a3pPR05oZFSZDNHYpUCMqx+9cfgKREfKwFci34rdo107H0PIsCQHwRBP7rv84eRSGqtBab
      s9Xr6R/Jqqyy4lHKPNlXYpu9JE8i3YjqP+XTWVmcfWw+XSxuz9blbpfV/9/ZxXb7y+q3D1fpr+LdeivE
      hw+b37bnl+fr1W+XH95frn55v1mfby5X//Zv//VfZ9fl/rXKHp/qs/+7/o+zi3fnV/84+70sH3NxNivW
      /6m+or/1IKpdJmWm4tXl2UGKf6ho+9d/nO3KTbZV/z8tNv9VVmebTNZVtjrU4qx+yuSZLLf1z7QSZ1v1
      YVq8atf+UO1LKc5+ZrX6AVXz/8tDfaau9EwhT6IS+tdXaaES4h9n+6p8zjYqSeqntFb/R5ylq/JZaNP6
      dO1FWWdroa+ijbvvr/f40X4v0uosK87SPNdkJuTx1y0/T88W95+W/zOZT89mi7OH+f2fs5vpzdn/mSzU
      v//P2eTupvnS5Ovy8/387Ga2uL6dzL4szia3t2eKmk/ulrPpQrv+Z7b8fDaf/j6ZK+ReUcrXu++ub7/e
      zO5+b8DZl4fbmYrSC87uP2nHl+n8+rP6y+Tj7Ha2/NaE/zRb3k0Xi/9UjrO7+7Ppn9O75dnis/YYV/Zx
      enY7m3y8nZ59Uv+a3H3TusXD9Ho2uf2Huu759Hr5D6U4/pf60vX93WL6z69Kp75zdjP5MvldX0hDH//Z
      /LDPk+XiXsWdq5+3+Hq71D/j0/z+y9nt/UJf+dnXxVTFmCwnmlZpqC558Q/FTdUFzvV1T9T/rpez+zvt
      U4AKvZxP9HXcTX+/nf0+vbueava+AZb3c/Xdr4uO+cfZZD5b6KD3X5eavtfOpgjf391Nm++0qa/TQ11L
      cxXTuUqIL5NG/MnOjf9syv/H+7lyqtsnmdzcJA/z6afZX2f7VNZCntU/yzNV9Io622aikqrwqMJfFkJl
      Qq2LmCrUO6n/oEVZre9WXeLK7dkuXVflmXjZp0VTCNX/slqepdXjYad88mwlFCyaQOru/c9/+/eNurML
      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
      zwQA
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
