# GRPC Node gyp file
# This currently builds the Node extension and dependencies
# This file has been automatically generated from a template file.
# Please look at the templates directory instead.
# This file can be regenerated from the template by running
# tools/buildgen/generate_projects.sh

# Copyright 2015-2016, Google Inc.
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

# Some of this file is built with the help of
# https://n8.io/converting-a-c-library-to-gyp/
{
  'target_defaults': {
    'include_dirs': [
      '.',
      'include'
    ],
    'conditions': [
      ['OS == "win"', {
        "include_dirs": [
          "third_party/boringssl/include",
          "third_party/zlib"
        ],
        "defines": [
          '_WIN32_WINNT=0x0600',
          'WIN32_LEAN_AND_MEAN',
          '_HAS_EXCEPTIONS=0',
          'UNICODE',
          '_UNICODE',
          'NOMINMAX',
          'OPENSSL_NO_ASM',
          'GPR_BACKWARDS_COMPATIBILITY_MODE'
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
        'variables': {
          'config': '<!(echo $CONFIG)',
          # The output of "node --version" is "v[version]". We use cut to
          # remove the first character.
          'target%': '<!(node --version | cut -c2-)'
        },
          # Empirically, Node only exports ALPN symbols if its major version is >0.
          # io.js always reports versions >0 and always exports ALPN symbols.
          # Therefore, Node's major version will be truthy if and only if it
          # supports ALPN. The target is "[major].[minor].[patch]". We split by
          # periods and take the first field to get the major version.
        'defines': [
          'TSI_OPENSSL_ALPN_SUPPORT=<!(echo <(target) | cut -d. -f1)',
          'GPR_BACKWARDS_COMPATIBILITY_MODE'
        ],
        'include_dirs': [
          '<(node_root_dir)/deps/openssl/openssl/include',
          '<(node_root_dir)/deps/zlib'
        ],
        'conditions': [
          ['config=="gcov"', {
            'cflags': [
              '-ftest-coverage',
              '-fprofile-arcs',
              '-O0'
            ],
            'ldflags': [
              '-ftest-coverage',
              '-fprofile-arcs'
            ]
          }
         ],
         ["target_arch=='ia32'", {
             "include_dirs": [ "<(node_root_dir)/deps/openssl/config/piii" ]
         }],
         ["target_arch=='x64'", {
             "include_dirs": [ "<(node_root_dir)/deps/openssl/config/k8" ]
         }],
         ["target_arch=='arm'", {
             "include_dirs": [ "<(node_root_dir)/deps/openssl/config/arm" ]
         }]
        ]
      }]
    ]
  },
  'conditions': [
    ['OS == "win"', {
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
          'target_name': 'WINDOWS_BUILD_WARNING',
          'actions': [
            {
              'action_name': 'WINDOWS_BUILD_WARNING',
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
        # Only want to compile BoringSSL and zlib under Windows
        {
          'cflags': [
            '-std=c99',
            '-Wall',
            '-Werror'
          ],
          'target_name': 'boringssl',
          'product_prefix': 'lib',
          'type': 'static_library',
          'dependencies': [
          ],
          'sources': [
            'src/boringssl/err_data.c',
            'third_party/boringssl/crypto/aes/aes.c',
            'third_party/boringssl/crypto/aes/mode_wrappers.c',
            'third_party/boringssl/crypto/asn1/a_bitstr.c',
            'third_party/boringssl/crypto/asn1/a_bool.c',
            'third_party/boringssl/crypto/asn1/a_bytes.c',
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
            'third_party/boringssl/crypto/asn1/bio_asn1.c',
            'third_party/boringssl/crypto/asn1/bio_ndef.c',
            'third_party/boringssl/crypto/asn1/f_enum.c',
            'third_party/boringssl/crypto/asn1/f_int.c',
            'third_party/boringssl/crypto/asn1/f_string.c',
            'third_party/boringssl/crypto/asn1/t_bitst.c',
            'third_party/boringssl/crypto/asn1/t_pkey.c',
            'third_party/boringssl/crypto/asn1/tasn_dec.c',
            'third_party/boringssl/crypto/asn1/tasn_enc.c',
            'third_party/boringssl/crypto/asn1/tasn_fre.c',
            'third_party/boringssl/crypto/asn1/tasn_new.c',
            'third_party/boringssl/crypto/asn1/tasn_prn.c',
            'third_party/boringssl/crypto/asn1/tasn_typ.c',
            'third_party/boringssl/crypto/asn1/tasn_utl.c',
            'third_party/boringssl/crypto/asn1/x_bignum.c',
            'third_party/boringssl/crypto/asn1/x_long.c',
            'third_party/boringssl/crypto/base64/base64.c',
            'third_party/boringssl/crypto/bio/bio.c',
            'third_party/boringssl/crypto/bio/bio_mem.c',
            'third_party/boringssl/crypto/bio/buffer.c',
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
            'third_party/boringssl/crypto/bn/mul.c',
            'third_party/boringssl/crypto/bn/prime.c',
            'third_party/boringssl/crypto/bn/random.c',
            'third_party/boringssl/crypto/bn/rsaz_exp.c',
            'third_party/boringssl/crypto/bn/shift.c',
            'third_party/boringssl/crypto/bn/sqrt.c',
            'third_party/boringssl/crypto/buf/buf.c',
            'third_party/boringssl/crypto/bytestring/ber.c',
            'third_party/boringssl/crypto/bytestring/cbb.c',
            'third_party/boringssl/crypto/bytestring/cbs.c',
            'third_party/boringssl/crypto/chacha/chacha_generic.c',
            'third_party/boringssl/crypto/chacha/chacha_vec.c',
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
            'third_party/boringssl/crypto/cpu-arm.c',
            'third_party/boringssl/crypto/cpu-intel.c',
            'third_party/boringssl/crypto/crypto.c',
            'third_party/boringssl/crypto/curve25519/curve25519.c',
            'third_party/boringssl/crypto/des/des.c',
            'third_party/boringssl/crypto/dh/check.c',
            'third_party/boringssl/crypto/dh/dh.c',
            'third_party/boringssl/crypto/dh/dh_asn1.c',
            'third_party/boringssl/crypto/dh/params.c',
            'third_party/boringssl/crypto/digest/digest.c',
            'third_party/boringssl/crypto/digest/digests.c',
            'third_party/boringssl/crypto/directory_posix.c',
            'third_party/boringssl/crypto/directory_win.c',
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
            'third_party/boringssl/crypto/evp/algorithm.c',
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
            'third_party/boringssl/crypto/pkcs8/p5_pbe.c',
            'third_party/boringssl/crypto/pkcs8/p5_pbev2.c',
            'third_party/boringssl/crypto/pkcs8/p8_pkey.c',
            'third_party/boringssl/crypto/pkcs8/pkcs8.c',
            'third_party/boringssl/crypto/poly1305/poly1305.c',
            'third_party/boringssl/crypto/poly1305/poly1305_arm.c',
            'third_party/boringssl/crypto/poly1305/poly1305_vec.c',
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
            'third_party/boringssl/crypto/sha/sha1.c',
            'third_party/boringssl/crypto/sha/sha256.c',
            'third_party/boringssl/crypto/sha/sha512.c',
            'third_party/boringssl/crypto/stack/stack.c',
            'third_party/boringssl/crypto/thread.c',
            'third_party/boringssl/crypto/thread_none.c',
            'third_party/boringssl/crypto/thread_pthread.c',
            'third_party/boringssl/crypto/thread_win.c',
            'third_party/boringssl/crypto/time_support.c',
            'third_party/boringssl/crypto/x509/a_digest.c',
            'third_party/boringssl/crypto/x509/a_sign.c',
            'third_party/boringssl/crypto/x509/a_strex.c',
            'third_party/boringssl/crypto/x509/a_verify.c',
            'third_party/boringssl/crypto/x509/asn1_gen.c',
            'third_party/boringssl/crypto/x509/by_dir.c',
            'third_party/boringssl/crypto/x509/by_file.c',
            'third_party/boringssl/crypto/x509/i2d_pr.c',
            'third_party/boringssl/crypto/x509/pkcs7.c',
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
            'third_party/boringssl/ssl/custom_extensions.c',
            'third_party/boringssl/ssl/d1_both.c',
            'third_party/boringssl/ssl/d1_clnt.c',
            'third_party/boringssl/ssl/d1_lib.c',
            'third_party/boringssl/ssl/d1_meth.c',
            'third_party/boringssl/ssl/d1_pkt.c',
            'third_party/boringssl/ssl/d1_srtp.c',
            'third_party/boringssl/ssl/d1_srvr.c',
            'third_party/boringssl/ssl/dtls_record.c',
            'third_party/boringssl/ssl/pqueue/pqueue.c',
            'third_party/boringssl/ssl/s3_both.c',
            'third_party/boringssl/ssl/s3_clnt.c',
            'third_party/boringssl/ssl/s3_enc.c',
            'third_party/boringssl/ssl/s3_lib.c',
            'third_party/boringssl/ssl/s3_meth.c',
            'third_party/boringssl/ssl/s3_pkt.c',
            'third_party/boringssl/ssl/s3_srvr.c',
            'third_party/boringssl/ssl/ssl_aead_ctx.c',
            'third_party/boringssl/ssl/ssl_asn1.c',
            'third_party/boringssl/ssl/ssl_buffer.c',
            'third_party/boringssl/ssl/ssl_cert.c',
            'third_party/boringssl/ssl/ssl_cipher.c',
            'third_party/boringssl/ssl/ssl_file.c',
            'third_party/boringssl/ssl/ssl_lib.c',
            'third_party/boringssl/ssl/ssl_rsa.c',
            'third_party/boringssl/ssl/ssl_session.c',
            'third_party/boringssl/ssl/ssl_stat.c',
            'third_party/boringssl/ssl/t1_enc.c',
            'third_party/boringssl/ssl/t1_lib.c',
            'third_party/boringssl/ssl/tls_record.c',
          ]
        },
        {
          'cflags': [
            '-std=c99',
            '-Wall',
            '-Werror'
          ],
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
      'cflags': [
        '-std=c99',
        '-Wall',
        '-Werror'
      ],
      'target_name': 'gpr',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
      ],
      'sources': [
        'src/core/profiling/basic_timers.c',
        'src/core/profiling/stap_timers.c',
        'src/core/support/alloc.c',
        'src/core/support/avl.c',
        'src/core/support/cmdline.c',
        'src/core/support/cpu_iphone.c',
        'src/core/support/cpu_linux.c',
        'src/core/support/cpu_posix.c',
        'src/core/support/cpu_windows.c',
        'src/core/support/env_linux.c',
        'src/core/support/env_posix.c',
        'src/core/support/env_win32.c',
        'src/core/support/histogram.c',
        'src/core/support/host_port.c',
        'src/core/support/load_file.c',
        'src/core/support/log.c',
        'src/core/support/log_android.c',
        'src/core/support/log_linux.c',
        'src/core/support/log_posix.c',
        'src/core/support/log_win32.c',
        'src/core/support/murmur_hash.c',
        'src/core/support/slice.c',
        'src/core/support/slice_buffer.c',
        'src/core/support/stack_lockfree.c',
        'src/core/support/string.c',
        'src/core/support/string_posix.c',
        'src/core/support/string_win32.c',
        'src/core/support/subprocess_posix.c',
        'src/core/support/subprocess_windows.c',
        'src/core/support/sync.c',
        'src/core/support/sync_posix.c',
        'src/core/support/sync_win32.c',
        'src/core/support/thd.c',
        'src/core/support/thd_posix.c',
        'src/core/support/thd_win32.c',
        'src/core/support/time.c',
        'src/core/support/time_posix.c',
        'src/core/support/time_precise.c',
        'src/core/support/time_win32.c',
        'src/core/support/tls_pthread.c',
        'src/core/support/tmpfile_posix.c',
        'src/core/support/tmpfile_win32.c',
        'src/core/support/wrap_memcpy.c',
      ],
      "conditions": [
        ['OS == "mac"', {
          'xcode_settings': {
            'MACOSX_DEPLOYMENT_TARGET': '10.9'
          }
        }]
      ]
    },
    {
      'cflags': [
        '-std=c99',
        '-Wall',
        '-Werror'
      ],
      'target_name': 'grpc',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'gpr',
      ],
      'sources': [
        'src/core/census/grpc_context.c',
        'src/core/census/grpc_filter.c',
        'src/core/channel/channel_args.c',
        'src/core/channel/channel_stack.c',
        'src/core/channel/client_channel.c',
        'src/core/channel/client_uchannel.c',
        'src/core/channel/compress_filter.c',
        'src/core/channel/connected_channel.c',
        'src/core/channel/http_client_filter.c',
        'src/core/channel/http_server_filter.c',
        'src/core/channel/subchannel_call_holder.c',
        'src/core/client_config/client_config.c',
        'src/core/client_config/connector.c',
        'src/core/client_config/default_initial_connect_string.c',
        'src/core/client_config/initial_connect_string.c',
        'src/core/client_config/lb_policies/load_balancer_api.c',
        'src/core/client_config/lb_policies/pick_first.c',
        'src/core/client_config/lb_policies/round_robin.c',
        'src/core/client_config/lb_policy.c',
        'src/core/client_config/lb_policy_factory.c',
        'src/core/client_config/lb_policy_registry.c',
        'src/core/client_config/resolver.c',
        'src/core/client_config/resolver_factory.c',
        'src/core/client_config/resolver_registry.c',
        'src/core/client_config/resolvers/dns_resolver.c',
        'src/core/client_config/resolvers/sockaddr_resolver.c',
        'src/core/client_config/subchannel.c',
        'src/core/client_config/subchannel_factory.c',
        'src/core/client_config/subchannel_index.c',
        'src/core/client_config/uri_parser.c',
        'src/core/compression/compression_algorithm.c',
        'src/core/compression/message_compress.c',
        'src/core/debug/trace.c',
        'src/core/httpcli/format_request.c',
        'src/core/httpcli/httpcli.c',
        'src/core/httpcli/parser.c',
        'src/core/iomgr/closure.c',
        'src/core/iomgr/endpoint.c',
        'src/core/iomgr/endpoint_pair_posix.c',
        'src/core/iomgr/endpoint_pair_windows.c',
        'src/core/iomgr/exec_ctx.c',
        'src/core/iomgr/executor.c',
        'src/core/iomgr/fd_posix.c',
        'src/core/iomgr/iocp_windows.c',
        'src/core/iomgr/iomgr.c',
        'src/core/iomgr/iomgr_posix.c',
        'src/core/iomgr/iomgr_windows.c',
        'src/core/iomgr/pollset_multipoller_with_epoll.c',
        'src/core/iomgr/pollset_multipoller_with_poll_posix.c',
        'src/core/iomgr/pollset_posix.c',
        'src/core/iomgr/pollset_set_posix.c',
        'src/core/iomgr/pollset_set_windows.c',
        'src/core/iomgr/pollset_windows.c',
        'src/core/iomgr/resolve_address_posix.c',
        'src/core/iomgr/resolve_address_windows.c',
        'src/core/iomgr/sockaddr_utils.c',
        'src/core/iomgr/socket_utils_common_posix.c',
        'src/core/iomgr/socket_utils_linux.c',
        'src/core/iomgr/socket_utils_posix.c',
        'src/core/iomgr/socket_windows.c',
        'src/core/iomgr/tcp_client_posix.c',
        'src/core/iomgr/tcp_client_windows.c',
        'src/core/iomgr/tcp_posix.c',
        'src/core/iomgr/tcp_server_posix.c',
        'src/core/iomgr/tcp_server_windows.c',
        'src/core/iomgr/tcp_windows.c',
        'src/core/iomgr/time_averaged_stats.c',
        'src/core/iomgr/timer.c',
        'src/core/iomgr/timer_heap.c',
        'src/core/iomgr/udp_server.c',
        'src/core/iomgr/wakeup_fd_eventfd.c',
        'src/core/iomgr/wakeup_fd_nospecial.c',
        'src/core/iomgr/wakeup_fd_pipe.c',
        'src/core/iomgr/wakeup_fd_posix.c',
        'src/core/iomgr/workqueue_posix.c',
        'src/core/iomgr/workqueue_windows.c',
        'src/core/json/json.c',
        'src/core/json/json_reader.c',
        'src/core/json/json_string.c',
        'src/core/json/json_writer.c',
        'src/core/proto/grpc/lb/v0/load_balancer.pb.c',
        'src/core/surface/alarm.c',
        'src/core/surface/api_trace.c',
        'src/core/surface/byte_buffer.c',
        'src/core/surface/byte_buffer_reader.c',
        'src/core/surface/call.c',
        'src/core/surface/call_details.c',
        'src/core/surface/call_log_batch.c',
        'src/core/surface/channel.c',
        'src/core/surface/channel_connectivity.c',
        'src/core/surface/channel_create.c',
        'src/core/surface/channel_ping.c',
        'src/core/surface/completion_queue.c',
        'src/core/surface/event_string.c',
        'src/core/surface/init.c',
        'src/core/surface/lame_client.c',
        'src/core/surface/metadata_array.c',
        'src/core/surface/server.c',
        'src/core/surface/server_chttp2.c',
        'src/core/surface/server_create.c',
        'src/core/surface/validate_metadata.c',
        'src/core/surface/version.c',
        'src/core/transport/byte_stream.c',
        'src/core/transport/chttp2/alpn.c',
        'src/core/transport/chttp2/bin_encoder.c',
        'src/core/transport/chttp2/frame_data.c',
        'src/core/transport/chttp2/frame_goaway.c',
        'src/core/transport/chttp2/frame_ping.c',
        'src/core/transport/chttp2/frame_rst_stream.c',
        'src/core/transport/chttp2/frame_settings.c',
        'src/core/transport/chttp2/frame_window_update.c',
        'src/core/transport/chttp2/hpack_encoder.c',
        'src/core/transport/chttp2/hpack_parser.c',
        'src/core/transport/chttp2/hpack_table.c',
        'src/core/transport/chttp2/huffsyms.c',
        'src/core/transport/chttp2/incoming_metadata.c',
        'src/core/transport/chttp2/parsing.c',
        'src/core/transport/chttp2/status_conversion.c',
        'src/core/transport/chttp2/stream_lists.c',
        'src/core/transport/chttp2/stream_map.c',
        'src/core/transport/chttp2/timeout_encoding.c',
        'src/core/transport/chttp2/varint.c',
        'src/core/transport/chttp2/writing.c',
        'src/core/transport/chttp2_transport.c',
        'src/core/transport/connectivity_state.c',
        'src/core/transport/metadata.c',
        'src/core/transport/metadata_batch.c',
        'src/core/transport/static_metadata.c',
        'src/core/transport/transport.c',
        'src/core/transport/transport_op_string.c',
        'src/core/httpcli/httpcli_security_connector.c',
        'src/core/security/b64.c',
        'src/core/security/client_auth_filter.c',
        'src/core/security/credentials.c',
        'src/core/security/credentials_metadata.c',
        'src/core/security/credentials_posix.c',
        'src/core/security/credentials_win32.c',
        'src/core/security/google_default_credentials.c',
        'src/core/security/handshake.c',
        'src/core/security/json_token.c',
        'src/core/security/jwt_verifier.c',
        'src/core/security/secure_endpoint.c',
        'src/core/security/security_connector.c',
        'src/core/security/security_context.c',
        'src/core/security/server_auth_filter.c',
        'src/core/security/server_secure_chttp2.c',
        'src/core/surface/init_secure.c',
        'src/core/surface/secure_channel_create.c',
        'src/core/tsi/fake_transport_security.c',
        'src/core/tsi/ssl_transport_security.c',
        'src/core/tsi/transport_security.c',
        'src/core/census/context.c',
        'src/core/census/initialize.c',
        'src/core/census/mlog.c',
        'src/core/census/operation.c',
        'src/core/census/placeholders.c',
        'src/core/census/tracing.c',
        'third_party/nanopb/pb_common.c',
        'third_party/nanopb/pb_decode.c',
        'third_party/nanopb/pb_encode.c',
      ],
      "conditions": [
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
        '-std=c++11',
        '-Wall',
        '-pthread',
        '-g',
        '-zdefs',
        '-Werror',
        '-Wno-error=deprecated-declarations'
      ],
      'ldflags': [
        '-g'
      ],
      "conditions": [
        ['OS=="mac"', {
          'xcode_settings': {
            'MACOSX_DEPLOYMENT_TARGET': '10.9',
            'OTHER_CFLAGS': [
              '-stdlib=libc++',
              '-std=c++11'
            ]
          }
        }],
        ['OS=="win"', {
          'dependencies': [
            "boringssl",
            "z",
          ]
        }],
        ['OS=="linux"', {
          'ldflags': [
            '-Wl,-wrap,memcpy'
          ]
        }]
      ],
      "target_name": "grpc_node",
      "sources": [
        "src/node/ext/byte_buffer.cc",
        "src/node/ext/call.cc",
        "src/node/ext/call_credentials.cc",
        "src/node/ext/channel.cc",
        "src/node/ext/channel_credentials.cc",
        "src/node/ext/completion_queue_async_worker.cc",
        "src/node/ext/node_grpc.cc",
        "src/node/ext/server.cc",
        "src/node/ext/server_credentials.cc",
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
