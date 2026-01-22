

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
  version = '0.0.41'
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
    :commit => "c63fadbde60a2224c22189d14c4001bbd2a3a629",
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
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++17',
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
