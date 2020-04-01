

# This file has been automatically generated from a template file.
# Please make modifications to
# `templates/src/objective-c/BoringSSL-GRPC.podspec.template` instead. This
# file can be regenerated from the template by running
# `tools/buildgen/generate_projects.sh`. Because of some limitations of this
# template, you might actually need to run the same script twice in a row.
# (see err_data.c section)

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
  version = '0.0.6'
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
    :commit => "7f02881e96e51f1873afcf384d02f782b48967ca",
  }

  s.ios.deployment_target = '7.0'
  s.osx.deployment_target = '10.7'
  s.tvos.deployment_target = '10.0'
  s.watchos.deployment_target = '4.0'

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

  # The module map and umbrella header created automatically by Cocoapods don't work for C libraries
  # like this one. The following file, and a correct umbrella header, are created on the fly by the
  # `prepare_command` of this pod.
  s.module_map = 'include/openssl/BoringSSL.modulemap'

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
    ss.header_mappings_dir = 'include/openssl'
    ss.source_files = 'include/openssl/*.h'
  end
  s.subspec 'Implementation' do |ss|
    ss.header_mappings_dir = '.'
    ss.source_files = 'ssl/*.{h,c,cc}',
                      'ssl/**/*.{h,c,cc}',
                      'crypto/*.{h,c,cc}',
                      'crypto/**/*.{h,c,cc}',
                      # We have to include fiat because spake25519 depends on it
                      'third_party/fiat/*.{h,c,cc}',
                      # Include the err_data.c generated in prepare_command below
                      'err_data.c'

    ss.private_header_files = 'ssl/*.h',
                              'ssl/**/*.h',
                              'crypto/*.h',
                              'crypto/**/*.h',
                              'third_party/fiat/*.h'
    # bcm.c includes other source files, creating duplicated symbols. Since it is not used, we
    # explicitly exclude it from the pod.
    # TODO (mxyan): Work with BoringSSL team to remove this hack.
    ss.exclude_files = 'crypto/fipsmodule/bcm.c',
                       '**/*_test.*',
                       '**/test_*.*',
                       '**/test/*.*'

    ss.dependency "#{s.name}/Interface", version
  end

  s.prepare_command = <<-END_OF_COMMAND
    # Add a module map and an umbrella header
    cat > include/openssl/umbrella.h <<EOF
      #include "ssl.h"
      #include "crypto.h"
      #include "aes.h"
      /* The following macros are defined by base.h. The latter is the first file included by the    
         other headers. */    
      #if defined(OPENSSL_ARM) || defined(OPENSSL_AARCH64)    
      #  include "arm_arch.h"   
      #endif
      #include "asn1.h"
      #include "asn1_mac.h"
      #include "asn1t.h"
      #include "blowfish.h"
      #include "cast.h"
      #include "chacha.h"
      #include "cmac.h"
      #include "conf.h"
      #include "cpu.h"
      #include "curve25519.h"
      #include "des.h"
      #include "dtls1.h"
      #include "hkdf.h"
      #include "md4.h"
      #include "md5.h"
      #include "obj_mac.h"
      #include "objects.h"
      #include "opensslv.h"
      #include "ossl_typ.h"
      #include "pkcs12.h"
      #include "pkcs7.h"
      #include "pkcs8.h"
      #include "poly1305.h"
      #include "rand.h"
      #include "rc4.h"
      #include "ripemd.h"
      #include "safestack.h"
      #include "srtp.h"
      #include "x509.h"
      #include "x509v3.h"
    EOF
    cat > include/openssl/BoringSSL.modulemap <<EOF
      framework module openssl {
        umbrella header "umbrella.h"
        textual header "arm_arch.h"
        export *
        module * { export * }
      }
    EOF

    # To build boringssl, we need the generated file err_data.c, which is normally generated
    # by boringssl's err_data_generate.go, but we already have a copy of err_data.c checked into the
    # grpc/grpc repository that gets regenerated whenever we update the third_party/boringssl submodule.
    # To make the podspec independent of the grpc repository, the .podspec.template just copies
    # the contents of err_data.c directly into the .podspec.
    # TODO(jtattermusch): avoid needing to run tools/buildgen/generate_projects.sh twice on update
    # TODO(jtattermusch): another pre-generated copy of err_data.c is under third_party/boringssl-with-bazel
    # investigate if we could use it.
    cat > err_data.c <<EOF
      /* Copyright (c) 2015, Google Inc.
       *
       * Permission to use, copy, modify, and/or distribute this software for any
       * purpose with or without fee is hereby granted, provided that the above
       * copyright notice and this permission notice appear in all copies.
       *
       * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
       * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
       * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
       * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
       * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
       * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
       * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. */
      
       /* This file was generated by err_data_generate.go. */
      
      #include <openssl/base.h>
      #include <openssl/err.h>
      #include <openssl/type_check.h>
      
      
      OPENSSL_STATIC_ASSERT(ERR_LIB_NONE == 1, "library value changed");
      OPENSSL_STATIC_ASSERT(ERR_LIB_SYS == 2, "library value changed");
      OPENSSL_STATIC_ASSERT(ERR_LIB_BN == 3, "library value changed");
      OPENSSL_STATIC_ASSERT(ERR_LIB_RSA == 4, "library value changed");
      OPENSSL_STATIC_ASSERT(ERR_LIB_DH == 5, "library value changed");
      OPENSSL_STATIC_ASSERT(ERR_LIB_EVP == 6, "library value changed");
      OPENSSL_STATIC_ASSERT(ERR_LIB_BUF == 7, "library value changed");
      OPENSSL_STATIC_ASSERT(ERR_LIB_OBJ == 8, "library value changed");
      OPENSSL_STATIC_ASSERT(ERR_LIB_PEM == 9, "library value changed");
      OPENSSL_STATIC_ASSERT(ERR_LIB_DSA == 10, "library value changed");
      OPENSSL_STATIC_ASSERT(ERR_LIB_X509 == 11, "library value changed");
      OPENSSL_STATIC_ASSERT(ERR_LIB_ASN1 == 12, "library value changed");
      OPENSSL_STATIC_ASSERT(ERR_LIB_CONF == 13, "library value changed");
      OPENSSL_STATIC_ASSERT(ERR_LIB_CRYPTO == 14, "library value changed");
      OPENSSL_STATIC_ASSERT(ERR_LIB_EC == 15, "library value changed");
      OPENSSL_STATIC_ASSERT(ERR_LIB_SSL == 16, "library value changed");
      OPENSSL_STATIC_ASSERT(ERR_LIB_BIO == 17, "library value changed");
      OPENSSL_STATIC_ASSERT(ERR_LIB_PKCS7 == 18, "library value changed");
      OPENSSL_STATIC_ASSERT(ERR_LIB_PKCS8 == 19, "library value changed");
      OPENSSL_STATIC_ASSERT(ERR_LIB_X509V3 == 20, "library value changed");
      OPENSSL_STATIC_ASSERT(ERR_LIB_RAND == 21, "library value changed");
      OPENSSL_STATIC_ASSERT(ERR_LIB_ENGINE == 22, "library value changed");
      OPENSSL_STATIC_ASSERT(ERR_LIB_OCSP == 23, "library value changed");
      OPENSSL_STATIC_ASSERT(ERR_LIB_UI == 24, "library value changed");
      OPENSSL_STATIC_ASSERT(ERR_LIB_COMP == 25, "library value changed");
      OPENSSL_STATIC_ASSERT(ERR_LIB_ECDSA == 26, "library value changed");
      OPENSSL_STATIC_ASSERT(ERR_LIB_ECDH == 27, "library value changed");
      OPENSSL_STATIC_ASSERT(ERR_LIB_HMAC == 28, "library value changed");
      OPENSSL_STATIC_ASSERT(ERR_LIB_DIGEST == 29, "library value changed");
      OPENSSL_STATIC_ASSERT(ERR_LIB_CIPHER == 30, "library value changed");
      OPENSSL_STATIC_ASSERT(ERR_LIB_HKDF == 31, "library value changed");
      OPENSSL_STATIC_ASSERT(ERR_LIB_USER == 32, "library value changed");
      OPENSSL_STATIC_ASSERT(ERR_NUM_LIBS == 33, "number of libraries changed");
      
      const uint32_t kOpenSSLReasonValues[] = {
          0xc32083a,
          0xc328854,
          0xc330863,
          0xc338873,
          0xc340882,
          0xc34889b,
          0xc3508a7,
          0xc3588c4,
          0xc3608e4,
          0xc3688f2,
          0xc370902,
          0xc37890f,
          0xc38091f,
          0xc38892a,
          0xc390940,
          0xc39894f,
          0xc3a0963,
          0xc3a8847,
          0xc3b00ea,
          0xc3b88d6,
          0x10320847,
          0x1032959f,
          0x103315ab,
          0x103395c4,
          0x103415d7,
          0x10348f27,
          0x10350c60,
          0x103595ea,
          0x10361614,
          0x10369627,
          0x10371646,
          0x1037965f,
          0x10381674,
          0x10389692,
          0x103916a1,
          0x103996bd,
          0x103a16d8,
          0x103a96e7,
          0x103b1703,
          0x103b971e,
          0x103c1744,
          0x103c80ea,
          0x103d1755,
          0x103d9769,
          0x103e1788,
          0x103e9797,
          0x103f17ae,
          0x103f97c1,
          0x10400c24,
          0x104097d4,
          0x104117f2,
          0x10419805,
          0x1042181f,
          0x1042982f,
          0x10431843,
          0x10439859,
          0x10441871,
          0x10449886,
          0x1045189a,
          0x104598ac,
          0x104605fd,
          0x1046894f,
          0x104718c1,
          0x104798d8,
          0x104818ed,
          0x104898fb,
          0x10490e73,
          0x10499735,
          0x104a15ff,
          0x14320c07,
          0x14328c15,
          0x14330c24,
          0x14338c36,
          0x143400ac,
          0x143480ea,
          0x18320083,
          0x18328f7d,
          0x183300ac,
          0x18338f93,
          0x18340fa7,
          0x183480ea,
          0x18350fbc,
          0x18358fd4,
          0x18360fe9,
          0x18368ffd,
          0x18371021,
          0x18379037,
          0x1838104b,
          0x1838905b,
          0x18390a75,
          0x1839906b,
          0x183a1091,
          0x183a90b7,
          0x183b0c7f,
          0x183b9106,
          0x183c1118,
          0x183c9123,
          0x183d1133,
          0x183d9144,
          0x183e1155,
          0x183e9167,
          0x183f1190,
          0x183f91a9,
          0x184011c1,
          0x184086d5,
          0x184110da,
          0x184190a5,
          0x184210c4,
          0x18428c6c,
          0x18431080,
          0x184390ec,
          0x203211fb,
          0x203291e8,
          0x24321207,
          0x24328995,
          0x24331219,
          0x24339226,
          0x24341233,
          0x24349245,
          0x24351254,
          0x24359271,
          0x2436127e,
          0x2436928c,
          0x2437129a,
          0x243792a8,
          0x243812b1,
          0x243892be,
          0x243912d1,
          0x28320c54,
          0x28328c7f,
          0x28330c24,
          0x28338c92,
          0x28340c60,
          0x283480ac,
          0x283500ea,
          0x28358c6c,
          0x2c322f0c,
          0x2c3292e8,
          0x2c332f1a,
          0x2c33af2c,
          0x2c342f40,
          0x2c34af52,
          0x2c352f6d,
          0x2c35af7f,
          0x2c362f92,
          0x2c36832d,
          0x2c372f9f,
          0x2c37afb1,
          0x2c382fd6,
          0x2c38afed,
          0x2c392ffb,
          0x2c39b00b,
          0x2c3a301d,
          0x2c3ab031,
          0x2c3b3042,
          0x2c3bb061,
          0x2c3c12fa,
          0x2c3c9310,
          0x2c3d3075,
          0x2c3d9329,
          0x2c3e3092,
          0x2c3eb0a0,
          0x2c3f30b8,
          0x2c3fb0d0,
          0x2c4030fa,
          0x2c4091fb,
          0x2c41310b,
          0x2c41b11e,
          0x2c4211c1,
          0x2c42b12f,
          0x2c430722,
          0x2c43b053,
          0x2c442fc4,
          0x2c44b0dd,
          0x30320000,
          0x30328015,
          0x3033001f,
          0x30338038,
          0x3034004a,
          0x30348064,
          0x3035006b,
          0x30358083,
          0x30360094,
          0x303680ac,
          0x303700b9,
          0x303780c8,
          0x303800ea,
          0x303880f7,
          0x3039010a,
          0x30398125,
          0x303a013a,
          0x303a814e,
          0x303b0162,
          0x303b8173,
          0x303c018c,
          0x303c81a9,
          0x303d01b7,
          0x303d81cb,
          0x303e01db,
          0x303e81f4,
          0x303f0204,
          0x303f8217,
          0x30400226,
          0x30408232,
          0x30410247,
          0x30418257,
          0x3042026e,
          0x3042827b,
          0x3043028e,
          0x3043829d,
          0x304402b2,
          0x304482d3,
          0x304502e6,
          0x304582f9,
          0x30460312,
          0x3046832d,
          0x3047034a,
          0x3047835c,
          0x3048036a,
          0x3048837b,
          0x3049038a,
          0x304983a2,
          0x304a03b4,
          0x304a83c8,
          0x304b03e0,
          0x304b83f3,
          0x304c03fe,
          0x304c840f,
          0x304d041b,
          0x304d8431,
          0x304e043f,
          0x304e8455,
          0x304f0467,
          0x304f8479,
          0x3050049c,
          0x305084af,
          0x305104c0,
          0x305184d0,
          0x305204e8,
          0x305284fd,
          0x30530515,
          0x30538529,
          0x30540541,
          0x3054855a,
          0x30550573,
          0x30558590,
          0x3056059b,
          0x305685b3,
          0x305705c3,
          0x305785d4,
          0x305805e7,
          0x305885fd,
          0x30590606,
          0x3059861b,
          0x305a062e,
          0x305a863d,
          0x305b065d,
          0x305b866c,
          0x305c068d,
          0x305c86a9,
          0x305d06b5,
          0x305d86d5,
          0x305e06f1,
          0x305e8702,
          0x305f0718,
          0x305f8722,
          0x3060048c,
          0x34320b65,
          0x34328b79,
          0x34330b96,
          0x34338ba9,
          0x34340bb8,
          0x34348bf1,
          0x34350bd5,
          0x3c320083,
          0x3c328cbc,
          0x3c330cd5,
          0x3c338cf0,
          0x3c340d0d,
          0x3c348d37,
          0x3c350d52,
          0x3c358d78,
          0x3c360d91,
          0x3c368da9,
          0x3c370dba,
          0x3c378dc8,
          0x3c380dd5,
          0x3c388de9,
          0x3c390c7f,
          0x3c398e0c,
          0x3c3a0e20,
          0x3c3a890f,
          0x3c3b0e30,
          0x3c3b8e4b,
          0x3c3c0e5d,
          0x3c3c8e90,
          0x3c3d0e9a,
          0x3c3d8eae,
          0x3c3e0ebc,
          0x3c3e8ee1,
          0x3c3f0ca8,
          0x3c3f8eca,
          0x3c4000ac,
          0x3c4080ea,
          0x3c410d28,
          0x3c418d67,
          0x3c420e73,
          0x3c428dfd,
          0x40321971,
          0x40329987,
          0x403319b5,
          0x403399bf,
          0x403419d6,
          0x403499f4,
          0x40351a04,
          0x40359a16,
          0x40361a23,
          0x40369a2f,
          0x40371a44,
          0x40379a56,
          0x40381a61,
          0x40389a73,
          0x40390f27,
          0x40399a83,
          0x403a1a96,
          0x403a9ab7,
          0x403b1ac8,
          0x403b9ad8,
          0x403c0064,
          0x403c8083,
          0x403d1b5c,
          0x403d9b72,
          0x403e1b81,
          0x403e9bb9,
          0x403f1bd3,
          0x403f9bfb,
          0x40401c10,
          0x40409c24,
          0x40411c41,
          0x40419c5c,
          0x40421c75,
          0x40429c88,
          0x40431c9c,
          0x40439cb4,
          0x40441ccb,
          0x404480ac,
          0x40451ce0,
          0x40459cf2,
          0x40461d16,
          0x40469d36,
          0x40471d44,
          0x40479d6b,
          0x40481ddc,
          0x40489e0f,
          0x40491e26,
          0x40499e40,
          0x404a1e57,
          0x404a9e75,
          0x404b1e8d,
          0x404b9ea4,
          0x404c1eba,
          0x404c9ecc,
          0x404d1eed,
          0x404d9f26,
          0x404e1f3a,
          0x404e9f47,
          0x404f1f8e,
          0x404f9fd4,
          0x4050202b,
          0x4050a03f,
          0x40512072,
          0x40522082,
          0x4052a0a6,
          0x405320be,
          0x4053a0d1,
          0x405420e6,
          0x4054a109,
          0x40552117,
          0x4055a154,
          0x40562161,
          0x4056a17a,
          0x40572192,
          0x4057a1a5,
          0x405821ba,
          0x4058a1e1,
          0x40592210,
          0x4059a23d,
          0x405a2251,
          0x405aa261,
          0x405b2279,
          0x405ba28a,
          0x405c229d,
          0x405ca2dc,
          0x405d22e9,
          0x405da30e,
          0x405e234c,
          0x405e8ab3,
          0x405f236d,
          0x405fa37a,
          0x40602388,
          0x4060a3aa,
          0x4061240b,
          0x4061a443,
          0x4062245a,
          0x4062a46b,
          0x40632490,
          0x4063a4a5,
          0x406424bc,
          0x4064a4e8,
          0x40652503,
          0x4065a51a,
          0x40662532,
          0x4066a55c,
          0x40672587,
          0x4067a5cc,
          0x40682614,
          0x4068a635,
          0x40692667,
          0x4069a695,
          0x406a26b6,
          0x406aa6d6,
          0x406b285e,
          0x406ba881,
          0x406c2897,
          0x406cab3a,
          0x406d2b69,
          0x406dab91,
          0x406e2bbf,
          0x406eac0c,
          0x406f2c47,
          0x406fac7f,
          0x40702c92,
          0x4070acaf,
          0x40710802,
          0x4071acc1,
          0x40722cd4,
          0x4072ad0a,
          0x40732d22,
          0x407394fa,
          0x40742d36,
          0x4074ad50,
          0x40752d61,
          0x4075ad75,
          0x40762d83,
          0x407692be,
          0x40772da8,
          0x4077adca,
          0x40782de5,
          0x4078ae1e,
          0x40792e35,
          0x4079ae4b,
          0x407a2e77,
          0x407aae8a,
          0x407b2e9f,
          0x407baeb1,
          0x407c2ee2,
          0x407caeeb,
          0x407d2650,
          0x407d9fe4,
          0x407e2dfa,
          0x407ea1f1,
          0x407f1d58,
          0x407f9afe,
          0x40801f9e,
          0x40809d80,
          0x40812094,
          0x40819f78,
          0x40822baa,
          0x40829ae4,
          0x408321cc,
          0x4083a4cd,
          0x40841d94,
          0x4084a229,
          0x408522ae,
          0x4085a3d2,
          0x4086232e,
          0x40869ffe,
          0x40872bf0,
          0x4087a420,
          0x40881b45,
          0x4088a5df,
          0x40891b94,
          0x40899b21,
          0x408a28cf,
          0x408a9912,
          0x408b2ec6,
          0x408bac5c,
          0x408c22be,
          0x408c992e,
          0x408d1df5,
          0x408d9dc6,
          0x408e1f0f,
          0x408ea134,
          0x408f25f3,
          0x408fa3ee,
          0x409025a8,
          0x4090a300,
          0x409128b7,
          0x40919954,
          0x40921be1,
          0x4092ac2b,
          0x40932ced,
          0x4093a00f,
          0x40941da8,
          0x4094a8e8,
          0x4095247c,
          0x4095ae57,
          0x40962bd7,
          0x40969fb7,
          0x4097205a,
          0x40979f5e,
          0x41f42789,
          0x41f9281b,
          0x41fe270e,
          0x41fea92b,
          0x41ff2a1c,
          0x420327a2,
          0x420827c4,
          0x4208a800,
          0x420926f2,
          0x4209a83a,
          0x420a2749,
          0x420aa729,
          0x420b2769,
          0x420ba7e2,
          0x420c2a38,
          0x420ca8f8,
          0x420d2912,
          0x420da949,
          0x42122963,
          0x421729ff,
          0x4217a9a5,
          0x421c29c7,
          0x421f2982,
          0x42212a4f,
          0x422629e2,
          0x422b2b1e,
          0x422baacc,
          0x422c2b06,
          0x422caa8b,
          0x422d2a6a,
          0x422daaeb,
          0x422e2ab1,
          0x4432072d,
          0x4432873c,
          0x44330748,
          0x44338756,
          0x44340769,
          0x4434877a,
          0x44350781,
          0x4435878b,
          0x4436079e,
          0x443687b4,
          0x443707c6,
          0x443787d3,
          0x443807e2,
          0x443887ea,
          0x44390802,
          0x44398810,
          0x443a0823,
          0x483212e8,
          0x483292fa,
          0x48331310,
          0x48339329,
          0x4c32134e,
          0x4c32935e,
          0x4c331371,
          0x4c339391,
          0x4c3400ac,
          0x4c3480ea,
          0x4c35139d,
          0x4c3593ab,
          0x4c3613c7,
          0x4c3693ed,
          0x4c3713fc,
          0x4c37940a,
          0x4c38141f,
          0x4c38942b,
          0x4c39144b,
          0x4c399475,
          0x4c3a148e,
          0x4c3a94a7,
          0x4c3b05fd,
          0x4c3b94c0,
          0x4c3c14d2,
          0x4c3c94e1,
          0x4c3d14fa,
          0x4c3d8c47,
          0x4c3e1567,
          0x4c3e9509,
          0x4c3f1589,
          0x4c3f92be,
          0x4c40151f,
          0x4c40933a,
          0x4c411557,
          0x4c4193da,
          0x4c421543,
          0x50323141,
          0x5032b150,
          0x5033315b,
          0x5033b16b,
          0x50343184,
          0x5034b19e,
          0x503531ac,
          0x5035b1c2,
          0x503631d4,
          0x5036b1ea,
          0x50373203,
          0x5037b216,
          0x5038322e,
          0x5038b23f,
          0x50393254,
          0x5039b268,
          0x503a3288,
          0x503ab29e,
          0x503b32b6,
          0x503bb2c8,
          0x503c32e4,
          0x503cb2fb,
          0x503d3314,
          0x503db32a,
          0x503e3337,
          0x503eb34d,
          0x503f335f,
          0x503f837b,
          0x50403372,
          0x5040b382,
          0x5041339c,
          0x5041b3ab,
          0x504233c5,
          0x5042b3e2,
          0x504333f2,
          0x5043b402,
          0x50443411,
          0x50448431,
          0x50453425,
          0x5045b443,
          0x50463456,
          0x5046b46c,
          0x5047347e,
          0x5047b493,
          0x504834b9,
          0x5048b4c7,
          0x504934da,
          0x5049b4ef,
          0x504a3505,
          0x504ab515,
          0x504b3535,
          0x504bb548,
          0x504c356b,
          0x504cb599,
          0x504d35ab,
          0x504db5c8,
          0x504e35e3,
          0x504eb5ff,
          0x504f3611,
          0x504fb628,
          0x50503637,
          0x505086f1,
          0x5051364a,
          0x58320f65,
          0x68320f27,
          0x68328c7f,
          0x68330c92,
          0x68338f35,
          0x68340f45,
          0x683480ea,
          0x6c320eed,
          0x6c328c36,
          0x6c330ef8,
          0x6c338f11,
          0x74320a1b,
          0x743280ac,
          0x74330c47,
          0x78320980,
          0x78328995,
          0x783309a1,
          0x78338083,
          0x783409b0,
          0x783489c5,
          0x783509e4,
          0x78358a06,
          0x78360a1b,
          0x78368a31,
          0x78370a41,
          0x78378a62,
          0x78380a75,
          0x78388a87,
          0x78390a94,
          0x78398ab3,
          0x783a0ac8,
          0x783a8ad6,
          0x783b0ae0,
          0x783b8af4,
          0x783c0b0b,
          0x783c8b20,
          0x783d0b37,
          0x783d8b4c,
          0x783e0aa2,
          0x783e8a54,
          0x7c3211d7,
      };
      
      const size_t kOpenSSLReasonValuesLen = sizeof(kOpenSSLReasonValues) / sizeof(kOpenSSLReasonValues[0]);
      
      const char kOpenSSLReasonStringData[] =
          "ASN1_LENGTH_MISMATCH\\0"
          "AUX_ERROR\\0"
          "BAD_GET_ASN1_OBJECT_CALL\\0"
          "BAD_OBJECT_HEADER\\0"
          "BMPSTRING_IS_WRONG_LENGTH\\0"
          "BN_LIB\\0"
          "BOOLEAN_IS_WRONG_LENGTH\\0"
          "BUFFER_TOO_SMALL\\0"
          "CONTEXT_NOT_INITIALISED\\0"
          "DECODE_ERROR\\0"
          "DEPTH_EXCEEDED\\0"
          "DIGEST_AND_KEY_TYPE_NOT_SUPPORTED\\0"
          "ENCODE_ERROR\\0"
          "ERROR_GETTING_TIME\\0"
          "EXPECTING_AN_ASN1_SEQUENCE\\0"
          "EXPECTING_AN_INTEGER\\0"
          "EXPECTING_AN_OBJECT\\0"
          "EXPECTING_A_BOOLEAN\\0"
          "EXPECTING_A_TIME\\0"
          "EXPLICIT_LENGTH_MISMATCH\\0"
          "EXPLICIT_TAG_NOT_CONSTRUCTED\\0"
          "FIELD_MISSING\\0"
          "FIRST_NUM_TOO_LARGE\\0"
          "HEADER_TOO_LONG\\0"
          "ILLEGAL_BITSTRING_FORMAT\\0"
          "ILLEGAL_BOOLEAN\\0"
          "ILLEGAL_CHARACTERS\\0"
          "ILLEGAL_FORMAT\\0"
          "ILLEGAL_HEX\\0"
          "ILLEGAL_IMPLICIT_TAG\\0"
          "ILLEGAL_INTEGER\\0"
          "ILLEGAL_NESTED_TAGGING\\0"
          "ILLEGAL_NULL\\0"
          "ILLEGAL_NULL_VALUE\\0"
          "ILLEGAL_OBJECT\\0"
          "ILLEGAL_OPTIONAL_ANY\\0"
          "ILLEGAL_OPTIONS_ON_ITEM_TEMPLATE\\0"
          "ILLEGAL_TAGGED_ANY\\0"
          "ILLEGAL_TIME_VALUE\\0"
          "INTEGER_NOT_ASCII_FORMAT\\0"
          "INTEGER_TOO_LARGE_FOR_LONG\\0"
          "INVALID_BIT_STRING_BITS_LEFT\\0"
          "INVALID_BMPSTRING\\0"
          "INVALID_DIGIT\\0"
          "INVALID_MODIFIER\\0"
          "INVALID_NUMBER\\0"
          "INVALID_OBJECT_ENCODING\\0"
          "INVALID_SEPARATOR\\0"
          "INVALID_TIME_FORMAT\\0"
          "INVALID_UNIVERSALSTRING\\0"
          "INVALID_UTF8STRING\\0"
          "LIST_ERROR\\0"
          "MISSING_ASN1_EOS\\0"
          "MISSING_EOC\\0"
          "MISSING_SECOND_NUMBER\\0"
          "MISSING_VALUE\\0"
          "MSTRING_NOT_UNIVERSAL\\0"
          "MSTRING_WRONG_TAG\\0"
          "NESTED_ASN1_ERROR\\0"
          "NESTED_ASN1_STRING\\0"
          "NESTED_TOO_DEEP\\0"
          "NON_HEX_CHARACTERS\\0"
          "NOT_ASCII_FORMAT\\0"
          "NOT_ENOUGH_DATA\\0"
          "NO_MATCHING_CHOICE_TYPE\\0"
          "NULL_IS_WRONG_LENGTH\\0"
          "OBJECT_NOT_ASCII_FORMAT\\0"
          "ODD_NUMBER_OF_CHARS\\0"
          "SECOND_NUMBER_TOO_LARGE\\0"
          "SEQUENCE_LENGTH_MISMATCH\\0"
          "SEQUENCE_NOT_CONSTRUCTED\\0"
          "SEQUENCE_OR_SET_NEEDS_CONFIG\\0"
          "SHORT_LINE\\0"
          "STREAMING_NOT_SUPPORTED\\0"
          "STRING_TOO_LONG\\0"
          "STRING_TOO_SHORT\\0"
          "TAG_VALUE_TOO_HIGH\\0"
          "TIME_NOT_ASCII_FORMAT\\0"
          "TOO_LONG\\0"
          "TYPE_NOT_CONSTRUCTED\\0"
          "TYPE_NOT_PRIMITIVE\\0"
          "UNEXPECTED_EOC\\0"
          "UNIVERSALSTRING_IS_WRONG_LENGTH\\0"
          "UNKNOWN_FORMAT\\0"
          "UNKNOWN_MESSAGE_DIGEST_ALGORITHM\\0"
          "UNKNOWN_SIGNATURE_ALGORITHM\\0"
          "UNKNOWN_TAG\\0"
          "UNSUPPORTED_ANY_DEFINED_BY_TYPE\\0"
          "UNSUPPORTED_PUBLIC_KEY_TYPE\\0"
          "UNSUPPORTED_TYPE\\0"
          "WRONG_PUBLIC_KEY_TYPE\\0"
          "WRONG_TAG\\0"
          "WRONG_TYPE\\0"
          "BAD_FOPEN_MODE\\0"
          "BROKEN_PIPE\\0"
          "CONNECT_ERROR\\0"
          "ERROR_SETTING_NBIO\\0"
          "INVALID_ARGUMENT\\0"
          "IN_USE\\0"
          "KEEPALIVE\\0"
          "NBIO_CONNECT_ERROR\\0"
          "NO_HOSTNAME_SPECIFIED\\0"
          "NO_PORT_SPECIFIED\\0"
          "NO_SUCH_FILE\\0"
          "NULL_PARAMETER\\0"
          "SYS_LIB\\0"
          "UNABLE_TO_CREATE_SOCKET\\0"
          "UNINITIALIZED\\0"
          "UNSUPPORTED_METHOD\\0"
          "WRITE_TO_READ_ONLY_BIO\\0"
          "ARG2_LT_ARG3\\0"
          "BAD_ENCODING\\0"
          "BAD_RECIPROCAL\\0"
          "BIGNUM_TOO_LONG\\0"
          "BITS_TOO_SMALL\\0"
          "CALLED_WITH_EVEN_MODULUS\\0"
          "DIV_BY_ZERO\\0"
          "EXPAND_ON_STATIC_BIGNUM_DATA\\0"
          "INPUT_NOT_REDUCED\\0"
          "INVALID_INPUT\\0"
          "INVALID_RANGE\\0"
          "NEGATIVE_NUMBER\\0"
          "NOT_A_SQUARE\\0"
          "NOT_INITIALIZED\\0"
          "NO_INVERSE\\0"
          "PRIVATE_KEY_TOO_LARGE\\0"
          "P_IS_NOT_PRIME\\0"
          "TOO_MANY_ITERATIONS\\0"
          "TOO_MANY_TEMPORARY_VARIABLES\\0"
          "AES_KEY_SETUP_FAILED\\0"
          "BAD_DECRYPT\\0"
          "BAD_KEY_LENGTH\\0"
          "CTRL_NOT_IMPLEMENTED\\0"
          "CTRL_OPERATION_NOT_IMPLEMENTED\\0"
          "DATA_NOT_MULTIPLE_OF_BLOCK_LENGTH\\0"
          "INITIALIZATION_ERROR\\0"
          "INPUT_NOT_INITIALIZED\\0"
          "INVALID_AD_SIZE\\0"
          "INVALID_KEY_LENGTH\\0"
          "INVALID_NONCE\\0"
          "INVALID_NONCE_SIZE\\0"
          "INVALID_OPERATION\\0"
          "IV_TOO_LARGE\\0"
          "NO_CIPHER_SET\\0"
          "NO_DIRECTION_SET\\0"
          "OUTPUT_ALIASES_INPUT\\0"
          "TAG_TOO_LARGE\\0"
          "TOO_LARGE\\0"
          "UNSUPPORTED_AD_SIZE\\0"
          "UNSUPPORTED_INPUT_SIZE\\0"
          "UNSUPPORTED_KEY_SIZE\\0"
          "UNSUPPORTED_NONCE_SIZE\\0"
          "UNSUPPORTED_TAG_SIZE\\0"
          "WRONG_FINAL_BLOCK_LENGTH\\0"
          "LIST_CANNOT_BE_NULL\\0"
          "MISSING_CLOSE_SQUARE_BRACKET\\0"
          "MISSING_EQUAL_SIGN\\0"
          "NO_CLOSE_BRACE\\0"
          "UNABLE_TO_CREATE_NEW_SECTION\\0"
          "VARIABLE_EXPANSION_TOO_LONG\\0"
          "VARIABLE_HAS_NO_VALUE\\0"
          "BAD_GENERATOR\\0"
          "INVALID_PUBKEY\\0"
          "MODULUS_TOO_LARGE\\0"
          "NO_PRIVATE_VALUE\\0"
          "UNKNOWN_HASH\\0"
          "BAD_Q_VALUE\\0"
          "BAD_VERSION\\0"
          "INVALID_PARAMETERS\\0"
          "MISSING_PARAMETERS\\0"
          "NEED_NEW_SETUP_VALUES\\0"
          "BIGNUM_OUT_OF_RANGE\\0"
          "COORDINATES_OUT_OF_RANGE\\0"
          "D2I_ECPKPARAMETERS_FAILURE\\0"
          "EC_GROUP_NEW_BY_NAME_FAILURE\\0"
          "GROUP2PKPARAMETERS_FAILURE\\0"
          "GROUP_MISMATCH\\0"
          "I2D_ECPKPARAMETERS_FAILURE\\0"
          "INCOMPATIBLE_OBJECTS\\0"
          "INVALID_COFACTOR\\0"
          "INVALID_COMPRESSED_POINT\\0"
          "INVALID_COMPRESSION_BIT\\0"
          "INVALID_ENCODING\\0"
          "INVALID_FIELD\\0"
          "INVALID_FORM\\0"
          "INVALID_GROUP_ORDER\\0"
          "INVALID_PRIVATE_KEY\\0"
          "INVALID_SCALAR\\0"
          "MISSING_PRIVATE_KEY\\0"
          "NON_NAMED_CURVE\\0"
          "PKPARAMETERS2GROUP_FAILURE\\0"
          "POINT_AT_INFINITY\\0"
          "POINT_IS_NOT_ON_CURVE\\0"
          "PUBLIC_KEY_VALIDATION_FAILED\\0"
          "SLOT_FULL\\0"
          "UNDEFINED_GENERATOR\\0"
          "UNKNOWN_GROUP\\0"
          "UNKNOWN_ORDER\\0"
          "WRONG_CURVE_PARAMETERS\\0"
          "WRONG_ORDER\\0"
          "KDF_FAILED\\0"
          "POINT_ARITHMETIC_FAILURE\\0"
          "UNKNOWN_DIGEST_LENGTH\\0"
          "BAD_SIGNATURE\\0"
          "NOT_IMPLEMENTED\\0"
          "RANDOM_NUMBER_GENERATION_FAILED\\0"
          "OPERATION_NOT_SUPPORTED\\0"
          "COMMAND_NOT_SUPPORTED\\0"
          "DIFFERENT_KEY_TYPES\\0"
          "DIFFERENT_PARAMETERS\\0"
          "EXPECTING_AN_EC_KEY_KEY\\0"
          "EXPECTING_AN_RSA_KEY\\0"
          "EXPECTING_A_DSA_KEY\\0"
          "ILLEGAL_OR_UNSUPPORTED_PADDING_MODE\\0"
          "INVALID_DIGEST_LENGTH\\0"
          "INVALID_DIGEST_TYPE\\0"
          "INVALID_KEYBITS\\0"
          "INVALID_MGF1_MD\\0"
          "INVALID_PADDING_MODE\\0"
          "INVALID_PEER_KEY\\0"
          "INVALID_PSS_SALTLEN\\0"
          "INVALID_SIGNATURE\\0"
          "KEYS_NOT_SET\\0"
          "MEMORY_LIMIT_EXCEEDED\\0"
          "NOT_A_PRIVATE_KEY\\0"
          "NOT_XOF_OR_INVALID_LENGTH\\0"
          "NO_DEFAULT_DIGEST\\0"
          "NO_KEY_SET\\0"
          "NO_MDC2_SUPPORT\\0"
          "NO_NID_FOR_CURVE\\0"
          "NO_OPERATION_SET\\0"
          "NO_PARAMETERS_SET\\0"
          "OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE\\0"
          "OPERATON_NOT_INITIALIZED\\0"
          "UNKNOWN_PUBLIC_KEY_TYPE\\0"
          "UNSUPPORTED_ALGORITHM\\0"
          "OUTPUT_TOO_LARGE\\0"
          "INVALID_OID_STRING\\0"
          "UNKNOWN_NID\\0"
          "BAD_BASE64_DECODE\\0"
          "BAD_END_LINE\\0"
          "BAD_IV_CHARS\\0"
          "BAD_PASSWORD_READ\\0"
          "CIPHER_IS_NULL\\0"
          "ERROR_CONVERTING_PRIVATE_KEY\\0"
          "NOT_DEK_INFO\\0"
          "NOT_ENCRYPTED\\0"
          "NOT_PROC_TYPE\\0"
          "NO_START_LINE\\0"
          "READ_KEY\\0"
          "SHORT_HEADER\\0"
          "UNSUPPORTED_CIPHER\\0"
          "UNSUPPORTED_ENCRYPTION\\0"
          "BAD_PKCS7_VERSION\\0"
          "NOT_PKCS7_SIGNED_DATA\\0"
          "NO_CERTIFICATES_INCLUDED\\0"
          "NO_CRLS_INCLUDED\\0"
          "BAD_ITERATION_COUNT\\0"
          "BAD_PKCS12_DATA\\0"
          "BAD_PKCS12_VERSION\\0"
          "CIPHER_HAS_NO_OBJECT_IDENTIFIER\\0"
          "CRYPT_ERROR\\0"
          "ENCRYPT_ERROR\\0"
          "ERROR_SETTING_CIPHER_PARAMS\\0"
          "INCORRECT_PASSWORD\\0"
          "INVALID_CHARACTERS\\0"
          "KEYGEN_FAILURE\\0"
          "KEY_GEN_ERROR\\0"
          "METHOD_NOT_SUPPORTED\\0"
          "MISSING_MAC\\0"
          "MULTIPLE_PRIVATE_KEYS_IN_PKCS12\\0"
          "PKCS12_PUBLIC_KEY_INTEGRITY_NOT_SUPPORTED\\0"
          "PKCS12_TOO_DEEPLY_NESTED\\0"
          "PRIVATE_KEY_DECODE_ERROR\\0"
          "PRIVATE_KEY_ENCODE_ERROR\\0"
          "UNKNOWN_ALGORITHM\\0"
          "UNKNOWN_CIPHER\\0"
          "UNKNOWN_CIPHER_ALGORITHM\\0"
          "UNKNOWN_DIGEST\\0"
          "UNSUPPORTED_KEYLENGTH\\0"
          "UNSUPPORTED_KEY_DERIVATION_FUNCTION\\0"
          "UNSUPPORTED_OPTIONS\\0"
          "UNSUPPORTED_PRF\\0"
          "UNSUPPORTED_PRIVATE_KEY_ALGORITHM\\0"
          "UNSUPPORTED_SALT_TYPE\\0"
          "BAD_E_VALUE\\0"
          "BAD_FIXED_HEADER_DECRYPT\\0"
          "BAD_PAD_BYTE_COUNT\\0"
          "BAD_RSA_PARAMETERS\\0"
          "BLOCK_TYPE_IS_NOT_01\\0"
          "BLOCK_TYPE_IS_NOT_02\\0"
          "BN_NOT_INITIALIZED\\0"
          "CANNOT_RECOVER_MULTI_PRIME_KEY\\0"
          "CRT_PARAMS_ALREADY_GIVEN\\0"
          "CRT_VALUES_INCORRECT\\0"
          "DATA_LEN_NOT_EQUAL_TO_MOD_LEN\\0"
          "DATA_TOO_LARGE\\0"
          "DATA_TOO_LARGE_FOR_KEY_SIZE\\0"
          "DATA_TOO_LARGE_FOR_MODULUS\\0"
          "DATA_TOO_SMALL\\0"
          "DATA_TOO_SMALL_FOR_KEY_SIZE\\0"
          "DIGEST_TOO_BIG_FOR_RSA_KEY\\0"
          "D_E_NOT_CONGRUENT_TO_1\\0"
          "D_OUT_OF_RANGE\\0"
          "EMPTY_PUBLIC_KEY\\0"
          "FIRST_OCTET_INVALID\\0"
          "INCONSISTENT_SET_OF_CRT_VALUES\\0"
          "INTERNAL_ERROR\\0"
          "INVALID_MESSAGE_LENGTH\\0"
          "KEY_SIZE_TOO_SMALL\\0"
          "LAST_OCTET_INVALID\\0"
          "MUST_HAVE_AT_LEAST_TWO_PRIMES\\0"
          "NO_PUBLIC_EXPONENT\\0"
          "NULL_BEFORE_BLOCK_MISSING\\0"
          "N_NOT_EQUAL_P_Q\\0"
          "OAEP_DECODING_ERROR\\0"
          "ONLY_ONE_OF_P_Q_GIVEN\\0"
          "OUTPUT_BUFFER_TOO_SMALL\\0"
          "PADDING_CHECK_FAILED\\0"
          "PKCS_DECODING_ERROR\\0"
          "SLEN_CHECK_FAILED\\0"
          "SLEN_RECOVERY_FAILED\\0"
          "UNKNOWN_ALGORITHM_TYPE\\0"
          "UNKNOWN_PADDING_TYPE\\0"
          "VALUE_MISSING\\0"
          "WRONG_SIGNATURE_LENGTH\\0"
          "ALPN_MISMATCH_ON_EARLY_DATA\\0"
          "APPLICATION_DATA_INSTEAD_OF_HANDSHAKE\\0"
          "APPLICATION_DATA_ON_SHUTDOWN\\0"
          "APP_DATA_IN_HANDSHAKE\\0"
          "ATTEMPT_TO_REUSE_SESSION_IN_DIFFERENT_CONTEXT\\0"
          "BAD_ALERT\\0"
          "BAD_CHANGE_CIPHER_SPEC\\0"
          "BAD_DATA_RETURNED_BY_CALLBACK\\0"
          "BAD_DH_P_LENGTH\\0"
          "BAD_DIGEST_LENGTH\\0"
          "BAD_ECC_CERT\\0"
          "BAD_ECPOINT\\0"
          "BAD_HANDSHAKE_RECORD\\0"
          "BAD_HELLO_REQUEST\\0"
          "BAD_LENGTH\\0"
          "BAD_PACKET_LENGTH\\0"
          "BAD_RSA_ENCRYPT\\0"
          "BAD_SRTP_MKI_VALUE\\0"
          "BAD_SRTP_PROTECTION_PROFILE_LIST\\0"
          "BAD_SSL_FILETYPE\\0"
          "BAD_WRITE_RETRY\\0"
          "BIO_NOT_SET\\0"
          "BLOCK_CIPHER_PAD_IS_WRONG\\0"
          "BUFFERED_MESSAGES_ON_CIPHER_CHANGE\\0"
          "CANNOT_HAVE_BOTH_PRIVKEY_AND_METHOD\\0"
          "CANNOT_PARSE_LEAF_CERT\\0"
          "CA_DN_LENGTH_MISMATCH\\0"
          "CA_DN_TOO_LONG\\0"
          "CCS_RECEIVED_EARLY\\0"
          "CERTIFICATE_AND_PRIVATE_KEY_MISMATCH\\0"
          "CERTIFICATE_VERIFY_FAILED\\0"
          "CERT_CB_ERROR\\0"
          "CERT_DECOMPRESSION_FAILED\\0"
          "CERT_LENGTH_MISMATCH\\0"
          "CHANNEL_ID_NOT_P256\\0"
          "CHANNEL_ID_SIGNATURE_INVALID\\0"
          "CIPHER_OR_HASH_UNAVAILABLE\\0"
          "CLIENTHELLO_PARSE_FAILED\\0"
          "CLIENTHELLO_TLSEXT\\0"
          "CONNECTION_REJECTED\\0"
          "CONNECTION_TYPE_NOT_SET\\0"
          "CUSTOM_EXTENSION_ERROR\\0"
          "DATA_LENGTH_TOO_LONG\\0"
          "DECRYPTION_FAILED\\0"
          "DECRYPTION_FAILED_OR_BAD_RECORD_MAC\\0"
          "DH_PUBLIC_VALUE_LENGTH_IS_WRONG\\0"
          "DH_P_TOO_LONG\\0"
          "DIGEST_CHECK_FAILED\\0"
          "DOWNGRADE_DETECTED\\0"
          "DTLS_MESSAGE_TOO_BIG\\0"
          "DUPLICATE_EXTENSION\\0"
          "DUPLICATE_KEY_SHARE\\0"
          "DUPLICATE_SIGNATURE_ALGORITHM\\0"
          "EARLY_DATA_NOT_IN_USE\\0"
          "ECC_CERT_NOT_FOR_SIGNING\\0"
          "EMPTY_HELLO_RETRY_REQUEST\\0"
          "EMS_STATE_INCONSISTENT\\0"
          "ENCRYPTED_LENGTH_TOO_LONG\\0"
          "ERROR_ADDING_EXTENSION\\0"
          "ERROR_IN_RECEIVED_CIPHER_LIST\\0"
          "ERROR_PARSING_EXTENSION\\0"
          "EXCESSIVE_MESSAGE_SIZE\\0"
          "EXTRA_DATA_IN_MESSAGE\\0"
          "FRAGMENT_MISMATCH\\0"
          "GOT_NEXT_PROTO_WITHOUT_EXTENSION\\0"
          "HANDSHAKE_FAILURE_ON_CLIENT_HELLO\\0"
          "HANDSHAKE_NOT_COMPLETE\\0"
          "HTTPS_PROXY_REQUEST\\0"
          "HTTP_REQUEST\\0"
          "INAPPROPRIATE_FALLBACK\\0"
          "INCONSISTENT_CLIENT_HELLO\\0"
          "INVALID_ALPN_PROTOCOL\\0"
          "INVALID_COMMAND\\0"
          "INVALID_COMPRESSION_LIST\\0"
          "INVALID_DELEGATED_CREDENTIAL\\0"
          "INVALID_MESSAGE\\0"
          "INVALID_OUTER_RECORD_TYPE\\0"
          "INVALID_SCT_LIST\\0"
          "INVALID_SIGNATURE_ALGORITHM\\0"
          "INVALID_SSL_SESSION\\0"
          "INVALID_TICKET_KEYS_LENGTH\\0"
          "KEY_USAGE_BIT_INCORRECT\\0"
          "LENGTH_MISMATCH\\0"
          "MISSING_EXTENSION\\0"
          "MISSING_KEY_SHARE\\0"
          "MISSING_RSA_CERTIFICATE\\0"
          "MISSING_TMP_DH_KEY\\0"
          "MISSING_TMP_ECDH_KEY\\0"
          "MIXED_SPECIAL_OPERATOR_WITH_GROUPS\\0"
          "MTU_TOO_SMALL\\0"
          "NEGOTIATED_BOTH_NPN_AND_ALPN\\0"
          "NEGOTIATED_TB_WITHOUT_EMS_OR_RI\\0"
          "NESTED_GROUP\\0"
          "NO_CERTIFICATES_RETURNED\\0"
          "NO_CERTIFICATE_ASSIGNED\\0"
          "NO_CERTIFICATE_SET\\0"
          "NO_CIPHERS_AVAILABLE\\0"
          "NO_CIPHERS_PASSED\\0"
          "NO_CIPHERS_SPECIFIED\\0"
          "NO_CIPHER_MATCH\\0"
          "NO_COMMON_SIGNATURE_ALGORITHMS\\0"
          "NO_COMPRESSION_SPECIFIED\\0"
          "NO_GROUPS_SPECIFIED\\0"
          "NO_METHOD_SPECIFIED\\0"
          "NO_P256_SUPPORT\\0"
          "NO_PRIVATE_KEY_ASSIGNED\\0"
          "NO_RENEGOTIATION\\0"
          "NO_REQUIRED_DIGEST\\0"
          "NO_SHARED_CIPHER\\0"
          "NO_SHARED_GROUP\\0"
          "NO_SUPPORTED_VERSIONS_ENABLED\\0"
          "NULL_SSL_CTX\\0"
          "NULL_SSL_METHOD_PASSED\\0"
          "OCSP_CB_ERROR\\0"
          "OLD_SESSION_CIPHER_NOT_RETURNED\\0"
          "OLD_SESSION_PRF_HASH_MISMATCH\\0"
          "OLD_SESSION_VERSION_NOT_RETURNED\\0"
          "PARSE_TLSEXT\\0"
          "PATH_TOO_LONG\\0"
          "PEER_DID_NOT_RETURN_A_CERTIFICATE\\0"
          "PEER_ERROR_UNSUPPORTED_CERTIFICATE_TYPE\\0"
          "PRE_SHARED_KEY_MUST_BE_LAST\\0"
          "PRIVATE_KEY_OPERATION_FAILED\\0"
          "PROTOCOL_IS_SHUTDOWN\\0"
          "PSK_IDENTITY_BINDER_COUNT_MISMATCH\\0"
          "PSK_IDENTITY_NOT_FOUND\\0"
          "PSK_NO_CLIENT_CB\\0"
          "PSK_NO_SERVER_CB\\0"
          "QUIC_INTERNAL_ERROR\\0"
          "READ_TIMEOUT_EXPIRED\\0"
          "RECORD_LENGTH_MISMATCH\\0"
          "RECORD_TOO_LARGE\\0"
          "RENEGOTIATION_EMS_MISMATCH\\0"
          "RENEGOTIATION_ENCODING_ERR\\0"
          "RENEGOTIATION_MISMATCH\\0"
          "REQUIRED_CIPHER_MISSING\\0"
          "RESUMED_EMS_SESSION_WITHOUT_EMS_EXTENSION\\0"
          "RESUMED_NON_EMS_SESSION_WITH_EMS_EXTENSION\\0"
          "SCSV_RECEIVED_WHEN_RENEGOTIATING\\0"
          "SECOND_SERVERHELLO_VERSION_MISMATCH\\0"
          "SERVERHELLO_TLSEXT\\0"
          "SERVER_CERT_CHANGED\\0"
          "SERVER_ECHOED_INVALID_SESSION_ID\\0"
          "SESSION_ID_CONTEXT_UNINITIALIZED\\0"
          "SESSION_MAY_NOT_BE_CREATED\\0"
          "SHUTDOWN_WHILE_IN_INIT\\0"
          "SIGNATURE_ALGORITHMS_EXTENSION_SENT_BY_SERVER\\0"
          "SRTP_COULD_NOT_ALLOCATE_PROFILES\\0"
          "SRTP_UNKNOWN_PROTECTION_PROFILE\\0"
          "SSL3_EXT_INVALID_SERVERNAME\\0"
          "SSLV3_ALERT_BAD_CERTIFICATE\\0"
          "SSLV3_ALERT_BAD_RECORD_MAC\\0"
          "SSLV3_ALERT_CERTIFICATE_EXPIRED\\0"
          "SSLV3_ALERT_CERTIFICATE_REVOKED\\0"
          "SSLV3_ALERT_CERTIFICATE_UNKNOWN\\0"
          "SSLV3_ALERT_CLOSE_NOTIFY\\0"
          "SSLV3_ALERT_DECOMPRESSION_FAILURE\\0"
          "SSLV3_ALERT_HANDSHAKE_FAILURE\\0"
          "SSLV3_ALERT_ILLEGAL_PARAMETER\\0"
          "SSLV3_ALERT_NO_CERTIFICATE\\0"
          "SSLV3_ALERT_UNEXPECTED_MESSAGE\\0"
          "SSLV3_ALERT_UNSUPPORTED_CERTIFICATE\\0"
          "SSL_CTX_HAS_NO_DEFAULT_SSL_VERSION\\0"
          "SSL_HANDSHAKE_FAILURE\\0"
          "SSL_SESSION_ID_CONTEXT_TOO_LONG\\0"
          "SSL_SESSION_ID_TOO_LONG\\0"
          "TICKET_ENCRYPTION_FAILED\\0"
          "TLS13_DOWNGRADE\\0"
          "TLSV1_ALERT_ACCESS_DENIED\\0"
          "TLSV1_ALERT_DECODE_ERROR\\0"
          "TLSV1_ALERT_DECRYPTION_FAILED\\0"
          "TLSV1_ALERT_DECRYPT_ERROR\\0"
          "TLSV1_ALERT_EXPORT_RESTRICTION\\0"
          "TLSV1_ALERT_INAPPROPRIATE_FALLBACK\\0"
          "TLSV1_ALERT_INSUFFICIENT_SECURITY\\0"
          "TLSV1_ALERT_INTERNAL_ERROR\\0"
          "TLSV1_ALERT_NO_RENEGOTIATION\\0"
          "TLSV1_ALERT_PROTOCOL_VERSION\\0"
          "TLSV1_ALERT_RECORD_OVERFLOW\\0"
          "TLSV1_ALERT_UNKNOWN_CA\\0"
          "TLSV1_ALERT_USER_CANCELLED\\0"
          "TLSV1_BAD_CERTIFICATE_HASH_VALUE\\0"
          "TLSV1_BAD_CERTIFICATE_STATUS_RESPONSE\\0"
          "TLSV1_CERTIFICATE_REQUIRED\\0"
          "TLSV1_CERTIFICATE_UNOBTAINABLE\\0"
          "TLSV1_UNKNOWN_PSK_IDENTITY\\0"
          "TLSV1_UNRECOGNIZED_NAME\\0"
          "TLSV1_UNSUPPORTED_EXTENSION\\0"
          "TLS_PEER_DID_NOT_RESPOND_WITH_CERTIFICATE_LIST\\0"
          "TLS_RSA_ENCRYPTED_VALUE_LENGTH_IS_WRONG\\0"
          "TOO_MANY_EMPTY_FRAGMENTS\\0"
          "TOO_MANY_KEY_UPDATES\\0"
          "TOO_MANY_WARNING_ALERTS\\0"
          "TOO_MUCH_READ_EARLY_DATA\\0"
          "TOO_MUCH_SKIPPED_EARLY_DATA\\0"
          "UNABLE_TO_FIND_ECDH_PARAMETERS\\0"
          "UNCOMPRESSED_CERT_TOO_LARGE\\0"
          "UNEXPECTED_EXTENSION\\0"
          "UNEXPECTED_EXTENSION_ON_EARLY_DATA\\0"
          "UNEXPECTED_MESSAGE\\0"
          "UNEXPECTED_OPERATOR_IN_GROUP\\0"
          "UNEXPECTED_RECORD\\0"
          "UNKNOWN_ALERT_TYPE\\0"
          "UNKNOWN_CERTIFICATE_TYPE\\0"
          "UNKNOWN_CERT_COMPRESSION_ALG\\0"
          "UNKNOWN_CIPHER_RETURNED\\0"
          "UNKNOWN_CIPHER_TYPE\\0"
          "UNKNOWN_KEY_EXCHANGE_TYPE\\0"
          "UNKNOWN_PROTOCOL\\0"
          "UNKNOWN_SSL_VERSION\\0"
          "UNKNOWN_STATE\\0"
          "UNSAFE_LEGACY_RENEGOTIATION_DISABLED\\0"
          "UNSUPPORTED_COMPRESSION_ALGORITHM\\0"
          "UNSUPPORTED_ELLIPTIC_CURVE\\0"
          "UNSUPPORTED_PROTOCOL\\0"
          "UNSUPPORTED_PROTOCOL_FOR_CUSTOM_KEY\\0"
          "WRONG_CERTIFICATE_TYPE\\0"
          "WRONG_CIPHER_RETURNED\\0"
          "WRONG_CURVE\\0"
          "WRONG_ENCRYPTION_LEVEL_RECEIVED\\0"
          "WRONG_MESSAGE_TYPE\\0"
          "WRONG_SIGNATURE_TYPE\\0"
          "WRONG_SSL_VERSION\\0"
          "WRONG_VERSION_NUMBER\\0"
          "WRONG_VERSION_ON_EARLY_DATA\\0"
          "X509_LIB\\0"
          "X509_VERIFICATION_SETUP_PROBLEMS\\0"
          "AKID_MISMATCH\\0"
          "BAD_X509_FILETYPE\\0"
          "BASE64_DECODE_ERROR\\0"
          "CANT_CHECK_DH_KEY\\0"
          "CERT_ALREADY_IN_HASH_TABLE\\0"
          "CRL_ALREADY_DELTA\\0"
          "CRL_VERIFY_FAILURE\\0"
          "IDP_MISMATCH\\0"
          "INVALID_DIRECTORY\\0"
          "INVALID_FIELD_NAME\\0"
          "INVALID_PARAMETER\\0"
          "INVALID_PSS_PARAMETERS\\0"
          "INVALID_TRUST\\0"
          "ISSUER_MISMATCH\\0"
          "KEY_TYPE_MISMATCH\\0"
          "KEY_VALUES_MISMATCH\\0"
          "LOADING_CERT_DIR\\0"
          "LOADING_DEFAULTS\\0"
          "NAME_TOO_LONG\\0"
          "NEWER_CRL_NOT_NEWER\\0"
          "NO_CERT_SET_FOR_US_TO_VERIFY\\0"
          "NO_CRL_NUMBER\\0"
          "PUBLIC_KEY_DECODE_ERROR\\0"
          "PUBLIC_KEY_ENCODE_ERROR\\0"
          "SHOULD_RETRY\\0"
          "SIGNATURE_ALGORITHM_MISMATCH\\0"
          "UNKNOWN_KEY_TYPE\\0"
          "UNKNOWN_PURPOSE_ID\\0"
          "UNKNOWN_TRUST_ID\\0"
          "WRONG_LOOKUP_TYPE\\0"
          "BAD_IP_ADDRESS\\0"
          "BAD_OBJECT\\0"
          "BN_DEC2BN_ERROR\\0"
          "BN_TO_ASN1_INTEGER_ERROR\\0"
          "CANNOT_FIND_FREE_FUNCTION\\0"
          "DIRNAME_ERROR\\0"
          "DISTPOINT_ALREADY_SET\\0"
          "DUPLICATE_ZONE_ID\\0"
          "ERROR_CONVERTING_ZONE\\0"
          "ERROR_CREATING_EXTENSION\\0"
          "ERROR_IN_EXTENSION\\0"
          "EXPECTED_A_SECTION_NAME\\0"
          "EXTENSION_EXISTS\\0"
          "EXTENSION_NAME_ERROR\\0"
          "EXTENSION_NOT_FOUND\\0"
          "EXTENSION_SETTING_NOT_SUPPORTED\\0"
          "EXTENSION_VALUE_ERROR\\0"
          "ILLEGAL_EMPTY_EXTENSION\\0"
          "ILLEGAL_HEX_DIGIT\\0"
          "INCORRECT_POLICY_SYNTAX_TAG\\0"
          "INVALID_BOOLEAN_STRING\\0"
          "INVALID_EXTENSION_STRING\\0"
          "INVALID_MULTIPLE_RDNS\\0"
          "INVALID_NAME\\0"
          "INVALID_NULL_ARGUMENT\\0"
          "INVALID_NULL_NAME\\0"
          "INVALID_NULL_VALUE\\0"
          "INVALID_NUMBERS\\0"
          "INVALID_OBJECT_IDENTIFIER\\0"
          "INVALID_OPTION\\0"
          "INVALID_POLICY_IDENTIFIER\\0"
          "INVALID_PROXY_POLICY_SETTING\\0"
          "INVALID_PURPOSE\\0"
          "INVALID_SECTION\\0"
          "INVALID_SYNTAX\\0"
          "ISSUER_DECODE_ERROR\\0"
          "NEED_ORGANIZATION_AND_NUMBERS\\0"
          "NO_CONFIG_DATABASE\\0"
          "NO_ISSUER_CERTIFICATE\\0"
          "NO_ISSUER_DETAILS\\0"
          "NO_POLICY_IDENTIFIER\\0"
          "NO_PROXY_CERT_POLICY_LANGUAGE_DEFINED\\0"
          "NO_PUBLIC_KEY\\0"
          "NO_SUBJECT_DETAILS\\0"
          "ODD_NUMBER_OF_DIGITS\\0"
          "OPERATION_NOT_DEFINED\\0"
          "OTHERNAME_ERROR\\0"
          "POLICY_LANGUAGE_ALREADY_DEFINED\\0"
          "POLICY_PATH_LENGTH\\0"
          "POLICY_PATH_LENGTH_ALREADY_DEFINED\\0"
          "POLICY_WHEN_PROXY_LANGUAGE_REQUIRES_NO_POLICY\\0"
          "SECTION_NOT_FOUND\\0"
          "UNABLE_TO_GET_ISSUER_DETAILS\\0"
          "UNABLE_TO_GET_ISSUER_KEYID\\0"
          "UNKNOWN_BIT_STRING_ARGUMENT\\0"
          "UNKNOWN_EXTENSION\\0"
          "UNKNOWN_EXTENSION_NAME\\0"
          "UNKNOWN_OPTION\\0"
          "UNSUPPORTED_OPTION\\0"
          "USER_TOO_LONG\\0"
          "";
      
    EOF

    # To avoid symbol conflict with OpenSSL, gRPC needs to rename all the BoringSSL symbols with a 
    # prefix. This is done with BoringSSL's BORINGSSL_PREFIX mechanism
    # (https://github.com/google/boringssl/blob/75148d7abf12bdd1797fec3c5da9a21963703516/BUILDING.md#building-with-prefixed-symbols).
    # The required prefix header file boringssl_prefix_symbols.h is not part of BoringSSL repo at
    # this moment. It has to be generated by BoringSSL's users and be injected to BoringSSL build.
    # gRPC generates this file in script /tools/distrib/upgrade_boringssl_objc.sh. This script
    # outputs a gzip+base64 encoded version of boringssl_prefix_symbols.h because of Cocoapods'
    # limit on the 'prepare_command' field length. The encoded header is put at
    # /src/boringssl/boringssl_prefix_symbols.h.gz.b64. Here we decode the content and inject
    # the header to the correct location in BoringSSL.
    base64 -D <<EOF | gunzip > include/openssl/boringssl_prefix_symbols.h
      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
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'
    # BoringSSL include boringssl_prefix_symbols.h without any prefix, which does not match the

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
