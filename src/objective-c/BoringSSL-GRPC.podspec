

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

    # Grab prefix header from Github repo
    base64 -D <<EOF | gunzip > include/openssl/boringssl_prefix_symbols.h
      H4sICIwn110AA2JvcmluZ3NzbF9wcmVmaXhfc3ltYm9scy5oAK2dXXPbRpa/7/dTqP57s1s1tWvJ
      cSZzSUt0zI0taUg6G+8NCiRBCWOSYABQtvPp/9147ZdzGvgduCo1kwh4ngM2Gv2GRvd///fVbXb+
      nqdPz+XVf2z/8+rm1fUvf7v6NcueDsnV4rT9r3/77/9W/1w9JvkxLYo0O12V2dWlSP52tVXg366O
      2S7dq/+PT7v/zvKrXVqUebq5lMlV+ZwWV0W2L7/GeXK1Vwfj03ftOl/yc1YkV1/T8vlK/Vn/f3Yp
      r/ZJcqWQ5yRPNt+vnvL4VCa7v12d8+wl3SU7JYxL9T/JVbzJXhJt2nbXfsrKdJvoq6jjnvvrbQ+d
      z0mcX6Wnq/hw0GSaFO2vW7+fX60e3q3/d7acXy1WV4/Lh98Xd/O7q/83W6n//n9Xs/u76qTZp/X7
      h+XV3WJ1+2G2+Li6mn34cKWo5ex+vZivtOt/F+v3V8v5r7OlQh4UpXy9+/72w6e7xf2vFbj4+Phh
      oaL0gquHd9rxcb68fa/+Mnu7+LBYf67Cv1us7+er1X8px9X9w9X89/n9+mr1XnuMK3s7v/qwmL39
      ML96p/5rdv9Z61aP89vF7MPf1HUv57frvylF+2/qpNuH+9X8n5+UTp1zdTf7OPtVX0hFt/9Z/bD3
      s/XqQcVdqp+3+vRhrX/Gu+XDx6sPDyt95VefVnMVY7aeaVqlobrk1d8UN1cXuNTXPVP/3K4XD/fa
      pwAVer2c6eu4n//6YfHr/P52rtmHClg/LNW5n1YN87er2XKx0kEfPq01/aCd2qQ09/PqnDr1dXqo
      a6muYr5UCfFxVonf2Xfjv/5Nw28flsq5Wn2IZnd30eNy/m7xx9U5LsqkuCq/Zlcq653KdJ8meaEy
      j8r82SlRN6HUWUxl6mOh/6BFaZnkcalzXLa/OsbbPLtKvp3jU5UJ1T9pWVzF+dPlqHzF1SZRcFIF
      Sk9P//Vv/75L9ukpIS/nP+K/XW3+kzwULdRPX9YnBB3miVfx1b//+1Wk/2fzbz21eIj2UVEc6Gvo
      /1j/4W898J+Wo0hK1NIgvedu/WEVbQ+pSqromKjiYTdW55OOVaAjPUWSvyS5RGeRjlWXhdHmst+r
      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
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'
    # BoringSSL include boringssl_prefix_symbols.h without any prefix, which does not match the

    # Xcode import style. We add it here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
