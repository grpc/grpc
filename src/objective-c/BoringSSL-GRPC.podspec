

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
  version = '0.0.32'
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
    :commit => "38314c88e85527ffc4ae0a7f642b6fd39777e0a9",
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
      H4sICAAAAAAC/2JvcmluZ3NzbF9wcmVmaXhfc3ltYm9scy5oALS9XXPbuJaofT+/wnXm5kzVrpnYaafd
      751jK4kmju0tKT2duWFREmRzhyIVgvJH//oDkJSIj7VArgW/VbtmOpaeZ1EAiC+CwH/918mDKESV1mJ9
      snw9/iNZllVWPEiZJ7tKbLKX5FGka1H9p3w8KYuTj82n8/nNyarcbrP6/zt5f/H+9LfVxYW4OD8/+32z
      Wf2Winfp75sPv50tP2zW7//4/fff1R/++Ld/+6//Orkqd69V9vBYn/zf1X+cnL07vfjHyeeyfMjFybRY
      /af6iv7Wvai2mZSZileXJ3sp/qGi7V7/cbIt19lG/f+0WP9XWZ2sM1lX2XJfi5P6MZMnstzUz2klTjbq
      w7R41a7dvtqVUpw8Z7X6AVXz/8t9fbIR4kQhj6IS+tdXaaES4h8nu6p8ytYqSerHtFb/R5yky/JJaNPq
      eO1FWWcroa+ijbvrr/fw0W4n0uokK07SPNdkJuTh1y2+TE7md58W/3M5m5xM5yf3s7s/p9eT65P/czlX
      //4/J5e3182XLr8vvtzNTq6n86uby+m3+cnlzc2JomaXt4vpZK5d/zNdfDmZTT5fzhRypyjl6923Vzff
      r6e3nxtw+u3+Zqqi9IKTu0/a8W0yu/qi/nL5cXozXfxown+aLm4n8/l/KsfJ7d3J5M/J7eJk/kV7jCv7
      ODm5mV5+vJmcfFL/urz9oXXz+8nV9PLmH+q6Z5OrxT+U4vBf6ktXd7fzyT+/K536zsn15bfLz/pCGvrw
      z+aHfblczO9U3Jn6efPvNwv9Mz7N7r6d3NzN9ZWffJ9PVIzLxaWmVRqqS57/Q3ETdYEzfd2X6n9Xi+nd
      rfYpQIVezC71ddxOPt9MP09uryaavWuAxd1Mfff7vGP+cXI5m8510LvvC03faWdThO9ubyfNd9rU1+mh
      rqW5islMJcS3y0b8yc6N/2zK/8e7mXKq2ye5vL5O7meTT9O/TnaprIU8qZ/LE1X0ijrbZKKSqvCowl8W
      QmVCrYuYKtRbqf+gRVmt71Zd4srNyTZdVeWJeNmlRVMI1f+yWp6k1cN+q3zyZCkULJpA6u79z3/797W6
      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
      Vb6vH6u8bJQ9BlBPGP75j/8DLvIDiRnfBAA=
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
