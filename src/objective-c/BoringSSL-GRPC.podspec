

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
    # limit on the 'prepare_command' field length. The encoded header is generated from
    # /src/boringssl/boringssl_prefix_symbols.h. Here we decode the content and inject the header to
    # the correct location in BoringSSL.
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
