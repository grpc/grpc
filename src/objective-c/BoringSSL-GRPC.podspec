

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
  version = '0.0.33'
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
    :commit => "2cc7bbcf80c59a1ffe842eebaa68a8c949e118cd",
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
      H4sICAAAAAAC/2JvcmluZ3NzbF9wcmVmaXhfc3ltYm9scy5oALS9XXPbuJaofT+/wnXm5kzVrj2xE6fd
      751jKx1NHNsjKT2duWFREmRzhyIVgvJH//oDkJSIj7VArgW/VbtmOhafZ5EACIAgCPznf548iEJUaS3W
      J8vX4z+SZVllxYOUebKrxCZ7SR5FuhbVP+XjSVmcfGp+nc9vTlbldpvV/9/J2Wr123K52ly8W53/np5u
      NuLiw5kQyzT9eJFerH7/8Ls4Pb1Yrf/t3/7zP0+uyt1rlT081if/d/UfJ2fvTi/+cfJHWT7k4mRarP6p
      DtFH3Ytqm0mZqXh1ebKX4h8q2u71Hyfbcp1t1P9Pi/V/ltXJOpN1lS33tTipHzN5IstN/ZxW4mSjfkyL
      V+3a7atdKcXJc1arC6ia/1/u65ONECcKeRSV0FdfpYVKiH+c7KryKVurJKkf01r9H3GSLssnoU2r47kX
      ZZ2thD6LNu6uP9/DT7udSKuTrDhJ81yTmZCHq1t8mZzM7z4v/udyNjmZzk/uZ3d/Tq8n1yf/53Ku/v1/
      Ti5vr5uDLr8vvtzNTq6n86uby+m3+cnlzc2JomaXt4vpZK5d/zNdfDmZTf64nCnkTlHK17tvr26+X09v
      /2jA6bf7m6mK0gtO7j5rx7fJ7OqL+svlp+nNdPGjCf95uridzOf/VI6T27uTyZ+T28XJ/Iv2GGf2aXJy
      M738dDM5+az+dXn7Q+vm95Or6eXNP9R5zyZXi38oxeG/1EFXd7fzyX9/Vzp1zMn15bfLP/SJNPThn82F
      fblczO9U3Jm6vPn3m4W+jM+zu28nN3dzfeYn3+cTFeNycalplYbqlOf/UNxEneBMn/el+t/VYnp3q30K
      UKEXs0t9HreTP26mf0xuryaavWuAxd1MHft93jH/OLmcTec66N33habvtLMpwne3t5PmmDb1dXqoc2nO
      YjJTCfHtshF/tnPjn035/3Q3U051+ySX19fJ/WzyefrXyS6VtZAn9XN5oopeUWebTFRSFR5V+MtCqEyo
      dRFThXor9R+0KKv13apLXLk52aarqjwRL7u0aAqh+l9Wy5O0ethvlU+eLIWCRRNI3b3//Ld/X6s7uxDg
      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
      Xfekin3zVBdVq2waQDxB+Oc//g9ZV92E8dsEAA==
    EOF

    # PrivacyInfo.xcprivacy is not part of BoringSSL repo, inject it during pod installation
    base64 --decode $opts <<EOF | gunzip > src/PrivacyInfo.xcprivacy
      H4sICAAAAAAC/1ByaXZhY3lJbmZvLnhjcHJpdmFjeQC1kl9PwjAUxZ/Hp6h9Z1di/JsxAhskJAQXGQ8+
      Nt0VG7a1aRuw395OHUhE8UHflrNzzj2/pNHgpSrJBrURsu7TXnhOCdZcFqJe9ekyn3Rv6CDuRGfpfZI/
      ZmOiSmEsyZaj2TQhtAswVKpEgDRPSTabLnLiOwDGc0ros7XqDmC73YascYVcVo3RQKalQm3dzJd1fSAs
      bEH9mff2gzleLQS3cSeI1uji+SLTYsO4yzXja78ygkb2f59YaRC++BJZlsgtFimzLHcKzS7BtGYOvm1O
      ZcVEfdI+5ByNwWKYTY/U+4+gBQh+TrZBbzNW+wFHnQmzuJLaTUSJuajQWFapCD4SJ488IDNyDxV8mrm/
      m1z1rsPeYSnscaDl+RewhTMWq5GUtsH7Y7KLy8ntL8h2WqtE8PY0484rAb5xoDEDAAA=
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
