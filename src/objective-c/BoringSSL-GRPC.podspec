

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
    :commit => "97e64f3af03363ad5c550441fb3b4208b7239680",
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
      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
      APEE4f/+9T+wbSreFVoFAA==
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
