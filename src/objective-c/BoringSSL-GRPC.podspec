

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
  version = '0.0.33'
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
    :commit => "e14d29f68c2d1b02e06f10c83b9b8ea4d061f8df",
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
    ss.private_header_files = 'src/include/openssl/time.h'
    ss.source_files = 'src/include/openssl/*.h'
  end
  s.subspec 'Implementation' do |ss|
    ss.header_mappings_dir = 'src'

    ss.resource_bundles = {
      s.module_name => 'src/PrivacyInfo.xcprivacy'
    }

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
      H4sICAAAAAAC/2JvcmluZ3NzbF9wcmVmaXhfc3ltYm9scy5oALS9XXPbuLagfX9+hWvmZqZq1zmx08l2
      v3eKrXR04tjektLTmRsWJUE2dyhSISh/9K8fgKREfKwFci34rdp1TsfS8ywKAPFFEPiv/zp7EIWo0lps
      zlavp38kq7LKigcp82RfiW32kjyKdCOq/5SPZ2Vx9qn5dLG4OVuXu11W/39n4vy3zcXv24+X64vN+erd
      hXj3cXv+bn35fvX76lKkv23efTzfXm62//Ef//VfZ1fl/rXKHh7rs/+1/t9nF+/OL/9x9kdZPuTibFas
      /1N9RX/rXlS7TMpMxavLs4MU/1DR9q//ONuVm2yr/n9abP6rrM42mayrbHWoxVn9mMkzWW7r57QSZ1v1
      YVq8atf+UO1LKc6es1r9gKr5/+WhPtsKcaaQR1EJ/eurtFAJ8Y+zfVU+ZRuVJPVjWqv/I87SVfkktGl9
      uvairLO10FfRxt3313v8aL8XaXWWFWdpnmsyE/L465ZfpmeLu8/L/zOZT89mi7P7+d2fs+vp9dn/mCzU
      v//H2eT2uvnS5Pvyy9387Hq2uLqZzL4tziY3N2eKmk9ul7PpQrv+z2z55Ww+/WMyV8idopSvd99e3Xy/
      nt3+0YCzb/c3MxWlF5zdfdaOb9P51Rf1l8mn2c1s+aMJ/3m2vJ0uFv+pHGe3d2fTP6e3y7PFF+0xruzT
      9OxmNvl0Mz37rP41uf2hdYv76dVscvMPdd3z6dXyH0px/C/1pau728X0X9+VTn3n7HrybfKHvpCGPv6z
      +WFfJsvFnYo7Vz9v8f1mqX/G5/ndt7Obu4W+8rPvi6mKMVlONK3SUF3y4h+Km6oLnOvrnqj/XS1nd7fa
      pwAVejmf6Ou4nf5xM/tjens11exdAyzv5uq73xcd84+zyXy20EHvvi81faedTRG+u72dNt9pU1+nh7qW
      5iqmc5UQ3yaN+LOdG//ZlP9Pd3PlVLdPMrm+Tu7n08+zv872qayFPKufyzNV9Io622aikqrwqMJfFkJl
      Qq2LmCrUO6n/oEVZre9WXeLK7dkuXVflmXjZp0VTCNX/slqepdXDYad88mwlFCyaQOru/c//+J8bdWcX
      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
      1TQnlVblqUjzWtljAPEE4Z//+D8AMksHMdUEAA==
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
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
