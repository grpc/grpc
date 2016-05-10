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
  s.name     = 'BoringSSL'
  s.version  = '3.0'
  s.summary  = 'BoringSSL is a fork of OpenSSL that is designed to meet Google’s needs.'
  # Adapted from the homepage:
  s.description = <<-DESC
    BoringSSL is a fork of OpenSSL that is designed to meet Google’s needs.

    Although BoringSSL is an open source project, it is not intended for general use, as OpenSSL is.
    We don’t recommend that third parties depend upon it. Doing so is likely to be frustrating
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
    up a large number of patches that were maintained while tracking upstream OpenSSL. As Google’s
    product portfolio became more complex, more copies of OpenSSL sprung up and the effort involved
    in maintaining all these patches in multiple places was growing steadily.

    Currently BoringSSL is the SSL library in Chrome/Chromium, Android (but it’s not part of the
    NDK) and a number of other apps/programs.
  DESC
  s.homepage = 'https://boringssl.googlesource.com/boringssl/'
  s.documentation_url = 'https://commondatastorage.googleapis.com/chromium-boringssl-docs/headers.html'
  s.license  = { :type => 'Mixed', :file => 'LICENSE' }
  # "The name and email addresses of the library maintainers, not the Podspec maintainer."
  s.authors  = 'Adam Langley', 'David Benjamin', 'Matt Braithwaite'

  s.source = { :git => 'https://boringssl.googlesource.com/boringssl',
               :tag => 'version_for_cocoapods_3.0' }

  s.source_files = 'ssl/*.{h,c}',
                   'ssl/**/*.{h,c}',
                   '*.{h,c}',
                   'crypto/*.{h,c}',
                   'crypto/**/*.{h,c}',
                   'include/openssl/*.h'

  s.public_header_files = 'include/openssl/*.h'
  s.header_mappings_dir = 'include'

  s.exclude_files = "**/*_test.*"

  # We don't need to inhibit all warnings; only -Wno-shorten-64-to-32. But Cocoapods' linter doesn't
  # want that for some reason.
  s.compiler_flags = '-DOPENSSL_NO_ASM', '-GCC_WARN_INHIBIT_ALL_WARNINGS', '-w'
  s.requires_arc = false

  s.prepare_command = <<-END_OF_COMMAND
    # Replace "const BIGNUM *I" in rsa.h with a lowercase i, as the former fails when including
    # OpenSSL in a Swift bridging header (complex.h defines "I", and it's as if the compiler
    # included it in every bridged header).
    sed -E -i '.back' 's/\\*I,/*i,/g' include/openssl/rsa.h

    # This is a bit ridiculous, but requiring people to install Go in order to build is slightly
    # more ridiculous IMO. To save you from scrolling, this is the last part of the podspec.
    # TODO(jcanizales): Translate err_data_generate.go into a Bash or Ruby script.
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


      OPENSSL_COMPILE_ASSERT(ERR_LIB_NONE == 1, library_values_changed_1);
      OPENSSL_COMPILE_ASSERT(ERR_LIB_SYS == 2, library_values_changed_2);
      OPENSSL_COMPILE_ASSERT(ERR_LIB_BN == 3, library_values_changed_3);
      OPENSSL_COMPILE_ASSERT(ERR_LIB_RSA == 4, library_values_changed_4);
      OPENSSL_COMPILE_ASSERT(ERR_LIB_DH == 5, library_values_changed_5);
      OPENSSL_COMPILE_ASSERT(ERR_LIB_EVP == 6, library_values_changed_6);
      OPENSSL_COMPILE_ASSERT(ERR_LIB_BUF == 7, library_values_changed_7);
      OPENSSL_COMPILE_ASSERT(ERR_LIB_OBJ == 8, library_values_changed_8);
      OPENSSL_COMPILE_ASSERT(ERR_LIB_PEM == 9, library_values_changed_9);
      OPENSSL_COMPILE_ASSERT(ERR_LIB_DSA == 10, library_values_changed_10);
      OPENSSL_COMPILE_ASSERT(ERR_LIB_X509 == 11, library_values_changed_11);
      OPENSSL_COMPILE_ASSERT(ERR_LIB_ASN1 == 12, library_values_changed_12);
      OPENSSL_COMPILE_ASSERT(ERR_LIB_CONF == 13, library_values_changed_13);
      OPENSSL_COMPILE_ASSERT(ERR_LIB_CRYPTO == 14, library_values_changed_14);
      OPENSSL_COMPILE_ASSERT(ERR_LIB_EC == 15, library_values_changed_15);
      OPENSSL_COMPILE_ASSERT(ERR_LIB_SSL == 16, library_values_changed_16);
      OPENSSL_COMPILE_ASSERT(ERR_LIB_BIO == 17, library_values_changed_17);
      OPENSSL_COMPILE_ASSERT(ERR_LIB_PKCS7 == 18, library_values_changed_18);
      OPENSSL_COMPILE_ASSERT(ERR_LIB_PKCS8 == 19, library_values_changed_19);
      OPENSSL_COMPILE_ASSERT(ERR_LIB_X509V3 == 20, library_values_changed_20);
      OPENSSL_COMPILE_ASSERT(ERR_LIB_RAND == 21, library_values_changed_21);
      OPENSSL_COMPILE_ASSERT(ERR_LIB_ENGINE == 22, library_values_changed_22);
      OPENSSL_COMPILE_ASSERT(ERR_LIB_OCSP == 23, library_values_changed_23);
      OPENSSL_COMPILE_ASSERT(ERR_LIB_UI == 24, library_values_changed_24);
      OPENSSL_COMPILE_ASSERT(ERR_LIB_COMP == 25, library_values_changed_25);
      OPENSSL_COMPILE_ASSERT(ERR_LIB_ECDSA == 26, library_values_changed_26);
      OPENSSL_COMPILE_ASSERT(ERR_LIB_ECDH == 27, library_values_changed_27);
      OPENSSL_COMPILE_ASSERT(ERR_LIB_HMAC == 28, library_values_changed_28);
      OPENSSL_COMPILE_ASSERT(ERR_LIB_DIGEST == 29, library_values_changed_29);
      OPENSSL_COMPILE_ASSERT(ERR_LIB_CIPHER == 30, library_values_changed_30);
      OPENSSL_COMPILE_ASSERT(ERR_LIB_HKDF == 31, library_values_changed_31);
      OPENSSL_COMPILE_ASSERT(ERR_LIB_USER == 32, library_values_changed_32);
      OPENSSL_COMPILE_ASSERT(ERR_NUM_LIBS == 33, library_values_changed_num);

      const uint32_t kOpenSSLReasonValues[] = {
          0xc3207ba,
          0xc3287d4,
          0xc3307e3,
          0xc3387f3,
          0xc340802,
          0xc34881b,
          0xc350827,
          0xc358844,
          0xc360856,
          0xc368864,
          0xc370874,
          0xc378881,
          0xc380891,
          0xc38889c,
          0xc3908b2,
          0xc3988c1,
          0xc3a08d5,
          0xc3a87c7,
          0xc3b00b0,
          0x10321478,
          0x10329484,
          0x1033149d,
          0x103394b0,
          0x10340de1,
          0x103494cf,
          0x103514e4,
          0x10359516,
          0x1036152f,
          0x10369544,
          0x10371562,
          0x10379571,
          0x1038158d,
          0x103895a8,
          0x103915b7,
          0x103995d3,
          0x103a15ee,
          0x103a9605,
          0x103b1616,
          0x103b962a,
          0x103c1649,
          0x103c9658,
          0x103d166f,
          0x103d9682,
          0x103e0b6c,
          0x103e96b3,
          0x103f16c6,
          0x103f96e0,
          0x104016f0,
          0x10409704,
          0x1041171a,
          0x10419732,
          0x10421747,
          0x1042975b,
          0x1043176d,
          0x104385d0,
          0x104408c1,
          0x10449782,
          0x10451799,
          0x104597ae,
          0x104617bc,
          0x10469695,
          0x104714f7,
          0x104787c7,
          0x104800b0,
          0x104894c3,
          0x14320b4f,
          0x14328b5d,
          0x14330b6c,
          0x14338b7e,
          0x18320083,
          0x18328e47,
          0x18340e75,
          0x18348e89,
          0x18358ec0,
          0x18368eed,
          0x18370f00,
          0x18378f14,
          0x18380f38,
          0x18388f46,
          0x18390f5c,
          0x18398f70,
          0x183a0f80,
          0x183b0f90,
          0x183b8fa5,
          0x183c8fd0,
          0x183d0fe4,
          0x183d8ff4,
          0x183e0b9b,
          0x183e9001,
          0x183f1013,
          0x183f901e,
          0x1840102e,
          0x1840903f,
          0x18411050,
          0x18419062,
          0x1842108b,
          0x184290bd,
          0x184310cc,
          0x18451135,
          0x1845914b,
          0x18461166,
          0x18468ed8,
          0x184709d9,
          0x18478094,
          0x18480fbc,
          0x18489101,
          0x18490e5d,
          0x18498e9e,
          0x184a119c,
          0x184a9119,
          0x184b10e0,
          0x184b8e37,
          0x184c10a4,
          0x184c866b,
          0x184d1181,
          0x203211c3,
          0x243211cf,
          0x24328907,
          0x243311e1,
          0x243391ee,
          0x243411fb,
          0x2434920d,
          0x2435121c,
          0x24359239,
          0x24361246,
          0x24369254,
          0x24371262,
          0x24379270,
          0x24381279,
          0x24389286,
          0x24391299,
          0x28320b8f,
          0x28328b9b,
          0x28330b6c,
          0x28338bae,
          0x2c322c0b,
          0x2c32ac19,
          0x2c332c2b,
          0x2c33ac3d,
          0x2c342c51,
          0x2c34ac63,
          0x2c352c7e,
          0x2c35ac90,
          0x2c362ca3,
          0x2c3682f3,
          0x2c372cb0,
          0x2c37acc2,
          0x2c382cd5,
          0x2c38ace3,
          0x2c392cf3,
          0x2c39ad05,
          0x2c3a2d19,
          0x2c3aad2a,
          0x2c3b1359,
          0x2c3bad3b,
          0x2c3c2d4f,
          0x2c3cad65,
          0x2c3d2d7e,
          0x2c3dadac,
          0x2c3e2dba,
          0x2c3eadd2,
          0x2c3f2dea,
          0x2c3fadf7,
          0x2c402e1a,
          0x2c40ae39,
          0x2c4111c3,
          0x2c41ae4a,
          0x2c422e5d,
          0x2c429135,
          0x2c432e6e,
          0x2c4386a2,
          0x2c442d9b,
          0x30320000,
          0x30328015,
          0x3033001f,
          0x30338038,
          0x3034004a,
          0x30348064,
          0x3035006b,
          0x30358083,
          0x30360094,
          0x303680a1,
          0x303700b0,
          0x303780bd,
          0x303800d0,
          0x303880eb,
          0x30390100,
          0x30398114,
          0x303a0128,
          0x303a8139,
          0x303b0152,
          0x303b816f,
          0x303c017d,
          0x303c8191,
          0x303d01a1,
          0x303d81ba,
          0x303e01ca,
          0x303e81dd,
          0x303f01ec,
          0x303f81f8,
          0x3040020d,
          0x3040821d,
          0x30410234,
          0x30418241,
          0x30420254,
          0x30428263,
          0x30430278,
          0x30438299,
          0x304402ac,
          0x304482bf,
          0x304502d8,
          0x304582f3,
          0x30460310,
          0x30468329,
          0x30470337,
          0x30478348,
          0x30480357,
          0x3048836f,
          0x30490381,
          0x30498395,
          0x304a03b4,
          0x304a83c7,
          0x304b03d2,
          0x304b83e1,
          0x304c03f2,
          0x304c83fe,
          0x304d0414,
          0x304d8422,
          0x304e0438,
          0x304e844a,
          0x304f045c,
          0x304f846f,
          0x30500482,
          0x30508493,
          0x305104a3,
          0x305184bb,
          0x305204d0,
          0x305284e8,
          0x305304fc,
          0x30538514,
          0x3054052d,
          0x30548546,
          0x30550563,
          0x3055856e,
          0x30560586,
          0x30568596,
          0x305705a7,
          0x305785ba,
          0x305805d0,
          0x305885d9,
          0x305905ee,
          0x30598601,
          0x305a0610,
          0x305a8630,
          0x305b063f,
          0x305b864b,
          0x305c066b,
          0x305c8687,
          0x305d0698,
          0x305d86a2,
          0x34320ac9,
          0x34328add,
          0x34330afa,
          0x34338b0d,
          0x34340b1c,
          0x34348b39,
          0x3c320083,
          0x3c328bd8,
          0x3c330bf1,
          0x3c338c0c,
          0x3c340c29,
          0x3c348c44,
          0x3c350c5f,
          0x3c358c74,
          0x3c360c8d,
          0x3c368ca5,
          0x3c370cb6,
          0x3c378cc4,
          0x3c380cd1,
          0x3c388ce5,
          0x3c390b9b,
          0x3c398cf9,
          0x3c3a0d0d,
          0x3c3a8881,
          0x3c3b0d1d,
          0x3c3b8d38,
          0x3c3c0d4a,
          0x3c3c8d60,
          0x3c3d0d6a,
          0x3c3d8d7e,
          0x3c3e0d8c,
          0x3c3e8db1,
          0x3c3f0bc4,
          0x3c3f8d9a,
          0x403217d3,
          0x403297e9,
          0x40331817,
          0x40339821,
          0x40341838,
          0x40349856,
          0x40351866,
          0x40359878,
          0x40361885,
          0x40369891,
          0x403718a6,
          0x403798bb,
          0x403818cd,
          0x403898d8,
          0x403918ea,
          0x40398de1,
          0x403a18fa,
          0x403a990d,
          0x403b192e,
          0x403b993f,
          0x403c194f,
          0x403c8064,
          0x403d195b,
          0x403d9977,
          0x403e198d,
          0x403e999c,
          0x403f19af,
          0x403f99c9,
          0x404019d7,
          0x404099ec,
          0x40411a00,
          0x40419a1d,
          0x40421a36,
          0x40429a51,
          0x40431a6a,
          0x40439a7d,
          0x40441a91,
          0x40449aa9,
          0x40451af4,
          0x40459b02,
          0x40461b20,
          0x40468094,
          0x40471b35,
          0x40479b47,
          0x40481b6b,
          0x40489b99,
          0x40491bad,
          0x40499bc2,
          0x404a1bdb,
          0x404a9c15,
          0x404b1c46,
          0x404b9c7c,
          0x404c1c97,
          0x404c9cb1,
          0x404d1cc8,
          0x404d9cf0,
          0x404e1d07,
          0x404e9d23,
          0x404f1d3f,
          0x404f9d60,
          0x40501d82,
          0x40509d9e,
          0x40511db2,
          0x40519dbf,
          0x40521dd6,
          0x40529de6,
          0x40531df6,
          0x40539e0a,
          0x40541e25,
          0x40549e35,
          0x40551e4c,
          0x40559e5b,
          0x40561e88,
          0x40569ea0,
          0x40571ebc,
          0x40579ed5,
          0x40581ee8,
          0x40589efd,
          0x40591f20,
          0x40599f4b,
          0x405a1f58,
          0x405a9f71,
          0x405b1f89,
          0x405b9f9c,
          0x405c1fb1,
          0x405c9fc3,
          0x405d1fd8,
          0x405d9fe8,
          0x405e2001,
          0x405ea015,
          0x405f2025,
          0x405fa03d,
          0x4060204e,
          0x4060a061,
          0x40612072,
          0x4061a090,
          0x406220a1,
          0x4062a0ae,
          0x406320c5,
          0x4063a106,
          0x4064211d,
          0x4064a12a,
          0x40652138,
          0x4065a15a,
          0x40662182,
          0x4066a197,
          0x406721ae,
          0x4067a1bf,
          0x406821d0,
          0x4068a1e1,
          0x406921f6,
          0x4069a20d,
          0x406a221e,
          0x406aa237,
          0x406b2252,
          0x406ba269,
          0x406c22d6,
          0x406ca2f7,
          0x406d230a,
          0x406da32b,
          0x406e2346,
          0x406ea38f,
          0x406f23b0,
          0x406fa3d6,
          0x407023f6,
          0x4070a412,
          0x4071259f,
          0x4071a5c2,
          0x407225d8,
          0x4072a5f7,
          0x4073260f,
          0x4073a62f,
          0x40742859,
          0x4074a87e,
          0x40752899,
          0x4075a8b8,
          0x407628e7,
          0x4076a90f,
          0x40772940,
          0x4077a95f,
          0x40782999,
          0x4078a9b0,
          0x407929c3,
          0x4079a9e0,
          0x407a0782,
          0x407aa9f2,
          0x407b2a05,
          0x407baa1e,
          0x407c2a36,
          0x407c90bd,
          0x407d2a4a,
          0x407daa64,
          0x407e2a75,
          0x407eaa89,
          0x407f2a97,
          0x407faab2,
          0x40801286,
          0x4080aad7,
          0x40812af9,
          0x4081ab14,
          0x40822b29,
          0x4082ab41,
          0x40832b59,
          0x4083ab70,
          0x40842b86,
          0x4084ab92,
          0x40852ba5,
          0x4085abba,
          0x40862bcc,
          0x4086abe1,
          0x40872bea,
          0x40879cde,
          0x40880083,
          0x4088a0e5,
          0x40890a17,
          0x4089a281,
          0x408a1bfe,
          0x408aa2ab,
          0x408b2928,
          0x408ba984,
          0x408c2361,
          0x408c9c2f,
          0x408d1c64,
          0x408d9e76,
          0x408e1ab9,
          0x408e9add,
          0x408f1f2e,
          0x408f9b8b,
          0x41f424ca,
          0x41f9255c,
          0x41fe244f,
          0x41fea680,
          0x41ff2771,
          0x420324e3,
          0x42082505,
          0x4208a541,
          0x42092433,
          0x4209a57b,
          0x420a248a,
          0x420aa46a,
          0x420b24aa,
          0x420ba523,
          0x420c278d,
          0x420ca64d,
          0x420d2667,
          0x420da69e,
          0x421226b8,
          0x42172754,
          0x4217a6fa,
          0x421c271c,
          0x421f26d7,
          0x422127a4,
          0x42262737,
          0x422b283d,
          0x422ba806,
          0x422c2825,
          0x422ca7e0,
          0x422d27bf,
          0x443206ad,
          0x443286bc,
          0x443306c8,
          0x443386d6,
          0x443406e9,
          0x443486fa,
          0x44350701,
          0x4435870b,
          0x4436071e,
          0x44368734,
          0x44370746,
          0x44378753,
          0x44380762,
          0x4438876a,
          0x44390782,
          0x44398790,
          0x443a07a3,
          0x4c3212b0,
          0x4c3292c0,
          0x4c3312d3,
          0x4c3392f3,
          0x4c340094,
          0x4c3480b0,
          0x4c3512ff,
          0x4c35930d,
          0x4c361329,
          0x4c36933c,
          0x4c37134b,
          0x4c379359,
          0x4c38136e,
          0x4c38937a,
          0x4c39139a,
          0x4c3993c4,
          0x4c3a13dd,
          0x4c3a93f6,
          0x4c3b05d0,
          0x4c3b940f,
          0x4c3c1421,
          0x4c3c9430,
          0x4c3d10bd,
          0x4c3d9449,
          0x4c3e1456,
          0x50322e80,
          0x5032ae8f,
          0x50332e9a,
          0x5033aeaa,
          0x50342ec3,
          0x5034aedd,
          0x50352eeb,
          0x5035af01,
          0x50362f13,
          0x5036af29,
          0x50372f42,
          0x5037af55,
          0x50382f6d,
          0x5038af7e,
          0x50392f93,
          0x5039afa7,
          0x503a2fc7,
          0x503aafdd,
          0x503b2ff5,
          0x503bb007,
          0x503c3023,
          0x503cb03a,
          0x503d3053,
          0x503db069,
          0x503e3076,
          0x503eb08c,
          0x503f309e,
          0x503f8348,
          0x504030b1,
          0x5040b0c1,
          0x504130db,
          0x5041b0ea,
          0x50423104,
          0x5042b121,
          0x50433131,
          0x5043b141,
          0x50443150,
          0x50448414,
          0x50453164,
          0x5045b182,
          0x50463195,
          0x5046b1ab,
          0x504731bd,
          0x5047b1d2,
          0x504831f8,
          0x5048b206,
          0x50493219,
          0x5049b22e,
          0x504a3244,
          0x504ab254,
          0x504b3274,
          0x504bb287,
          0x504c32aa,
          0x504cb2d8,
          0x504d32ea,
          0x504db307,
          0x504e3322,
          0x504eb33e,
          0x504f3350,
          0x504fb367,
          0x50503376,
          0x50508687,
          0x50513389,
          0x58320e1f,
          0x68320de1,
          0x68328b9b,
          0x68330bae,
          0x68338def,
          0x68340dff,
          0x683480b0,
          0x6c320dbd,
          0x6c328b7e,
          0x6c330dc8,
          0x7432098d,
          0x783208f2,
          0x78328907,
          0x78330913,
          0x78338083,
          0x78340922,
          0x78348937,
          0x78350956,
          0x78358978,
          0x7836098d,
          0x783689a3,
          0x783709b3,
          0x783789c6,
          0x783809d9,
          0x783889eb,
          0x783909f8,
          0x78398a17,
          0x783a0a2c,
          0x783a8a3a,
          0x783b0a44,
          0x783b8a58,
          0x783c0a6f,
          0x783c8a84,
          0x783d0a9b,
          0x783d8ab0,
          0x783e0a06,
          0x7c3211b2,
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
          "DECODE_ERROR\\0"
          "DEPTH_EXCEEDED\\0"
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
          "INVALID_BMPSTRING_LENGTH\\0"
          "INVALID_DIGIT\\0"
          "INVALID_MODIFIER\\0"
          "INVALID_NUMBER\\0"
          "INVALID_OBJECT_ENCODING\\0"
          "INVALID_SEPARATOR\\0"
          "INVALID_TIME_FORMAT\\0"
          "INVALID_UNIVERSALSTRING_LENGTH\\0"
          "INVALID_UTF8STRING\\0"
          "LIST_ERROR\\0"
          "MALLOC_FAILURE\\0"
          "MISSING_ASN1_EOS\\0"
          "MISSING_EOC\\0"
          "MISSING_SECOND_NUMBER\\0"
          "MISSING_VALUE\\0"
          "MSTRING_NOT_UNIVERSAL\\0"
          "MSTRING_WRONG_TAG\\0"
          "NESTED_ASN1_ERROR\\0"
          "NESTED_ASN1_STRING\\0"
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
          "UNKNOWN_TAG\\0"
          "UNSUPPORTED_ANY_DEFINED_BY_TYPE\\0"
          "UNSUPPORTED_PUBLIC_KEY_TYPE\\0"
          "UNSUPPORTED_TYPE\\0"
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
          "VARIABLE_HAS_NO_VALUE\\0"
          "BAD_GENERATOR\\0"
          "INVALID_PUBKEY\\0"
          "MODULUS_TOO_LARGE\\0"
          "NO_PRIVATE_VALUE\\0"
          "BAD_Q_VALUE\\0"
          "MISSING_PARAMETERS\\0"
          "NEED_NEW_SETUP_VALUES\\0"
          "BIGNUM_OUT_OF_RANGE\\0"
          "COORDINATES_OUT_OF_RANGE\\0"
          "D2I_ECPKPARAMETERS_FAILURE\\0"
          "EC_GROUP_NEW_BY_NAME_FAILURE\\0"
          "GROUP2PKPARAMETERS_FAILURE\\0"
          "I2D_ECPKPARAMETERS_FAILURE\\0"
          "INCOMPATIBLE_OBJECTS\\0"
          "INVALID_COMPRESSED_POINT\\0"
          "INVALID_COMPRESSION_BIT\\0"
          "INVALID_ENCODING\\0"
          "INVALID_FIELD\\0"
          "INVALID_FORM\\0"
          "INVALID_GROUP_ORDER\\0"
          "INVALID_PRIVATE_KEY\\0"
          "MISSING_PRIVATE_KEY\\0"
          "NON_NAMED_CURVE\\0"
          "PKPARAMETERS2GROUP_FAILURE\\0"
          "POINT_AT_INFINITY\\0"
          "POINT_IS_NOT_ON_CURVE\\0"
          "SLOT_FULL\\0"
          "UNDEFINED_GENERATOR\\0"
          "UNKNOWN_GROUP\\0"
          "UNKNOWN_ORDER\\0"
          "WRONG_CURVE_PARAMETERS\\0"
          "WRONG_ORDER\\0"
          "KDF_FAILED\\0"
          "POINT_ARITHMETIC_FAILURE\\0"
          "BAD_SIGNATURE\\0"
          "NOT_IMPLEMENTED\\0"
          "RANDOM_NUMBER_GENERATION_FAILED\\0"
          "OPERATION_NOT_SUPPORTED\\0"
          "BN_DECODE_ERROR\\0"
          "COMMAND_NOT_SUPPORTED\\0"
          "CONTEXT_NOT_INITIALISED\\0"
          "DIFFERENT_KEY_TYPES\\0"
          "DIFFERENT_PARAMETERS\\0"
          "DIGEST_AND_KEY_TYPE_NOT_SUPPORTED\\0"
          "EXPECTING_AN_EC_KEY_KEY\\0"
          "EXPECTING_AN_RSA_KEY\\0"
          "EXPECTING_A_DH_KEY\\0"
          "EXPECTING_A_DSA_KEY\\0"
          "ILLEGAL_OR_UNSUPPORTED_PADDING_MODE\\0"
          "INVALID_CURVE\\0"
          "INVALID_DIGEST_LENGTH\\0"
          "INVALID_DIGEST_TYPE\\0"
          "INVALID_KEYBITS\\0"
          "INVALID_MGF1_MD\\0"
          "INVALID_PADDING_MODE\\0"
          "INVALID_PSS_PARAMETERS\\0"
          "INVALID_PSS_SALTLEN\\0"
          "INVALID_SALT_LENGTH\\0"
          "INVALID_TRAILER\\0"
          "KEYS_NOT_SET\\0"
          "NO_DEFAULT_DIGEST\\0"
          "NO_KEY_SET\\0"
          "NO_MDC2_SUPPORT\\0"
          "NO_NID_FOR_CURVE\\0"
          "NO_OPERATION_SET\\0"
          "NO_PARAMETERS_SET\\0"
          "OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE\\0"
          "OPERATON_NOT_INITIALIZED\\0"
          "PARAMETER_ENCODING_ERROR\\0"
          "UNKNOWN_DIGEST\\0"
          "UNKNOWN_MASK_DIGEST\\0"
          "UNKNOWN_MESSAGE_DIGEST_ALGORITHM\\0"
          "UNKNOWN_PUBLIC_KEY_TYPE\\0"
          "UNKNOWN_SIGNATURE_ALGORITHM\\0"
          "UNSUPPORTED_ALGORITHM\\0"
          "UNSUPPORTED_MASK_ALGORITHM\\0"
          "UNSUPPORTED_MASK_PARAMETER\\0"
          "UNSUPPORTED_SIGNATURE_TYPE\\0"
          "WRONG_PUBLIC_KEY_TYPE\\0"
          "OUTPUT_TOO_LARGE\\0"
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
          "BAD_PKCS12_DATA\\0"
          "BAD_PKCS12_VERSION\\0"
          "CIPHER_HAS_NO_OBJECT_IDENTIFIER\\0"
          "CRYPT_ERROR\\0"
          "ENCRYPT_ERROR\\0"
          "ERROR_SETTING_CIPHER_PARAMS\\0"
          "INCORRECT_PASSWORD\\0"
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
          "UNKNOWN_HASH\\0"
          "UNSUPPORTED_PRIVATE_KEY_ALGORITHM\\0"
          "BAD_E_VALUE\\0"
          "BAD_FIXED_HEADER_DECRYPT\\0"
          "BAD_PAD_BYTE_COUNT\\0"
          "BAD_RSA_PARAMETERS\\0"
          "BAD_VERSION\\0"
          "BLOCK_TYPE_IS_NOT_01\\0"
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
          "APP_DATA_IN_HANDSHAKE\\0"
          "ATTEMPT_TO_REUSE_SESSION_IN_DIFFERENT_CONTEXT\\0"
          "BAD_ALERT\\0"
          "BAD_CHANGE_CIPHER_SPEC\\0"
          "BAD_DATA_RETURNED_BY_CALLBACK\\0"
          "BAD_DH_P_LENGTH\\0"
          "BAD_DIGEST_LENGTH\\0"
          "BAD_ECC_CERT\\0"
          "BAD_ECPOINT\\0"
          "BAD_HANDSHAKE_LENGTH\\0"
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
          "CANNOT_SERIALIZE_PUBLIC_KEY\\0"
          "CA_DN_LENGTH_MISMATCH\\0"
          "CA_DN_TOO_LONG\\0"
          "CCS_RECEIVED_EARLY\\0"
          "CERTIFICATE_VERIFY_FAILED\\0"
          "CERT_CB_ERROR\\0"
          "CERT_LENGTH_MISMATCH\\0"
          "CHANNEL_ID_NOT_P256\\0"
          "CHANNEL_ID_SIGNATURE_INVALID\\0"
          "CIPHER_CODE_WRONG_LENGTH\\0"
          "CIPHER_OR_HASH_UNAVAILABLE\\0"
          "CLIENTHELLO_PARSE_FAILED\\0"
          "CLIENTHELLO_TLSEXT\\0"
          "CONNECTION_REJECTED\\0"
          "CONNECTION_TYPE_NOT_SET\\0"
          "COOKIE_MISMATCH\\0"
          "CUSTOM_EXTENSION_CONTENTS_TOO_LARGE\\0"
          "CUSTOM_EXTENSION_ERROR\\0"
          "D2I_ECDSA_SIG\\0"
          "DATA_BETWEEN_CCS_AND_FINISHED\\0"
          "DATA_LENGTH_TOO_LONG\\0"
          "DECRYPTION_FAILED\\0"
          "DECRYPTION_FAILED_OR_BAD_RECORD_MAC\\0"
          "DH_PUBLIC_VALUE_LENGTH_IS_WRONG\\0"
          "DH_P_TOO_LONG\\0"
          "DIGEST_CHECK_FAILED\\0"
          "DTLS_MESSAGE_TOO_BIG\\0"
          "ECC_CERT_NOT_FOR_SIGNING\\0"
          "EMPTY_SRTP_PROTECTION_PROFILE_LIST\\0"
          "EMS_STATE_INCONSISTENT\\0"
          "ENCRYPTED_LENGTH_TOO_LONG\\0"
          "ERROR_ADDING_EXTENSION\\0"
          "ERROR_IN_RECEIVED_CIPHER_LIST\\0"
          "ERROR_PARSING_EXTENSION\\0"
          "EVP_DIGESTSIGNFINAL_FAILED\\0"
          "EVP_DIGESTSIGNINIT_FAILED\\0"
          "EXCESSIVE_MESSAGE_SIZE\\0"
          "EXTRA_DATA_IN_MESSAGE\\0"
          "FRAGMENT_MISMATCH\\0"
          "GOT_A_FIN_BEFORE_A_CCS\\0"
          "GOT_CHANNEL_ID_BEFORE_A_CCS\\0"
          "GOT_NEXT_PROTO_BEFORE_A_CCS\\0"
          "GOT_NEXT_PROTO_WITHOUT_EXTENSION\\0"
          "HANDSHAKE_FAILURE_ON_CLIENT_HELLO\\0"
          "HANDSHAKE_RECORD_BEFORE_CCS\\0"
          "HTTPS_PROXY_REQUEST\\0"
          "HTTP_REQUEST\\0"
          "INAPPROPRIATE_FALLBACK\\0"
          "INVALID_COMMAND\\0"
          "INVALID_MESSAGE\\0"
          "INVALID_SSL_SESSION\\0"
          "INVALID_TICKET_KEYS_LENGTH\\0"
          "LENGTH_MISMATCH\\0"
          "LIBRARY_HAS_NO_CIPHERS\\0"
          "MISSING_DH_KEY\\0"
          "MISSING_ECDSA_SIGNING_CERT\\0"
          "MISSING_EXTENSION\\0"
          "MISSING_RSA_CERTIFICATE\\0"
          "MISSING_RSA_ENCRYPTING_CERT\\0"
          "MISSING_RSA_SIGNING_CERT\\0"
          "MISSING_TMP_DH_KEY\\0"
          "MISSING_TMP_ECDH_KEY\\0"
          "MIXED_SPECIAL_OPERATOR_WITH_GROUPS\\0"
          "MTU_TOO_SMALL\\0"
          "NEGOTIATED_BOTH_NPN_AND_ALPN\\0"
          "NESTED_GROUP\\0"
          "NO_CERTIFICATES_RETURNED\\0"
          "NO_CERTIFICATE_ASSIGNED\\0"
          "NO_CERTIFICATE_SET\\0"
          "NO_CIPHERS_AVAILABLE\\0"
          "NO_CIPHERS_PASSED\\0"
          "NO_CIPHERS_SPECIFIED\\0"
          "NO_CIPHER_MATCH\\0"
          "NO_COMPRESSION_SPECIFIED\\0"
          "NO_METHOD_SPECIFIED\\0"
          "NO_P256_SUPPORT\\0"
          "NO_PRIVATE_KEY_ASSIGNED\\0"
          "NO_RENEGOTIATION\\0"
          "NO_REQUIRED_DIGEST\\0"
          "NO_SHARED_CIPHER\\0"
          "NO_SHARED_SIGATURE_ALGORITHMS\\0"
          "NO_SRTP_PROFILES\\0"
          "NULL_SSL_CTX\\0"
          "NULL_SSL_METHOD_PASSED\\0"
          "OLD_SESSION_CIPHER_NOT_RETURNED\\0"
          "OLD_SESSION_VERSION_NOT_RETURNED\\0"
          "PACKET_LENGTH_TOO_LONG\\0"
          "PARSE_TLSEXT\\0"
          "PATH_TOO_LONG\\0"
          "PEER_DID_NOT_RETURN_A_CERTIFICATE\\0"
          "PEER_ERROR_UNSUPPORTED_CERTIFICATE_TYPE\\0"
          "PROTOCOL_IS_SHUTDOWN\\0"
          "PSK_IDENTITY_NOT_FOUND\\0"
          "PSK_NO_CLIENT_CB\\0"
          "PSK_NO_SERVER_CB\\0"
          "READ_BIO_NOT_SET\\0"
          "READ_TIMEOUT_EXPIRED\\0"
          "RECORD_LENGTH_MISMATCH\\0"
          "RECORD_TOO_LARGE\\0"
          "RENEGOTIATE_EXT_TOO_LONG\\0"
          "RENEGOTIATION_ENCODING_ERR\\0"
          "RENEGOTIATION_MISMATCH\\0"
          "REQUIRED_CIPHER_MISSING\\0"
          "RESUMED_EMS_SESSION_WITHOUT_EMS_EXTENSION\\0"
          "RESUMED_NON_EMS_SESSION_WITH_EMS_EXTENSION\\0"
          "SCSV_RECEIVED_WHEN_RENEGOTIATING\\0"
          "SERVERHELLO_TLSEXT\\0"
          "SESSION_ID_CONTEXT_UNINITIALIZED\\0"
          "SESSION_MAY_NOT_BE_CREATED\\0"
          "SIGNATURE_ALGORITHMS_ERROR\\0"
          "SIGNATURE_ALGORITHMS_EXTENSION_SENT_BY_SERVER\\0"
          "SRTP_COULD_NOT_ALLOCATE_PROFILES\\0"
          "SRTP_PROTECTION_PROFILE_LIST_TOO_LONG\\0"
          "SRTP_UNKNOWN_PROTECTION_PROFILE\\0"
          "SSL3_EXT_INVALID_SERVERNAME\\0"
          "SSL3_EXT_INVALID_SERVERNAME_TYPE\\0"
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
          "SSL_SESSION_ID_CALLBACK_FAILED\\0"
          "SSL_SESSION_ID_CONFLICT\\0"
          "SSL_SESSION_ID_CONTEXT_TOO_LONG\\0"
          "SSL_SESSION_ID_HAS_BAD_LENGTH\\0"
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
          "TLSV1_CERTIFICATE_UNOBTAINABLE\\0"
          "TLSV1_UNRECOGNIZED_NAME\\0"
          "TLSV1_UNSUPPORTED_EXTENSION\\0"
          "TLS_CLIENT_CERT_REQ_WITH_ANON_CIPHER\\0"
          "TLS_ILLEGAL_EXPORTER_LABEL\\0"
          "TLS_INVALID_ECPOINTFORMAT_LIST\\0"
          "TLS_PEER_DID_NOT_RESPOND_WITH_CERTIFICATE_LIST\\0"
          "TLS_RSA_ENCRYPTED_VALUE_LENGTH_IS_WRONG\\0"
          "TOO_MANY_EMPTY_FRAGMENTS\\0"
          "TOO_MANY_WARNING_ALERTS\\0"
          "UNABLE_TO_FIND_ECDH_PARAMETERS\\0"
          "UNABLE_TO_FIND_PUBLIC_KEY_PARAMETERS\\0"
          "UNEXPECTED_EXTENSION\\0"
          "UNEXPECTED_GROUP_CLOSE\\0"
          "UNEXPECTED_MESSAGE\\0"
          "UNEXPECTED_OPERATOR_IN_GROUP\\0"
          "UNEXPECTED_RECORD\\0"
          "UNKNOWN_ALERT_TYPE\\0"
          "UNKNOWN_CERTIFICATE_TYPE\\0"
          "UNKNOWN_CIPHER_RETURNED\\0"
          "UNKNOWN_CIPHER_TYPE\\0"
          "UNKNOWN_KEY_EXCHANGE_TYPE\\0"
          "UNKNOWN_PROTOCOL\\0"
          "UNKNOWN_SSL_VERSION\\0"
          "UNKNOWN_STATE\\0"
          "UNPROCESSED_HANDSHAKE_DATA\\0"
          "UNSAFE_LEGACY_RENEGOTIATION_DISABLED\\0"
          "UNSUPPORTED_COMPRESSION_ALGORITHM\\0"
          "UNSUPPORTED_ELLIPTIC_CURVE\\0"
          "UNSUPPORTED_PROTOCOL\\0"
          "UNSUPPORTED_SSL_VERSION\\0"
          "USE_SRTP_NOT_NEGOTIATED\\0"
          "WRONG_CERTIFICATE_TYPE\\0"
          "WRONG_CIPHER_RETURNED\\0"
          "WRONG_CURVE\\0"
          "WRONG_MESSAGE_TYPE\\0"
          "WRONG_SIGNATURE_TYPE\\0"
          "WRONG_SSL_VERSION\\0"
          "WRONG_VERSION_NUMBER\\0"
          "X509_LIB\\0"
          "X509_VERIFICATION_SETUP_PROBLEMS\\0"
          "AKID_MISMATCH\\0"
          "BAD_PKCS7_VERSION\\0"
          "BAD_X509_FILETYPE\\0"
          "BASE64_DECODE_ERROR\\0"
          "CANT_CHECK_DH_KEY\\0"
          "CERT_ALREADY_IN_HASH_TABLE\\0"
          "CRL_ALREADY_DELTA\\0"
          "CRL_VERIFY_FAILURE\\0"
          "IDP_MISMATCH\\0"
          "INVALID_DIRECTORY\\0"
          "INVALID_FIELD_NAME\\0"
          "INVALID_TRUST\\0"
          "ISSUER_MISMATCH\\0"
          "KEY_TYPE_MISMATCH\\0"
          "KEY_VALUES_MISMATCH\\0"
          "LOADING_CERT_DIR\\0"
          "LOADING_DEFAULTS\\0"
          "NEWER_CRL_NOT_NEWER\\0"
          "NOT_PKCS7_SIGNED_DATA\\0"
          "NO_CERTIFICATES_INCLUDED\\0"
          "NO_CERT_SET_FOR_US_TO_VERIFY\\0"
          "NO_CRLS_INCLUDED\\0"
          "NO_CRL_NUMBER\\0"
          "PUBLIC_KEY_DECODE_ERROR\\0"
          "PUBLIC_KEY_ENCODE_ERROR\\0"
          "SHOULD_RETRY\\0"
          "UNABLE_TO_FIND_PARAMETERS_IN_CHAIN\\0"
          "UNABLE_TO_GET_CERTS_PUBLIC_KEY\\0"
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
  END_OF_COMMAND
end
