

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
    :commit => "342e805bc1f5dfdd650e3f031686d6c939b095d9",
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
      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
      fdu+qHxfv1R52Sh7DCCeIPz9b/8HQzlluJfOBAA=
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
