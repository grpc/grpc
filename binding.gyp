# GRPC Node gyp file
# This currently builds the Node extension and dependencies
# This file has been automatically generated from a template file.
# Please look at the templates directory instead.
# This file can be regenerated from the template by running
# tools/buildgen/generate_projects.sh

# Copyright 2015 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Some of this file is built with the help of
# https://n8.io/converting-a-c-library-to-gyp/
{
  'variables': {
    'runtime%': 'node',
    # Some Node installations use the system installation of OpenSSL, and on
    # some systems, the system OpenSSL still does not have ALPN support. This
    # will let users recompile gRPC to work without ALPN.
    'grpc_alpn%': 'true',
    # Indicates that the library should be built with gcov.
    'grpc_gcov%': 'false',
    # Indicates that the library should be built with compatibility for musl
    # libc, so that it can run on Alpine Linux. This is only necessary if not
    # building on Alpine Linux
    'grpc_alpine%': 'false'
  },
  'target_defaults': {
    'configurations': {
      'Release': {
        'cflags': [
            '-O2',
        ],
        'defines': [
            'NDEBUG',
        ],
      },
      'Debug': {
        'cflags': [
            '-O0',
        ],
        'defines': [
            '_DEBUG',
            'DEBUG',
        ],
      },
    },
    'cflags': [
        '-g',
        '-Wall',
        '-Wextra',
        '-Werror',
        '-Wno-long-long',
        '-Wno-unused-parameter',
        '-DOSATOMIC_USE_INLINED=1',
    ],
    'ldflags': [
        '-g',
    ],
    'cflags_c': [
      '-Werror',
      '-std=c99'
    ],
    'cflags_cc': [
      '-Werror',
      '-std=c++11'
    ],
    'include_dirs': [
      '.',
      'include'
    ],
    'defines': [
      'GPR_BACKWARDS_COMPATIBILITY_MODE',
      'GRPC_ARES=0',
      'GRPC_UV'
    ],
    'conditions': [
      ['grpc_gcov=="true"', {
        'cflags': [
            '-O0',
            '-fprofile-arcs',
            '-ftest-coverage',
            '-Wno-return-type',
        ],
        'defines': [
            '_DEBUG',
            'DEBUG',
            'GPR_GCOV',
        ],
        'ldflags': [
            '-fprofile-arcs',
            '-ftest-coverage',
            '-rdynamic',
        ],
      }],
      ['grpc_alpine=="true"', {
        'defines': [
          'GPR_MUSL_LIBC_COMPAT'
        ]
      }],
      ['OS!="win" and runtime=="electron"', {
        "defines": [
          'OPENSSL_NO_THREADS'
        ]
      }],
      # This is the condition for using boringssl
      ['OS=="win" or runtime=="electron"', {
        "include_dirs": [
          "third_party/boringssl/include"
        ],
        "defines": [
          'OPENSSL_NO_ASM'
        ]
      }, {
        'conditions': [
          ["target_arch=='ia32'", {
             "include_dirs": [ "<(node_root_dir)/deps/openssl/config/piii" ]
          }],
          ["target_arch=='x64'", {
             "include_dirs": [ "<(node_root_dir)/deps/openssl/config/k8" ]
          }],
          ["target_arch=='arm'", {
             "include_dirs": [ "<(node_root_dir)/deps/openssl/config/arm" ]
          }],
          ['grpc_alpn=="true"', {
            'defines': [
              'TSI_OPENSSL_ALPN_SUPPORT=1'
            ],
          }, {
            'defines': [
              'TSI_OPENSSL_ALPN_SUPPORT=0'
            ],
          }]
        ],
        'include_dirs': [
          '<(node_root_dir)/deps/openssl/openssl/include',
        ]
      }],
      ['OS == "win"', {
        "include_dirs": [
          "third_party/zlib",
          "third_party/cares/cares"
        ],
        "defines": [
          '_WIN32_WINNT=0x0600',
          'WIN32_LEAN_AND_MEAN',
          '_HAS_EXCEPTIONS=0',
          'UNICODE',
          '_UNICODE',
          'NOMINMAX',
        ],
        "msvs_settings": {
          'VCCLCompilerTool': {
            'RuntimeLibrary': 1, # static debug
          }
        },
        "libraries": [
          "ws2_32"
        ]
      }, { # OS != "win"
        'include_dirs': [
          '<(node_root_dir)/deps/zlib',
          '<(node_root_dir)/deps/cares/include'
        ]
      }],
      ['OS == "mac"', {
        'xcode_settings': {
          'OTHER_CFLAGS': [
              '-g',
              '-Wall',
              '-Wextra',
              '-Werror',
              '-Wno-long-long',
              '-Wno-unused-parameter',
              '-DOSATOMIC_USE_INLINED=1',
          ],
          'OTHER_CPLUSPLUSFLAGS': [
              '-g',
              '-Wall',
              '-Wextra',
              '-Werror',
              '-Wno-long-long',
              '-Wno-unused-parameter',
              '-DOSATOMIC_USE_INLINED=1',
            '-stdlib=libc++',
            '-std=c++11',
            '-Wno-error=deprecated-declarations'
          ],
        },
      }]
    ]
  },
  'conditions': [
    ['OS=="win" or runtime=="electron"', {
      'targets': [
        {
          'target_name': 'boringssl',
          'product_prefix': 'lib',
          'type': 'static_library',
          'dependencies': [
          ],
          'sources': [
            'src/boringssl/err_data.c',
            'third_party/boringssl/crypto/aes/aes.c',
            'third_party/boringssl/crypto/aes/key_wrap.c',
            'third_party/boringssl/crypto/aes/mode_wrappers.c',
            'third_party/boringssl/crypto/asn1/a_bitstr.c',
            'third_party/boringssl/crypto/asn1/a_bool.c',
            'third_party/boringssl/crypto/asn1/a_d2i_fp.c',
            'third_party/boringssl/crypto/asn1/a_dup.c',
            'third_party/boringssl/crypto/asn1/a_enum.c',
            'third_party/boringssl/crypto/asn1/a_gentm.c',
            'third_party/boringssl/crypto/asn1/a_i2d_fp.c',
            'third_party/boringssl/crypto/asn1/a_int.c',
            'third_party/boringssl/crypto/asn1/a_mbstr.c',
            'third_party/boringssl/crypto/asn1/a_object.c',
            'third_party/boringssl/crypto/asn1/a_octet.c',
            'third_party/boringssl/crypto/asn1/a_print.c',
            'third_party/boringssl/crypto/asn1/a_strnid.c',
            'third_party/boringssl/crypto/asn1/a_time.c',
            'third_party/boringssl/crypto/asn1/a_type.c',
            'third_party/boringssl/crypto/asn1/a_utctm.c',
            'third_party/boringssl/crypto/asn1/a_utf8.c',
            'third_party/boringssl/crypto/asn1/asn1_lib.c',
            'third_party/boringssl/crypto/asn1/asn1_par.c',
            'third_party/boringssl/crypto/asn1/asn_pack.c',
            'third_party/boringssl/crypto/asn1/f_enum.c',
            'third_party/boringssl/crypto/asn1/f_int.c',
            'third_party/boringssl/crypto/asn1/f_string.c',
            'third_party/boringssl/crypto/asn1/t_bitst.c',
            'third_party/boringssl/crypto/asn1/tasn_dec.c',
            'third_party/boringssl/crypto/asn1/tasn_enc.c',
            'third_party/boringssl/crypto/asn1/tasn_fre.c',
            'third_party/boringssl/crypto/asn1/tasn_new.c',
            'third_party/boringssl/crypto/asn1/tasn_typ.c',
            'third_party/boringssl/crypto/asn1/tasn_utl.c',
            'third_party/boringssl/crypto/asn1/time_support.c',
            'third_party/boringssl/crypto/asn1/x_bignum.c',
            'third_party/boringssl/crypto/asn1/x_long.c',
            'third_party/boringssl/crypto/base64/base64.c',
            'third_party/boringssl/crypto/bio/bio.c',
            'third_party/boringssl/crypto/bio/bio_mem.c',
            'third_party/boringssl/crypto/bio/connect.c',
            'third_party/boringssl/crypto/bio/fd.c',
            'third_party/boringssl/crypto/bio/file.c',
            'third_party/boringssl/crypto/bio/hexdump.c',
            'third_party/boringssl/crypto/bio/pair.c',
            'third_party/boringssl/crypto/bio/printf.c',
            'third_party/boringssl/crypto/bio/socket.c',
            'third_party/boringssl/crypto/bio/socket_helper.c',
            'third_party/boringssl/crypto/bn/add.c',
            'third_party/boringssl/crypto/bn/asm/x86_64-gcc.c',
            'third_party/boringssl/crypto/bn/bn.c',
            'third_party/boringssl/crypto/bn/bn_asn1.c',
            'third_party/boringssl/crypto/bn/cmp.c',
            'third_party/boringssl/crypto/bn/convert.c',
            'third_party/boringssl/crypto/bn/ctx.c',
            'third_party/boringssl/crypto/bn/div.c',
            'third_party/boringssl/crypto/bn/exponentiation.c',
            'third_party/boringssl/crypto/bn/gcd.c',
            'third_party/boringssl/crypto/bn/generic.c',
            'third_party/boringssl/crypto/bn/kronecker.c',
            'third_party/boringssl/crypto/bn/montgomery.c',
            'third_party/boringssl/crypto/bn/montgomery_inv.c',
            'third_party/boringssl/crypto/bn/mul.c',
            'third_party/boringssl/crypto/bn/prime.c',
            'third_party/boringssl/crypto/bn/random.c',
            'third_party/boringssl/crypto/bn/rsaz_exp.c',
            'third_party/boringssl/crypto/bn/shift.c',
            'third_party/boringssl/crypto/bn/sqrt.c',
            'third_party/boringssl/crypto/buf/buf.c',
            'third_party/boringssl/crypto/bytestring/asn1_compat.c',
            'third_party/boringssl/crypto/bytestring/ber.c',
            'third_party/boringssl/crypto/bytestring/cbb.c',
            'third_party/boringssl/crypto/bytestring/cbs.c',
            'third_party/boringssl/crypto/chacha/chacha.c',
            'third_party/boringssl/crypto/cipher/aead.c',
            'third_party/boringssl/crypto/cipher/cipher.c',
            'third_party/boringssl/crypto/cipher/derive_key.c',
            'third_party/boringssl/crypto/cipher/e_aes.c',
            'third_party/boringssl/crypto/cipher/e_chacha20poly1305.c',
            'third_party/boringssl/crypto/cipher/e_des.c',
            'third_party/boringssl/crypto/cipher/e_null.c',
            'third_party/boringssl/crypto/cipher/e_rc2.c',
            'third_party/boringssl/crypto/cipher/e_rc4.c',
            'third_party/boringssl/crypto/cipher/e_ssl3.c',
            'third_party/boringssl/crypto/cipher/e_tls.c',
            'third_party/boringssl/crypto/cipher/tls_cbc.c',
            'third_party/boringssl/crypto/cmac/cmac.c',
            'third_party/boringssl/crypto/conf/conf.c',
            'third_party/boringssl/crypto/cpu-aarch64-linux.c',
            'third_party/boringssl/crypto/cpu-arm-linux.c',
            'third_party/boringssl/crypto/cpu-arm.c',
            'third_party/boringssl/crypto/cpu-intel.c',
            'third_party/boringssl/crypto/cpu-ppc64le.c',
            'third_party/boringssl/crypto/crypto.c',
            'third_party/boringssl/crypto/curve25519/curve25519.c',
            'third_party/boringssl/crypto/curve25519/spake25519.c',
            'third_party/boringssl/crypto/curve25519/x25519-x86_64.c',
            'third_party/boringssl/crypto/des/des.c',
            'third_party/boringssl/crypto/dh/check.c',
            'third_party/boringssl/crypto/dh/dh.c',
            'third_party/boringssl/crypto/dh/dh_asn1.c',
            'third_party/boringssl/crypto/dh/params.c',
            'third_party/boringssl/crypto/digest/digest.c',
            'third_party/boringssl/crypto/digest/digests.c',
            'third_party/boringssl/crypto/dsa/dsa.c',
            'third_party/boringssl/crypto/dsa/dsa_asn1.c',
            'third_party/boringssl/crypto/ec/ec.c',
            'third_party/boringssl/crypto/ec/ec_asn1.c',
            'third_party/boringssl/crypto/ec/ec_key.c',
            'third_party/boringssl/crypto/ec/ec_montgomery.c',
            'third_party/boringssl/crypto/ec/oct.c',
            'third_party/boringssl/crypto/ec/p224-64.c',
            'third_party/boringssl/crypto/ec/p256-64.c',
            'third_party/boringssl/crypto/ec/p256-x86_64.c',
            'third_party/boringssl/crypto/ec/simple.c',
            'third_party/boringssl/crypto/ec/util-64.c',
            'third_party/boringssl/crypto/ec/wnaf.c',
            'third_party/boringssl/crypto/ecdh/ecdh.c',
            'third_party/boringssl/crypto/ecdsa/ecdsa.c',
            'third_party/boringssl/crypto/ecdsa/ecdsa_asn1.c',
            'third_party/boringssl/crypto/engine/engine.c',
            'third_party/boringssl/crypto/err/err.c',
            'third_party/boringssl/crypto/evp/digestsign.c',
            'third_party/boringssl/crypto/evp/evp.c',
            'third_party/boringssl/crypto/evp/evp_asn1.c',
            'third_party/boringssl/crypto/evp/evp_ctx.c',
            'third_party/boringssl/crypto/evp/p_dsa_asn1.c',
            'third_party/boringssl/crypto/evp/p_ec.c',
            'third_party/boringssl/crypto/evp/p_ec_asn1.c',
            'third_party/boringssl/crypto/evp/p_rsa.c',
            'third_party/boringssl/crypto/evp/p_rsa_asn1.c',
            'third_party/boringssl/crypto/evp/pbkdf.c',
            'third_party/boringssl/crypto/evp/print.c',
            'third_party/boringssl/crypto/evp/sign.c',
            'third_party/boringssl/crypto/ex_data.c',
            'third_party/boringssl/crypto/hkdf/hkdf.c',
            'third_party/boringssl/crypto/hmac/hmac.c',
            'third_party/boringssl/crypto/lhash/lhash.c',
            'third_party/boringssl/crypto/md4/md4.c',
            'third_party/boringssl/crypto/md5/md5.c',
            'third_party/boringssl/crypto/mem.c',
            'third_party/boringssl/crypto/modes/cbc.c',
            'third_party/boringssl/crypto/modes/cfb.c',
            'third_party/boringssl/crypto/modes/ctr.c',
            'third_party/boringssl/crypto/modes/gcm.c',
            'third_party/boringssl/crypto/modes/ofb.c',
            'third_party/boringssl/crypto/modes/polyval.c',
            'third_party/boringssl/crypto/obj/obj.c',
            'third_party/boringssl/crypto/obj/obj_xref.c',
            'third_party/boringssl/crypto/pem/pem_all.c',
            'third_party/boringssl/crypto/pem/pem_info.c',
            'third_party/boringssl/crypto/pem/pem_lib.c',
            'third_party/boringssl/crypto/pem/pem_oth.c',
            'third_party/boringssl/crypto/pem/pem_pk8.c',
            'third_party/boringssl/crypto/pem/pem_pkey.c',
            'third_party/boringssl/crypto/pem/pem_x509.c',
            'third_party/boringssl/crypto/pem/pem_xaux.c',
            'third_party/boringssl/crypto/pkcs8/p5_pbev2.c',
            'third_party/boringssl/crypto/pkcs8/p8_pkey.c',
            'third_party/boringssl/crypto/pkcs8/pkcs8.c',
            'third_party/boringssl/crypto/poly1305/poly1305.c',
            'third_party/boringssl/crypto/poly1305/poly1305_arm.c',
            'third_party/boringssl/crypto/poly1305/poly1305_vec.c',
            'third_party/boringssl/crypto/pool/pool.c',
            'third_party/boringssl/crypto/rand/deterministic.c',
            'third_party/boringssl/crypto/rand/fuchsia.c',
            'third_party/boringssl/crypto/rand/rand.c',
            'third_party/boringssl/crypto/rand/urandom.c',
            'third_party/boringssl/crypto/rand/windows.c',
            'third_party/boringssl/crypto/rc4/rc4.c',
            'third_party/boringssl/crypto/refcount_c11.c',
            'third_party/boringssl/crypto/refcount_lock.c',
            'third_party/boringssl/crypto/rsa/blinding.c',
            'third_party/boringssl/crypto/rsa/padding.c',
            'third_party/boringssl/crypto/rsa/rsa.c',
            'third_party/boringssl/crypto/rsa/rsa_asn1.c',
            'third_party/boringssl/crypto/rsa/rsa_impl.c',
            'third_party/boringssl/crypto/sha/sha1-altivec.c',
            'third_party/boringssl/crypto/sha/sha1.c',
            'third_party/boringssl/crypto/sha/sha256.c',
            'third_party/boringssl/crypto/sha/sha512.c',
            'third_party/boringssl/crypto/stack/stack.c',
            'third_party/boringssl/crypto/thread.c',
            'third_party/boringssl/crypto/thread_none.c',
            'third_party/boringssl/crypto/thread_pthread.c',
            'third_party/boringssl/crypto/thread_win.c',
            'third_party/boringssl/crypto/x509/a_digest.c',
            'third_party/boringssl/crypto/x509/a_sign.c',
            'third_party/boringssl/crypto/x509/a_strex.c',
            'third_party/boringssl/crypto/x509/a_verify.c',
            'third_party/boringssl/crypto/x509/algorithm.c',
            'third_party/boringssl/crypto/x509/asn1_gen.c',
            'third_party/boringssl/crypto/x509/by_dir.c',
            'third_party/boringssl/crypto/x509/by_file.c',
            'third_party/boringssl/crypto/x509/i2d_pr.c',
            'third_party/boringssl/crypto/x509/pkcs7.c',
            'third_party/boringssl/crypto/x509/rsa_pss.c',
            'third_party/boringssl/crypto/x509/t_crl.c',
            'third_party/boringssl/crypto/x509/t_req.c',
            'third_party/boringssl/crypto/x509/t_x509.c',
            'third_party/boringssl/crypto/x509/t_x509a.c',
            'third_party/boringssl/crypto/x509/x509.c',
            'third_party/boringssl/crypto/x509/x509_att.c',
            'third_party/boringssl/crypto/x509/x509_cmp.c',
            'third_party/boringssl/crypto/x509/x509_d2.c',
            'third_party/boringssl/crypto/x509/x509_def.c',
            'third_party/boringssl/crypto/x509/x509_ext.c',
            'third_party/boringssl/crypto/x509/x509_lu.c',
            'third_party/boringssl/crypto/x509/x509_obj.c',
            'third_party/boringssl/crypto/x509/x509_r2x.c',
            'third_party/boringssl/crypto/x509/x509_req.c',
            'third_party/boringssl/crypto/x509/x509_set.c',
            'third_party/boringssl/crypto/x509/x509_trs.c',
            'third_party/boringssl/crypto/x509/x509_txt.c',
            'third_party/boringssl/crypto/x509/x509_v3.c',
            'third_party/boringssl/crypto/x509/x509_vfy.c',
            'third_party/boringssl/crypto/x509/x509_vpm.c',
            'third_party/boringssl/crypto/x509/x509cset.c',
            'third_party/boringssl/crypto/x509/x509name.c',
            'third_party/boringssl/crypto/x509/x509rset.c',
            'third_party/boringssl/crypto/x509/x509spki.c',
            'third_party/boringssl/crypto/x509/x509type.c',
            'third_party/boringssl/crypto/x509/x_algor.c',
            'third_party/boringssl/crypto/x509/x_all.c',
            'third_party/boringssl/crypto/x509/x_attrib.c',
            'third_party/boringssl/crypto/x509/x_crl.c',
            'third_party/boringssl/crypto/x509/x_exten.c',
            'third_party/boringssl/crypto/x509/x_info.c',
            'third_party/boringssl/crypto/x509/x_name.c',
            'third_party/boringssl/crypto/x509/x_pkey.c',
            'third_party/boringssl/crypto/x509/x_pubkey.c',
            'third_party/boringssl/crypto/x509/x_req.c',
            'third_party/boringssl/crypto/x509/x_sig.c',
            'third_party/boringssl/crypto/x509/x_spki.c',
            'third_party/boringssl/crypto/x509/x_val.c',
            'third_party/boringssl/crypto/x509/x_x509.c',
            'third_party/boringssl/crypto/x509/x_x509a.c',
            'third_party/boringssl/crypto/x509v3/pcy_cache.c',
            'third_party/boringssl/crypto/x509v3/pcy_data.c',
            'third_party/boringssl/crypto/x509v3/pcy_lib.c',
            'third_party/boringssl/crypto/x509v3/pcy_map.c',
            'third_party/boringssl/crypto/x509v3/pcy_node.c',
            'third_party/boringssl/crypto/x509v3/pcy_tree.c',
            'third_party/boringssl/crypto/x509v3/v3_akey.c',
            'third_party/boringssl/crypto/x509v3/v3_akeya.c',
            'third_party/boringssl/crypto/x509v3/v3_alt.c',
            'third_party/boringssl/crypto/x509v3/v3_bcons.c',
            'third_party/boringssl/crypto/x509v3/v3_bitst.c',
            'third_party/boringssl/crypto/x509v3/v3_conf.c',
            'third_party/boringssl/crypto/x509v3/v3_cpols.c',
            'third_party/boringssl/crypto/x509v3/v3_crld.c',
            'third_party/boringssl/crypto/x509v3/v3_enum.c',
            'third_party/boringssl/crypto/x509v3/v3_extku.c',
            'third_party/boringssl/crypto/x509v3/v3_genn.c',
            'third_party/boringssl/crypto/x509v3/v3_ia5.c',
            'third_party/boringssl/crypto/x509v3/v3_info.c',
            'third_party/boringssl/crypto/x509v3/v3_int.c',
            'third_party/boringssl/crypto/x509v3/v3_lib.c',
            'third_party/boringssl/crypto/x509v3/v3_ncons.c',
            'third_party/boringssl/crypto/x509v3/v3_pci.c',
            'third_party/boringssl/crypto/x509v3/v3_pcia.c',
            'third_party/boringssl/crypto/x509v3/v3_pcons.c',
            'third_party/boringssl/crypto/x509v3/v3_pku.c',
            'third_party/boringssl/crypto/x509v3/v3_pmaps.c',
            'third_party/boringssl/crypto/x509v3/v3_prn.c',
            'third_party/boringssl/crypto/x509v3/v3_purp.c',
            'third_party/boringssl/crypto/x509v3/v3_skey.c',
            'third_party/boringssl/crypto/x509v3/v3_sxnet.c',
            'third_party/boringssl/crypto/x509v3/v3_utl.c',
            'third_party/boringssl/ssl/bio_ssl.c',
            'third_party/boringssl/ssl/custom_extensions.c',
            'third_party/boringssl/ssl/d1_both.c',
            'third_party/boringssl/ssl/d1_lib.c',
            'third_party/boringssl/ssl/d1_pkt.c',
            'third_party/boringssl/ssl/d1_srtp.c',
            'third_party/boringssl/ssl/dtls_method.c',
            'third_party/boringssl/ssl/dtls_record.c',
            'third_party/boringssl/ssl/handshake_client.c',
            'third_party/boringssl/ssl/handshake_server.c',
            'third_party/boringssl/ssl/s3_both.c',
            'third_party/boringssl/ssl/s3_lib.c',
            'third_party/boringssl/ssl/s3_pkt.c',
            'third_party/boringssl/ssl/ssl_aead_ctx.c',
            'third_party/boringssl/ssl/ssl_asn1.c',
            'third_party/boringssl/ssl/ssl_buffer.c',
            'third_party/boringssl/ssl/ssl_cert.c',
            'third_party/boringssl/ssl/ssl_cipher.c',
            'third_party/boringssl/ssl/ssl_ecdh.c',
            'third_party/boringssl/ssl/ssl_file.c',
            'third_party/boringssl/ssl/ssl_lib.c',
            'third_party/boringssl/ssl/ssl_privkey.c',
            'third_party/boringssl/ssl/ssl_privkey_cc.cc',
            'third_party/boringssl/ssl/ssl_session.c',
            'third_party/boringssl/ssl/ssl_stat.c',
            'third_party/boringssl/ssl/ssl_transcript.c',
            'third_party/boringssl/ssl/ssl_x509.c',
            'third_party/boringssl/ssl/t1_enc.c',
            'third_party/boringssl/ssl/t1_lib.c',
            'third_party/boringssl/ssl/tls13_both.c',
            'third_party/boringssl/ssl/tls13_client.c',
            'third_party/boringssl/ssl/tls13_enc.c',
            'third_party/boringssl/ssl/tls13_server.c',
            'third_party/boringssl/ssl/tls_method.c',
            'third_party/boringssl/ssl/tls_record.c',
          ],
          'conditions': [
            ['OS == "mac"', {
              'xcode_settings': {
                'MACOSX_DEPLOYMENT_TARGET': '10.9'
              }
            }]
          ]
        },
      ],
    }],
    ['OS == "win" and runtime!="electron"', {
      'targets': [
        {
          # IMPORTANT WINDOWS BUILD INFORMATION
          # This library does not build on Windows without modifying the Node
          # development packages that node-gyp downloads in order to build.
          # Due to https://github.com/nodejs/node/issues/4932, the headers for
          # BoringSSL conflict with the OpenSSL headers included by default
          # when including the Node headers. The remedy for this is to remove
          # the OpenSSL headers, from the downloaded Node development package,
          # which is typically located in `.node-gyp` in your home directory.
          #
          # This is not true of Electron, which does not have OpenSSL headers.
          'target_name': 'WINDOWS_BUILD_WARNING',
          'rules': [
            {
              'rule_name': 'WINDOWS_BUILD_WARNING',
              'extension': 'S',
              'inputs': [
                'package.json'
              ],
              'outputs': [
                'ignore_this_part'
              ],
              'action': ['echo', 'IMPORTANT: Due to https://github.com/nodejs/node/issues/4932, to build this library on Windows, you must first remove <(node_root_dir)/include/node/openssl/']
            }
          ]
        },
      ]
    }],
    ['OS == "win"', {
      'targets': [
        # Only want to compile zlib under Windows
        {
          'target_name': 'z',
          'product_prefix': 'lib',
          'type': 'static_library',
          'dependencies': [
          ],
          'sources': [
            'third_party/zlib/adler32.c',
            'third_party/zlib/compress.c',
            'third_party/zlib/crc32.c',
            'third_party/zlib/deflate.c',
            'third_party/zlib/gzclose.c',
            'third_party/zlib/gzlib.c',
            'third_party/zlib/gzread.c',
            'third_party/zlib/gzwrite.c',
            'third_party/zlib/infback.c',
            'third_party/zlib/inffast.c',
            'third_party/zlib/inflate.c',
            'third_party/zlib/inftrees.c',
            'third_party/zlib/trees.c',
            'third_party/zlib/uncompr.c',
            'third_party/zlib/zutil.c',
          ]
        },
      ]
    }]
  ],
  'targets': [
    {
      'target_name': 'gpr',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
      ],
      'sources': [
        'src/core/lib/profiling/basic_timers.c',
        'src/core/lib/profiling/stap_timers.c',
        'src/core/lib/support/alloc.c',
        'src/core/lib/support/arena.c',
        'src/core/lib/support/atm.c',
        'src/core/lib/support/avl.c',
        'src/core/lib/support/backoff.c',
        'src/core/lib/support/cmdline.c',
        'src/core/lib/support/cpu_iphone.c',
        'src/core/lib/support/cpu_linux.c',
        'src/core/lib/support/cpu_posix.c',
        'src/core/lib/support/cpu_windows.c',
        'src/core/lib/support/env_linux.c',
        'src/core/lib/support/env_posix.c',
        'src/core/lib/support/env_windows.c',
        'src/core/lib/support/fork.c',
        'src/core/lib/support/histogram.c',
        'src/core/lib/support/host_port.c',
        'src/core/lib/support/log.c',
        'src/core/lib/support/log_android.c',
        'src/core/lib/support/log_linux.c',
        'src/core/lib/support/log_posix.c',
        'src/core/lib/support/log_windows.c',
        'src/core/lib/support/mpscq.c',
        'src/core/lib/support/murmur_hash.c',
        'src/core/lib/support/stack_lockfree.c',
        'src/core/lib/support/string.c',
        'src/core/lib/support/string_posix.c',
        'src/core/lib/support/string_util_windows.c',
        'src/core/lib/support/string_windows.c',
        'src/core/lib/support/subprocess_posix.c',
        'src/core/lib/support/subprocess_windows.c',
        'src/core/lib/support/sync.c',
        'src/core/lib/support/sync_posix.c',
        'src/core/lib/support/sync_windows.c',
        'src/core/lib/support/thd.c',
        'src/core/lib/support/thd_posix.c',
        'src/core/lib/support/thd_windows.c',
        'src/core/lib/support/time.c',
        'src/core/lib/support/time_posix.c',
        'src/core/lib/support/time_precise.c',
        'src/core/lib/support/time_windows.c',
        'src/core/lib/support/tls_pthread.c',
        'src/core/lib/support/tmpfile_msys.c',
        'src/core/lib/support/tmpfile_posix.c',
        'src/core/lib/support/tmpfile_windows.c',
        'src/core/lib/support/wrap_memcpy.c',
      ],
      'conditions': [
        ['OS == "mac"', {
          'xcode_settings': {
            'MACOSX_DEPLOYMENT_TARGET': '10.9'
          }
        }]
      ]
    },
    {
      'target_name': 'grpc',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'gpr',
      ],
      'sources': [
        'src/core/lib/surface/init.c',
        'src/core/lib/channel/channel_args.c',
        'src/core/lib/channel/channel_stack.c',
        'src/core/lib/channel/channel_stack_builder.c',
        'src/core/lib/channel/connected_channel.c',
        'src/core/lib/channel/handshaker.c',
        'src/core/lib/channel/handshaker_factory.c',
        'src/core/lib/channel/handshaker_registry.c',
        'src/core/lib/compression/compression.c',
        'src/core/lib/compression/message_compress.c',
        'src/core/lib/compression/stream_compression.c',
        'src/core/lib/compression/stream_compression_gzip.c',
        'src/core/lib/compression/stream_compression_identity.c',
        'src/core/lib/debug/stats.c',
        'src/core/lib/debug/stats_data.c',
        'src/core/lib/http/format_request.c',
        'src/core/lib/http/httpcli.c',
        'src/core/lib/http/parser.c',
        'src/core/lib/iomgr/call_combiner.c',
        'src/core/lib/iomgr/closure.c',
        'src/core/lib/iomgr/combiner.c',
        'src/core/lib/iomgr/endpoint.c',
        'src/core/lib/iomgr/endpoint_pair_posix.c',
        'src/core/lib/iomgr/endpoint_pair_uv.c',
        'src/core/lib/iomgr/endpoint_pair_windows.c',
        'src/core/lib/iomgr/error.c',
        'src/core/lib/iomgr/ev_epoll1_linux.c',
        'src/core/lib/iomgr/ev_epollex_linux.c',
        'src/core/lib/iomgr/ev_epollsig_linux.c',
        'src/core/lib/iomgr/ev_poll_posix.c',
        'src/core/lib/iomgr/ev_posix.c',
        'src/core/lib/iomgr/ev_windows.c',
        'src/core/lib/iomgr/exec_ctx.c',
        'src/core/lib/iomgr/executor.c',
        'src/core/lib/iomgr/fork_posix.c',
        'src/core/lib/iomgr/fork_windows.c',
        'src/core/lib/iomgr/gethostname_fallback.c',
        'src/core/lib/iomgr/gethostname_host_name_max.c',
        'src/core/lib/iomgr/gethostname_sysconf.c',
        'src/core/lib/iomgr/iocp_windows.c',
        'src/core/lib/iomgr/iomgr.c',
        'src/core/lib/iomgr/iomgr_posix.c',
        'src/core/lib/iomgr/iomgr_uv.c',
        'src/core/lib/iomgr/iomgr_windows.c',
        'src/core/lib/iomgr/is_epollexclusive_available.c',
        'src/core/lib/iomgr/load_file.c',
        'src/core/lib/iomgr/lockfree_event.c',
        'src/core/lib/iomgr/network_status_tracker.c',
        'src/core/lib/iomgr/polling_entity.c',
        'src/core/lib/iomgr/pollset_set_uv.c',
        'src/core/lib/iomgr/pollset_set_windows.c',
        'src/core/lib/iomgr/pollset_uv.c',
        'src/core/lib/iomgr/pollset_windows.c',
        'src/core/lib/iomgr/resolve_address_posix.c',
        'src/core/lib/iomgr/resolve_address_uv.c',
        'src/core/lib/iomgr/resolve_address_windows.c',
        'src/core/lib/iomgr/resource_quota.c',
        'src/core/lib/iomgr/sockaddr_utils.c',
        'src/core/lib/iomgr/socket_factory_posix.c',
        'src/core/lib/iomgr/socket_mutator.c',
        'src/core/lib/iomgr/socket_utils_common_posix.c',
        'src/core/lib/iomgr/socket_utils_linux.c',
        'src/core/lib/iomgr/socket_utils_posix.c',
        'src/core/lib/iomgr/socket_utils_uv.c',
        'src/core/lib/iomgr/socket_utils_windows.c',
        'src/core/lib/iomgr/socket_windows.c',
        'src/core/lib/iomgr/tcp_client_posix.c',
        'src/core/lib/iomgr/tcp_client_uv.c',
        'src/core/lib/iomgr/tcp_client_windows.c',
        'src/core/lib/iomgr/tcp_posix.c',
        'src/core/lib/iomgr/tcp_server_posix.c',
        'src/core/lib/iomgr/tcp_server_utils_posix_common.c',
        'src/core/lib/iomgr/tcp_server_utils_posix_ifaddrs.c',
        'src/core/lib/iomgr/tcp_server_utils_posix_noifaddrs.c',
        'src/core/lib/iomgr/tcp_server_uv.c',
        'src/core/lib/iomgr/tcp_server_windows.c',
        'src/core/lib/iomgr/tcp_uv.c',
        'src/core/lib/iomgr/tcp_windows.c',
        'src/core/lib/iomgr/time_averaged_stats.c',
        'src/core/lib/iomgr/timer_generic.c',
        'src/core/lib/iomgr/timer_heap.c',
        'src/core/lib/iomgr/timer_manager.c',
        'src/core/lib/iomgr/timer_uv.c',
        'src/core/lib/iomgr/udp_server.c',
        'src/core/lib/iomgr/unix_sockets_posix.c',
        'src/core/lib/iomgr/unix_sockets_posix_noop.c',
        'src/core/lib/iomgr/wakeup_fd_cv.c',
        'src/core/lib/iomgr/wakeup_fd_eventfd.c',
        'src/core/lib/iomgr/wakeup_fd_nospecial.c',
        'src/core/lib/iomgr/wakeup_fd_pipe.c',
        'src/core/lib/iomgr/wakeup_fd_posix.c',
        'src/core/lib/json/json.c',
        'src/core/lib/json/json_reader.c',
        'src/core/lib/json/json_string.c',
        'src/core/lib/json/json_writer.c',
        'src/core/lib/slice/b64.c',
        'src/core/lib/slice/percent_encoding.c',
        'src/core/lib/slice/slice.c',
        'src/core/lib/slice/slice_buffer.c',
        'src/core/lib/slice/slice_hash_table.c',
        'src/core/lib/slice/slice_intern.c',
        'src/core/lib/slice/slice_string_helpers.c',
        'src/core/lib/surface/alarm.c',
        'src/core/lib/surface/api_trace.c',
        'src/core/lib/surface/byte_buffer.c',
        'src/core/lib/surface/byte_buffer_reader.c',
        'src/core/lib/surface/call.c',
        'src/core/lib/surface/call_details.c',
        'src/core/lib/surface/call_log_batch.c',
        'src/core/lib/surface/channel.c',
        'src/core/lib/surface/channel_init.c',
        'src/core/lib/surface/channel_ping.c',
        'src/core/lib/surface/channel_stack_type.c',
        'src/core/lib/surface/completion_queue.c',
        'src/core/lib/surface/completion_queue_factory.c',
        'src/core/lib/surface/event_string.c',
        'src/core/lib/surface/lame_client.cc',
        'src/core/lib/surface/metadata_array.c',
        'src/core/lib/surface/server.c',
        'src/core/lib/surface/validate_metadata.c',
        'src/core/lib/surface/version.c',
        'src/core/lib/transport/bdp_estimator.c',
        'src/core/lib/transport/byte_stream.c',
        'src/core/lib/transport/connectivity_state.c',
        'src/core/lib/transport/error_utils.c',
        'src/core/lib/transport/metadata.c',
        'src/core/lib/transport/metadata_batch.c',
        'src/core/lib/transport/pid_controller.c',
        'src/core/lib/transport/service_config.c',
        'src/core/lib/transport/static_metadata.c',
        'src/core/lib/transport/status_conversion.c',
        'src/core/lib/transport/timeout_encoding.c',
        'src/core/lib/transport/transport.c',
        'src/core/lib/transport/transport_op_string.c',
        'src/core/lib/debug/trace.c',
        'src/core/ext/transport/chttp2/server/secure/server_secure_chttp2.c',
        'src/core/ext/transport/chttp2/transport/bin_decoder.c',
        'src/core/ext/transport/chttp2/transport/bin_encoder.c',
        'src/core/ext/transport/chttp2/transport/chttp2_plugin.c',
        'src/core/ext/transport/chttp2/transport/chttp2_transport.c',
        'src/core/ext/transport/chttp2/transport/flow_control.c',
        'src/core/ext/transport/chttp2/transport/frame_data.c',
        'src/core/ext/transport/chttp2/transport/frame_goaway.c',
        'src/core/ext/transport/chttp2/transport/frame_ping.c',
        'src/core/ext/transport/chttp2/transport/frame_rst_stream.c',
        'src/core/ext/transport/chttp2/transport/frame_settings.c',
        'src/core/ext/transport/chttp2/transport/frame_window_update.c',
        'src/core/ext/transport/chttp2/transport/hpack_encoder.c',
        'src/core/ext/transport/chttp2/transport/hpack_parser.c',
        'src/core/ext/transport/chttp2/transport/hpack_table.c',
        'src/core/ext/transport/chttp2/transport/http2_settings.c',
        'src/core/ext/transport/chttp2/transport/huffsyms.c',
        'src/core/ext/transport/chttp2/transport/incoming_metadata.c',
        'src/core/ext/transport/chttp2/transport/parsing.c',
        'src/core/ext/transport/chttp2/transport/stream_lists.c',
        'src/core/ext/transport/chttp2/transport/stream_map.c',
        'src/core/ext/transport/chttp2/transport/varint.c',
        'src/core/ext/transport/chttp2/transport/writing.c',
        'src/core/ext/transport/chttp2/alpn/alpn.c',
        'src/core/ext/filters/http/client/http_client_filter.c',
        'src/core/ext/filters/http/http_filters_plugin.c',
        'src/core/ext/filters/http/message_compress/message_compress_filter.c',
        'src/core/ext/filters/http/server/http_server_filter.c',
        'src/core/lib/http/httpcli_security_connector.c',
        'src/core/lib/security/context/security_context.c',
        'src/core/lib/security/credentials/composite/composite_credentials.c',
        'src/core/lib/security/credentials/credentials.c',
        'src/core/lib/security/credentials/credentials_metadata.c',
        'src/core/lib/security/credentials/fake/fake_credentials.c',
        'src/core/lib/security/credentials/google_default/credentials_generic.c',
        'src/core/lib/security/credentials/google_default/google_default_credentials.c',
        'src/core/lib/security/credentials/iam/iam_credentials.c',
        'src/core/lib/security/credentials/jwt/json_token.c',
        'src/core/lib/security/credentials/jwt/jwt_credentials.c',
        'src/core/lib/security/credentials/jwt/jwt_verifier.c',
        'src/core/lib/security/credentials/oauth2/oauth2_credentials.c',
        'src/core/lib/security/credentials/plugin/plugin_credentials.c',
        'src/core/lib/security/credentials/ssl/ssl_credentials.c',
        'src/core/lib/security/transport/client_auth_filter.c',
        'src/core/lib/security/transport/lb_targets_info.c',
        'src/core/lib/security/transport/secure_endpoint.c',
        'src/core/lib/security/transport/security_connector.c',
        'src/core/lib/security/transport/security_handshaker.c',
        'src/core/lib/security/transport/server_auth_filter.c',
        'src/core/lib/security/transport/tsi_error.c',
        'src/core/lib/security/util/json_util.c',
        'src/core/lib/surface/init_secure.c',
        'src/core/tsi/fake_transport_security.c',
        'src/core/tsi/gts_transport_security.c',
        'src/core/tsi/ssl_transport_security.c',
        'src/core/tsi/transport_security_grpc.c',
        'src/core/tsi/transport_security.c',
        'src/core/tsi/transport_security_adapter.c',
        'src/core/ext/transport/chttp2/server/chttp2_server.c',
        'src/core/ext/transport/chttp2/client/secure/secure_channel_create.c',
        'src/core/ext/filters/client_channel/channel_connectivity.c',
        'src/core/ext/filters/client_channel/client_channel.c',
        'src/core/ext/filters/client_channel/client_channel_factory.c',
        'src/core/ext/filters/client_channel/client_channel_plugin.c',
        'src/core/ext/filters/client_channel/connector.c',
        'src/core/ext/filters/client_channel/http_connect_handshaker.c',
        'src/core/ext/filters/client_channel/http_proxy.c',
        'src/core/ext/filters/client_channel/lb_policy.c',
        'src/core/ext/filters/client_channel/lb_policy_factory.c',
        'src/core/ext/filters/client_channel/lb_policy_registry.c',
        'src/core/ext/filters/client_channel/parse_address.c',
        'src/core/ext/filters/client_channel/proxy_mapper.c',
        'src/core/ext/filters/client_channel/proxy_mapper_registry.c',
        'src/core/ext/filters/client_channel/resolver.c',
        'src/core/ext/filters/client_channel/resolver_factory.c',
        'src/core/ext/filters/client_channel/resolver_registry.c',
        'src/core/ext/filters/client_channel/retry_throttle.c',
        'src/core/ext/filters/client_channel/subchannel.c',
        'src/core/ext/filters/client_channel/subchannel_index.c',
        'src/core/ext/filters/client_channel/uri_parser.c',
        'src/core/ext/filters/deadline/deadline_filter.c',
        'src/core/ext/transport/chttp2/client/chttp2_connector.c',
        'src/core/ext/transport/chttp2/server/insecure/server_chttp2.c',
        'src/core/ext/transport/chttp2/server/insecure/server_chttp2_posix.c',
        'src/core/ext/transport/chttp2/client/insecure/channel_create.c',
        'src/core/ext/transport/chttp2/client/insecure/channel_create_posix.c',
        'src/core/ext/transport/inproc/inproc_plugin.c',
        'src/core/ext/transport/inproc/inproc_transport.c',
        'src/core/ext/filters/client_channel/lb_policy/grpclb/client_load_reporting_filter.c',
        'src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb.c',
        'src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_channel_secure.c',
        'src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.c',
        'src/core/ext/filters/client_channel/lb_policy/grpclb/load_balancer_api.c',
        'src/core/ext/filters/client_channel/lb_policy/grpclb/proto/grpc/lb/v1/load_balancer.pb.c',
        'third_party/nanopb/pb_common.c',
        'third_party/nanopb/pb_decode.c',
        'third_party/nanopb/pb_encode.c',
        'src/core/ext/filters/client_channel/resolver/fake/fake_resolver.c',
        'src/core/ext/filters/client_channel/lb_policy/pick_first/pick_first.c',
        'src/core/ext/filters/client_channel/lb_policy/round_robin/round_robin.c',
        'src/core/ext/filters/client_channel/resolver/dns/c_ares/dns_resolver_ares.c',
        'src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver_posix.c',
        'src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.c',
        'src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper_fallback.c',
        'src/core/ext/filters/client_channel/resolver/dns/native/dns_resolver.c',
        'src/core/ext/filters/client_channel/resolver/sockaddr/sockaddr_resolver.c',
        'src/core/ext/filters/load_reporting/server_load_reporting_filter.c',
        'src/core/ext/filters/load_reporting/server_load_reporting_plugin.c',
        'src/core/ext/census/base_resources.c',
        'src/core/ext/census/context.c',
        'src/core/ext/census/gen/census.pb.c',
        'src/core/ext/census/gen/trace_context.pb.c',
        'src/core/ext/census/grpc_context.c',
        'src/core/ext/census/grpc_filter.c',
        'src/core/ext/census/grpc_plugin.c',
        'src/core/ext/census/initialize.c',
        'src/core/ext/census/intrusive_hash_map.c',
        'src/core/ext/census/mlog.c',
        'src/core/ext/census/operation.c',
        'src/core/ext/census/placeholders.c',
        'src/core/ext/census/resource.c',
        'src/core/ext/census/trace_context.c',
        'src/core/ext/census/tracing.c',
        'src/core/ext/filters/max_age/max_age_filter.c',
        'src/core/ext/filters/message_size/message_size_filter.c',
        'src/core/ext/filters/workarounds/workaround_cronet_compression_filter.c',
        'src/core/ext/filters/workarounds/workaround_utils.c',
        'src/core/plugin_registry/grpc_plugin_registry.c',
      ],
      'conditions': [
        ['OS == "mac"', {
          'xcode_settings': {
            'MACOSX_DEPLOYMENT_TARGET': '10.9'
          }
        }]
      ]
    },
    {
      'include_dirs': [
        "<!(node -e \"require('nan')\")"
      ],
      'cflags': [
        '-pthread',
        '-zdefs',
        '-Wno-error=deprecated-declarations'
      ],
      "conditions": [
        ['OS=="win" or runtime=="electron"', {
          'dependencies': [
            "boringssl",
          ]
        }],
        ['OS=="win"', {
          'dependencies': [
            "z",
          ]
        }],
        ['OS=="linux"', {
          'ldflags': [
            '-Wl,-wrap,memcpy'
          ]
        }],
        ['OS == "mac"', {
          'xcode_settings': {
            'MACOSX_DEPLOYMENT_TARGET': '10.9'
          }
        }]
      ],
      "target_name": "grpc_node",
      "sources": [
        "src/node/ext/byte_buffer.cc",
        "src/node/ext/call.cc",
        "src/node/ext/call_credentials.cc",
        "src/node/ext/channel.cc",
        "src/node/ext/channel_credentials.cc",
        "src/node/ext/completion_queue.cc",
        "src/node/ext/node_grpc.cc",
        "src/node/ext/server.cc",
        "src/node/ext/server_credentials.cc",
        "src/node/ext/slice.cc",
        "src/node/ext/timeval.cc",
      ],
      "dependencies": [
        "grpc",
        "gpr",
      ]
    },
    {
      "target_name": "action_after_build",
      "type": "none",
      "dependencies": [ "<(module_name)" ],
      "copies": [
        {
          "files": [ "<(PRODUCT_DIR)/<(module_name).node"],
          "destination": "<(module_path)"
        }
      ]
    }
  ]
}
