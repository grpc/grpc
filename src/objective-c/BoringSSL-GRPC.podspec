

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
  version = '0.0.42'
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
    :commit => "2b44a3701a4788e1ef866ddc7f143060a3d196c9",
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
  s.header_mappings_dir = 'include/openssl'

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
    ss.header_mappings_dir = 'include/openssl'
    ss.private_header_files = 'include/openssl/time.h'
    ss.source_files = 'include/openssl/*.h',
                      'include/openssl/**/*.h'
  end
  s.subspec 'Implementation' do |ss|
    ss.header_mappings_dir = 'src'

    ss.resource_bundles = {
      s.module_name => 'src/PrivacyInfo.xcprivacy'
    }

    ss.source_files = 'ssl/*.{h,c,cc}',
                      'ssl/**/*.{h,c,cc}',
                      'crypto/*.{h,c,cc}',
                      'crypto/**/*.{h,c,cc,inc}',
                      # We have to include fiat because spake25519 depends on it
                      'third_party/fiat/*.{h,c,cc}',
                      # Include the err_data.c pre-generated in boringssl's master-with-bazel branch
                      'gen/crypto/err_data.cc'

    ss.private_header_files = 'ssl/*.h',
                              'ssl/**/*.h',
                              'crypto/*.h',
                              'crypto/**/*.h',
                              'third_party/fiat/*.h'
    ss.exclude_files = './**/*_test.*',
                       './**/test_*.*',
                       './**/test/*.*'

    ss.dependency "#{s.name}/Interface", version
  end

  s.pod_target_xcconfig = {
    # Do not let include/openssl/time.h override system API
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
