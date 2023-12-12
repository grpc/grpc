

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
  version = '0.0.31'
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
    :commit => "031b148bb941509a1ba878a62331df489afa766d",
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
      H4sICAAAAAAC/2JvcmluZ3NzbF9wcmVmaXhfc3ltYm9scy5oALS9XXPbuJaofT+/wnXm5kzVrpnY6aTd
      751iK4kmju0tyT2duWFRIiVzhyIVgvJH//oDkJSIj7VArgW/VbtmOpaeZ1EAiC+CwH/919k2LdIqrtPk
      bPV6+ke0Kqus2AqRR/sq3WQv0WMaJ2n1n+LxrCzOPjWfLhY3Z+tyt8vq/+/s3fvz1flvl6vVH7+df3j3
      R3y+ii9/v4w/Xrx/f55sfrv8I97Ev3/8mPzbv/3Xf51dlfvXKts+1mf/d/0fZxfvzi//cfalLLd5ejYr
      1v8pv6K+dZ9Wu0yITMary7ODSP8ho+1f/3G2K5NsI/9/XCT/VVZnSSbqKlsd6vSsfszEmSg39XNcpWcb
      +WFcvCrX/lDtS5GePWe1/AFV8//LQ322SdMziTymVap+fRUXMiH+cbavyqcskUlSP8a1/D/pWbwqn1Jl
      Wp+uvSjrbJ2qq2jj7vvrPX6036dxdZYVZ3GeKzJLxfHXLb9OzxZ3n5f/M5lPz2aLs/v53Z+z6+n12f+Z
      LOS//8/Z5Pa6+dLkYfn1bn52PVtc3Uxm3xdnk5ubM0nNJ7fL2XShXP8zW349m0+/TOYSuZOU9PXu26ub
      h+vZ7ZcGnH2/v5nJKL3g7O6zcnyfzq++yr9MPs1uZssfTfjPs+XtdLH4T+k4u707m/45vV2eLb4qj3Zl
      n6ZnN7PJp5vp2Wf5r8ntD6Vb3E+vZpObf8jrnk+vlv+QiuN/yS9d3d0upv98kDr5nbPryffJF3UhDX38
      Z/PDvk6WizsZdy5/3uLhZql+xuf53fezm7uFuvKzh8VUxpgsJ4qWaSgvefEPyU3lBc7VdU/k/66Ws7tb
      5ZOADL2cT9R13E6/3My+TG+vpoq9a4Dl3Vx+92HRMf84m8xnCxX07mGp6DvlbIrw3e3ttPlOm/oqPeS1
      NFcxncuE+D5pxJ/N3PjPpvx/uptLp7x9osn1dXQ/n36e/XW2j0WdirP6uTyTRa+os02WVkIWHln4yyKV
      mVCrIiYL9U6oPyhRVqu7VZW4cnO2i9dVeZa+7OOiKYTyf1ktzuJqe9hJnzhbpRJOm0Dy7v3Pf/v3RN7Z
      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
      3dUEAA==
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
