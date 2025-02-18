

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
  version = '0.0.39'
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
    :commit => "c64b8fefbba9a9dadda73138062fc449bdf11e2a",
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
      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
      N9lzGi93Rb0PIJ4g/Ptf/wOLd/e7r20FAA==
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
