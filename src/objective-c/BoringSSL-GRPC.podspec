

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
    :commit => "70bb715d9266fee85984be895d6f5208eb018a76",
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
      X79WedkoewwgniD89S//B9Vc5ktBzAQA
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
