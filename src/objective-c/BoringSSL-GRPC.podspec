

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
  version = '0.0.36'
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
    :commit => "16c8d3db1af20fcc04b5190b25242aadcb1fbb30",
  }

  s.ios.deployment_target = '10.0'
  s.osx.deployment_target = '10.12'
  s.tvos.deployment_target = '12.0'
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
                      'src/crypto/**/*.{h,c,cc}',
                      # We have to include fiat because spake25519 depends on it
                      'src/third_party/fiat/*.{h,c,cc}',
                      # Include the err_data.c pre-generated in boringssl's master-with-bazel branch
                      'src/gen/crypto/err_data.c'

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
      H4sICAAAAAAC/2JvcmluZ3NzbF9wcmVmaXhfc3ltYm9scy5oALS9XXPbuJaofT+/wnXm5kzVrpnY6WRn
      v3eKrXQ0cWxvSenpzA2LkiCLOxSpEJRj968/AEmJ+FgL5FrwW7VrpmPpeRYFgPgiCPzXf108ikJUaS02
      F6uX8z+SVVllxaOUeXKoxDZ7TnYi3YjqP+XuoiwuPjafLha3F+tyv8/q/+/i8v36w+btZnWZbq/ebNfr
      N7+t3l3+483q6t3Vb1dpulmvLrer1ds3//Zv//VfF9fl4aXKHnf1xf9d/8fF1ZvLD3+7+L0sH3NxMSvW
      /6m+or/1IKp9JmWm4tXlxVGKv6loh5e/XezLTbZV/z8tNv9VVhebTNZVtjrW4qLeZfJCltv6V1qJi636
      MC1etOtwrA6lFBe/slr9gKr5/+WxvtgKcaGQnaiE/vVVWqiE+NvFoSqfso1KknqX1ur/iIt0VT4JbVqf
      r70o62wt9FW0cQ/99Z4+OhxEWl1kxUWa55rMhDz9uuXn6cXi/tPyfybz6cVscfEwv/9jdjO9ufg/k4X6
      9/+5mNzdNF+afFt+vp9f3MwW17eT2dfFxeT29kJR88ndcjZdaNf/zJafL+bT3ydzhdwrSvl699317beb
      2d3vDTj7+nA7U1F6wcX9J+34Op1ff1Z/mXyc3c6W35vwn2bLu+li8Z/KcXF3fzH9Y3q3vFh81h7jyj5O
      L25nk4+304tP6l+Tu+9at3iYXs8mt39T1z2fXi//phSn/1Jfur6/W0z/+U3p1HcubiZfJ7/rC2no0z+b
      H/Z5slzcq7hz9fMW326X+md8mt9/vbi9X+grv/i2mKoYk+VE0yoN1SUv/qa4qbrAub7uifrf9XJ2f6d9
      ClChl/OJvo676e+3s9+nd9dTzd43wPJ+rr77bdExf7uYzGcLHfT+21LT99rZFOH7u7tp85029XV6qGtp
      rmI6VwnxddKIP9m58Z9N+f94P1dOdfskk5ub5GE+/TT78+KQylrIi/pXeaGKXlFn20xUUhUeVfjLQqhM
      qHURU4V6L/UftCir9d2qS1y5vdin66q8EM+HtGgKofpfVsuLtHo87pVPXqyEgkUTSN29//lv/75Rd3Yh
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
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
