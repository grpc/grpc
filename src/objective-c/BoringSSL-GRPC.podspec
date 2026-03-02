

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
    :commit => "781a72b2aa513bbbf01b9bc670b0495a6b115968",
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
      MisaZY8B1BOGf/7j/7f2jSs94QQA
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
