

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
  version = '0.0.40'
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
    :commit => "c57adcf6947912fe17bc5bfaf0876225d1fe742d",
  }

  s.ios.deployment_target = '15.0'
  s.osx.deployment_target = '11.0'
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
      Darwin) opts=""; BASE64_BIN=/usr/bin/base64 ;;
      *) opts="--ignore-garbage"; BASE64_BIN=base64 ;;
    esac

    base64 --decode $opts <<EOF | gunzip > src/include/openssl/boringssl_prefix_symbols.h
      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
      i3iTPafxclfU+wDiCcK///U/ggIARa9tBQA=
    EOF

    # PrivacyInfo.xcprivacy is not part of BoringSSL repo, inject it during pod installation
    "$BASE64_BIN" --decode $opts <<EOF | gunzip > src/PrivacyInfo.xcprivacy
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
