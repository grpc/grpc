

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
  version = '0.0.37'
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
    :commit => "b8b3e6e11166719a8ebfa43c0cde9ad7d57a84f6",
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
                      'src/gen/crypto/err_data.c'

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
      H4sICAAAAAAC/2JvcmluZ3NzbF9wcmVmaXhfc3ltYm9scy5oALS9W3PbyJamfd+/QjFzMx2xo9uSyy7v
      746W6DLbsqQm6Zpy3yBAAhSxDQI0EpSl+vWTCYBAHtZKYK3UF7GjuyzifV4gz+f8z/+8eEyLtIrrNLnY
      vPT/iDZllRWPQuTRsUp32XO0T+Mkrf5D7C/K4uJj8+tqdXuxLQ+HrP7/LjYfNm/T9+nl5eX7979f/jP+
      kG528W9vt2+2SfrPOPk9efd7/OG33ft/+7f//M+L6/L4UmWP+/ri/2z//eLqzeWHf1z8UZaPeXqxKLb/
      IR9RTz2k1SETIpN+dXlxEuk/pNvx5R8XhzLJdvL/x0Xyn2V1kWSirrLNqU4v6n0mLkS5q3/FVXqxkz/G
      xYtiHU/VsRTpxa+slh9QNf+/PNUXuzS9kJJ9WqXq66u4kAHxj4tjVT5liQySeh/X8v+kF/GmfEoVadu/
      e1HW2TZVb9H6Hof3Pf90PKZxdZEVF3GeK2WWivPXrT/PL1b3n9b/d7acXyxWFw/L+z8XN/Obi/81W8l/
      /6+L2d1N89Ds2/rz/fLiZrG6vp0tvq4uZre3F1K1nN2tF/OVYv3fxfrzxXL+x2wpJfdSJXkD++769tvN
      4u6PRrj4+nC7kC4D4OL+k2J8nS+vP8u/zD4ubhfr7439p8X6br5a/YdkXNzdX8z/nN+tL1afFUd7s4/z
      i9vF7OPt/OKT/Nfs7rvCrR7m14vZ7T/key/n1+t/SMT5v+RD1/d3q/l/f5M4+czFzezr7A/1Io36/M/m
      wz7P1qt76buUn7f6drtWn/Fpef/14vZ+pd784ttqLj1m65lSyzCUr7z6h9TN5Qsu1XvP5P+u14v7O8WT
      Amm9Xs7Ue9zN/7hd/DG/u54r7X0jWN8v5bPfVp3mHxez5WKlTO+/rZX6XjGbJHx/dzdvnmlDX4WHfJfm
      LeZLGRBfZw34kxkb/9Gk/4/3S8mU2Sea3dxED8v5p8VfF8dY1Km4qH+VFzLpFXW2y9JKyMQjE39ZpDIS
      apXEZKI+CPUHBcpqlVtViit3F4d4W5UX6fMxLppEKP+X1eIirh5PB8kTF5tUitPGSObe//i3/53InF2k
      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
      SWWH6qnMiloNzwDiCcK///V/4K+mmdHmBAA=
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
