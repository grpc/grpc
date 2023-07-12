

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
  version = '0.0.29'
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
    :commit => "14de8bccb14ebff8fb793bd6459ad55841af6866",
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
      H4sICAAAAAAC/2JvcmluZ3NzbF9wcmVmaXhfc3ltYm9scy5oALS9XXPbuJaofT+/wnXm5kzVrpnY6aTd
      751iK4kmju0tKT2duWFREmRzhyIVgnLs/vUHICkRH2uBXAt+q3bNdCw9z6IAEF8Egf/6r7MHUYgqrcXm
      bPVy+keyKquseJAyT/aV2GbPyaNIN6L6T/l4VhZnH5pPF4ubs3W522X1/3cmfnv/9vLtdn1+uXlzuRHb
      P96cry7E5l16/sdvF3+8e//HW/HHm3fr3//t3/7rv86uyv1LlT081mf/d/0fZxdvzi//cfapLB9ycTYr
      1v+pvqK/dS+qXSZlpuLV5dlBin+oaPuXf5ztyk22Vf8/LTb/VVZnm0zWVbY61OKsfszkmSy39a+0Emdb
      9WFavGjX/lDtSynOfmW1+gFV8//LQ322FeJMIY+iEvrXV2mhEuIfZ/uqfMo2Kknqx7RW/0ecpavySWjT
      +nTtRVlna6Gvoo2776/3+NF+L9LqLCvO0jzXZCbk8dctP0/PFncfl/8zmU/PZouz+/ndn7Pr6fXZ/5ks
      1L//z9nk9rr50uTb8vPd/Ox6tri6mcy+Ls4mNzdnippPbpez6UK7/me2/Hw2n36azBVypyjl6923Vzff
      rme3nxpw9vX+Zqai9IKzu4/a8XU6v/qs/jL5MLuZLb834T/OlrfTxeI/lePs9u5s+uf0dnm2+Kw9xpV9
      mJ7dzCYfbqZnH9W/JrfftW5xP72aTW7+oa57Pr1a/kMpjv+lvnR1d7uY/vOb0qnvnF1Pvk4+6Qtp6OM/
      mx/2ebJc3Km4c/XzFt9ulvpnfJzffT27uVvoKz/7tpiqGJPlRNMqDdUlL/6huKm6wLm+7on639Vydner
      fQpQoZfzib6O2+mnm9mn6e3VVLN3DbC8m6vvflt0zD/OJvPZQge9+7bU9J12NkX47vZ22nynTX2dHupa
      mquYzlVCfJ004o92bvxnU/4/3M2VU90+yeT6OrmfTz/O/jrbp7IW8qz+VZ6polfU2TYTlVSFRxX+shAq
      E2pdxFSh3kn9By3Kan236hJXbs926boqz8TzPi2aQqj+l9XyLK0eDjvlk2croWDRBFJ373/+27+r+zQr
      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
      P63L1Bd9yQQA
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
