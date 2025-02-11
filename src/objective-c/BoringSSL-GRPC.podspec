

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
  version = '0.0.38'
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
    :commit => "dec0d8f681348af8bb675e07bd89989665fca8bc",
  }

  s.ios.deployment_target = '11.0'
  s.osx.deployment_target = '10.14'
  s.tvos.deployment_target = '13.0'
  s.watchos.deployment_target = '6.0'
  s.visionos.deployment_target = '1.0'

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
  s.compiler_flags = '-DOPENSSL_NO_ASM', '-w', '-DBORINGSSL_PREFIX=GRPC'
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
    ss.private_header_files = 'src/include/openssl/time.h'
    ss.source_files = 'src/include/openssl/*.h',
                      'src/include/openssl/**/*.h'
  end
  s.subspec 'Implementation' do |ss|
    ss.header_mappings_dir = 'src'

    ss.resource_bundles = {
      s.module_name => 'src/PrivacyInfo.xcprivacy'
    }

    ss.source_files = 'src/ssl/*.{h,c,cc}',
                      'src/ssl/**/*.{h,c,cc}',
                      'src/crypto/*.{h,c,cc}',
                      'src/crypto/**/*.{h,c,cc,inc}',
                      # We have to include fiat because spake25519 depends on it
                      'src/third_party/fiat/*.{h,c,cc}',
                      # Include the err_data.c pre-generated in boringssl's master-with-bazel branch
                      'src/gen/crypto/err_data.cc'

    ss.private_header_files = 'src/ssl/*.h',
                              'src/ssl/**/*.h',
                              'src/crypto/*.h',
                              'src/crypto/**/*.h',
                              'src/third_party/fiat/*.h'
    ss.exclude_files = 'src/**/*_test.*',
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
      H4sICAAAAAAC/2JvcmluZ3NzbF9wcmVmaXhfc3ltYm9scy5oALS923LbyJaue99PoVjrZq+IGd2WXPJU
      7TtZosvqsiVNUq6uWjcIkEiKmAIBGgnqUE+/MwGQyMMYCYyR2hEzussi/u8H8nzO//qvk0dRijptRHay
      fDv+I1lWdV4+Slkku1qs89dkI9JM1P8pNydVefK5/XWx+HayqrbbvPl/TzKx+pBdrD9dnH785SJdXyyX
      n/55Lj78c5ld/Prrxa+fPp2vV+nFcvUf//Ff/3VyVe3e6vxx05ycfTi9OHnYCIN4uW82VS3Vc/rRe1Fv
      cylzZdpUJ3sp/qEsd2//ONlWWb5W/z8ts/+q6pMsl02dL/eNOGk2uTyR1bp5SWtxslY/puWbZu329a6S
      4uQlb9RX1O3/r/bNyVqIEyXZiFroIKjTUoXGP052dfWcZypcmk3aqP8jTtJl9Sw0aXX8gLJq8pXQb9H5
      7ob3Pfy024m0PsnLk7QotDIX8j/7r3v4OjtZ3H15+J/L+ezkZnFyP7/74+Z6dn3yvy4X6t//6+Ty9rp9
      6PLHw9e7+cn1zeLq2+XN98XJ5bdvJ0o1v7x9uJktNOt/bh6+nsxnv13OleROqRRvYN9efftxfXP7Wyu8
      +X7/7Ua5DICTuy+a8X02v/qq/nL5+ebbzcNfrf2Xm4fb2WLxn4pxcnt3MvtjdvtwsviqOcabfZ6dfLu5
      /PxtdvJF/evy9i+NW9zPrm4uv/1Dvfd8dvXwD4U4/Jd66OrudjH71w+FU8+cXF9+v/xNv0irPvyz/bCv
      lw+LO+U7V5+3+PHtQX/Gl/nd95Nvdwv95ic/FjPlcflwqdUqDNUrL/6hdDP1gnP93pfqf1cPN3e3mqcE
      yvphfqnf43b227eb32a3VzOtvWsFD3dz9eyPRa/5x8nl/GahTe9+PGj1nWa26fju9nbWPtOFvg4P9S7t
      W8zmKiC+X7bgL3Zs/GebCT7fzRVTpfjk8vo6uZ/Pvtz8ebJLZSPkSfNSnaikVzb5Ohe1VIlHJf6qFCoS
      Gp3EVKLeSv0HDcobnWV1iqvWJ9t0VVcn4nWXlm0iVP/LG3mS1o/7reLJk6VQYtEaqQz3n//xvzOVvUsB
      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
      f31dCnEaWgUA
    EOF

    # PrivacyInfo.xcprivacy is not part of BoringSSL repo, inject it during pod installation
    base64 --decode $opts <<EOF | gunzip > src/PrivacyInfo.xcprivacy
      H4sICAAAAAAC/1ByaXZhY3lJbmZvLnhjcHJpdmFjeQCFUU1PwjAYPo9fUXtnr7uoMWMEN0iWEFykHDw2
      3Ss2dGvTNuD+vUWdk4hya54+n3nT6VujyB6tk7qd0CS+pgRboWvZbid0wxbjOzrNRulV8Ziz52pOjJLO
      k2rzsCxzQscAM2MUAhSsINWyXDMSPADmK0roq/fmHuBwOMT8yIqFbo5EB5XVBq3vlsFsHARx7WsaYj7d
      T+oEtJbCZ6Mo3WGXrdaVlXsuOma52IWWKRzh8PvClUP4xcu1Uig81gX3nHUG3beCW8s7+NO50A2X7UX6
      TAh0DutZVZ6xD4+oHxD9r+yFgea8DQXOMnPucattt5AKmWzQed6YFL4UF0OekDs9jIp+1Bxy85vkNk5O
      TWGYA/1BeqxHUvg4YDZ6B1ry6jZXAgAA
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' -or -path '*.inc' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' -or -path '*.inc' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
