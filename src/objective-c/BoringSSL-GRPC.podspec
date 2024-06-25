

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
  version = '0.0.35'
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
    :commit => "b8a2bffc598f230484ff48a247526a9820facfc2",
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
      F6uX8z+SVVllxaOUeXKoxDZ7TnYi3YjqP+XuoiwuPjafLha3F+tyv8/q/+9i9SG9Wm2363f/+LC9evvm
      tw+/bbe/qb/99vd3V+/Tf3y4erNN19v11b/923/918V1eXipssddffF/1/9xcfXm8sPfLn4vy8dcXMyK
      9X+qr+hvPYhqn0mZqXh1eXGU4m8q2uHlbxf7cpNt1f9Pi81/ldXFJpN1la2Otbiod5m8kOW2/pVW4mKr
      PkyLF+06HKtDKcXFr6xWP6Bq/n95rC+2QlwoZCcqoX99lRYqIf52cajKp2yjkqTepbX6P+IiXZVPQpvW
      52svyjpbC30VbdxDf72njw4HkVYXWXGR5rkmMyFPv275eXqxuP+0/J/JfHoxW1w8zO//mN1Mby7+z2Sh
      /v1/LiZ3N82XJt+Wn+/nFzezxfXtZPZ1cTG5vb1Q1Hxyt5xNF9r1P7Pl54v59PfJXCH3ilK+3n13ffvt
      Znb3ewPOvj7czlSUXnBx/0k7vk7n15/VXyYfZ7ez5fcm/KfZ8m66WPynclzc3V9M/5jeLS8Wn7XHuLKP
      04vb2eTj7fTik/rX5O671i0eptezye3f1HXPp9fLvynF6b/Ul67v7xbTf35TOvWdi5vJ18nv+kIa+vTP
      5od9niwX9yruXP28xbfbpf4Zn+b3Xy9u7xf6yi++LaYqxmQ50bRKQ3XJi78pbqoucK6ve6L+d72c3d9p
      nwJU6OV8oq/jbvr77ez36d31VLP3DbC8n6vvflt0zN8uJvPZQge9/7bU9L12NkX4/u5u2nynTX2dHupa
      mquYzlVCfJ004k92bvxnU/4/3s+VU90+yeTmJnmYTz/N/rw4pLIW8qL+VV6oolfU2TYTlVSFRxX+shAq
      E2pdxFSh3kv9By3Kan236hJXbi/26boqL8TzIS2aQqj+l9XyIq0ej3vlkxcroWDRBFJ373/+279v1J1d
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
