cc_library(
  name = "boringssl",
  linkstatic = 1,
  srcs = [
    'err_data.c',
    'src/crypto/aes/aes.c',
    'src/crypto/aes/mode_wrappers.c',
    'src/crypto/asn1/a_bitstr.c',
    'src/crypto/asn1/a_bool.c',
    'src/crypto/asn1/a_bytes.c',
    'src/crypto/asn1/a_d2i_fp.c',
    'src/crypto/asn1/a_dup.c',
    'src/crypto/asn1/a_enum.c',
    'src/crypto/asn1/a_gentm.c',
    'src/crypto/asn1/a_i2d_fp.c',
    'src/crypto/asn1/a_int.c',
    'src/crypto/asn1/a_mbstr.c',
    'src/crypto/asn1/a_object.c',
    'src/crypto/asn1/a_octet.c',
    'src/crypto/asn1/a_print.c',
    'src/crypto/asn1/a_strnid.c',
    'src/crypto/asn1/a_time.c',
    'src/crypto/asn1/a_type.c',
    'src/crypto/asn1/a_utctm.c',
    'src/crypto/asn1/a_utf8.c',
    'src/crypto/asn1/asn1_lib.c',
    'src/crypto/asn1/asn1_par.c',
    'src/crypto/asn1/asn_pack.c',
    'src/crypto/asn1/bio_asn1.c',
    'src/crypto/asn1/bio_ndef.c',
    'src/crypto/asn1/f_enum.c',
    'src/crypto/asn1/f_int.c',
    'src/crypto/asn1/f_string.c',
    'src/crypto/asn1/t_bitst.c',
    'src/crypto/asn1/t_pkey.c',
    'src/crypto/asn1/tasn_dec.c',
    'src/crypto/asn1/tasn_enc.c',
    'src/crypto/asn1/tasn_fre.c',
    'src/crypto/asn1/tasn_new.c',
    'src/crypto/asn1/tasn_prn.c',
    'src/crypto/asn1/tasn_typ.c',
    'src/crypto/asn1/tasn_utl.c',
    'src/crypto/asn1/x_bignum.c',
    'src/crypto/asn1/x_long.c',
    'src/crypto/base64/base64.c',
    'src/crypto/bio/bio.c',
    'src/crypto/bio/bio_mem.c',
    'src/crypto/bio/buffer.c',
    'src/crypto/bio/connect.c',
    'src/crypto/bio/fd.c',
    'src/crypto/bio/file.c',
    'src/crypto/bio/hexdump.c',
    'src/crypto/bio/pair.c',
    'src/crypto/bio/printf.c',
    'src/crypto/bio/socket.c',
    'src/crypto/bio/socket_helper.c',
    'src/crypto/bn/add.c',
    'src/crypto/bn/asm/x86_64-gcc.c',
    'src/crypto/bn/bn.c',
    'src/crypto/bn/bn_asn1.c',
    'src/crypto/bn/cmp.c',
    'src/crypto/bn/convert.c',
    'src/crypto/bn/ctx.c',
    'src/crypto/bn/div.c',
    'src/crypto/bn/exponentiation.c',
    'src/crypto/bn/gcd.c',
    'src/crypto/bn/generic.c',
    'src/crypto/bn/kronecker.c',
    'src/crypto/bn/montgomery.c',
    'src/crypto/bn/mul.c',
    'src/crypto/bn/prime.c',
    'src/crypto/bn/random.c',
    'src/crypto/bn/rsaz_exp.c',
    'src/crypto/bn/shift.c',
    'src/crypto/bn/sqrt.c',
    'src/crypto/buf/buf.c',
    'src/crypto/bytestring/asn1_compat.c',
    'src/crypto/bytestring/ber.c',
    'src/crypto/bytestring/cbb.c',
    'src/crypto/bytestring/cbs.c',
    'src/crypto/chacha/chacha_generic.c',
    'src/crypto/chacha/chacha_vec.c',
    'src/crypto/cipher/aead.c',
    'src/crypto/cipher/cipher.c',
    'src/crypto/cipher/derive_key.c',
    'src/crypto/cipher/e_aes.c',
    'src/crypto/cipher/e_chacha20poly1305.c',
    'src/crypto/cipher/e_des.c',
    'src/crypto/cipher/e_null.c',
    'src/crypto/cipher/e_rc2.c',
    'src/crypto/cipher/e_rc4.c',
    'src/crypto/cipher/e_ssl3.c',
    'src/crypto/cipher/e_tls.c',
    'src/crypto/cipher/tls_cbc.c',
    'src/crypto/cmac/cmac.c',
    'src/crypto/conf/conf.c',
    'src/crypto/cpu-arm.c',
    'src/crypto/cpu-intel.c',
    'src/crypto/crypto.c',
    'src/crypto/curve25519/curve25519.c',
    'src/crypto/curve25519/x25519-x86_64.c',
    'src/crypto/des/des.c',
    'src/crypto/dh/check.c',
    'src/crypto/dh/dh.c',
    'src/crypto/dh/dh_asn1.c',
    'src/crypto/dh/params.c',
    'src/crypto/digest/digest.c',
    'src/crypto/digest/digests.c',
    'src/crypto/directory_posix.c',
    'src/crypto/directory_win.c',
    'src/crypto/dsa/dsa.c',
    'src/crypto/dsa/dsa_asn1.c',
    'src/crypto/ec/ec.c',
    'src/crypto/ec/ec_asn1.c',
    'src/crypto/ec/ec_key.c',
    'src/crypto/ec/ec_montgomery.c',
    'src/crypto/ec/oct.c',
    'src/crypto/ec/p224-64.c',
    'src/crypto/ec/p256-64.c',
    'src/crypto/ec/p256-x86_64.c',
    'src/crypto/ec/simple.c',
    'src/crypto/ec/util-64.c',
    'src/crypto/ec/wnaf.c',
    'src/crypto/ecdh/ecdh.c',
    'src/crypto/ecdsa/ecdsa.c',
    'src/crypto/ecdsa/ecdsa_asn1.c',
    'src/crypto/engine/engine.c',
    'src/crypto/err/err.c',
    'src/crypto/evp/algorithm.c',
    'src/crypto/evp/digestsign.c',
    'src/crypto/evp/evp.c',
    'src/crypto/evp/evp_asn1.c',
    'src/crypto/evp/evp_ctx.c',
    'src/crypto/evp/p_dsa_asn1.c',
    'src/crypto/evp/p_ec.c',
    'src/crypto/evp/p_ec_asn1.c',
    'src/crypto/evp/p_rsa.c',
    'src/crypto/evp/p_rsa_asn1.c',
    'src/crypto/evp/pbkdf.c',
    'src/crypto/evp/sign.c',
    'src/crypto/ex_data.c',
    'src/crypto/hkdf/hkdf.c',
    'src/crypto/hmac/hmac.c',
    'src/crypto/lhash/lhash.c',
    'src/crypto/md4/md4.c',
    'src/crypto/md5/md5.c',
    'src/crypto/mem.c',
    'src/crypto/modes/cbc.c',
    'src/crypto/modes/cfb.c',
    'src/crypto/modes/ctr.c',
    'src/crypto/modes/gcm.c',
    'src/crypto/modes/ofb.c',
    'src/crypto/obj/obj.c',
    'src/crypto/obj/obj_xref.c',
    'src/crypto/pem/pem_all.c',
    'src/crypto/pem/pem_info.c',
    'src/crypto/pem/pem_lib.c',
    'src/crypto/pem/pem_oth.c',
    'src/crypto/pem/pem_pk8.c',
    'src/crypto/pem/pem_pkey.c',
    'src/crypto/pem/pem_x509.c',
    'src/crypto/pem/pem_xaux.c',
    'src/crypto/pkcs8/p5_pbe.c',
    'src/crypto/pkcs8/p5_pbev2.c',
    'src/crypto/pkcs8/p8_pkey.c',
    'src/crypto/pkcs8/pkcs8.c',
    'src/crypto/poly1305/poly1305.c',
    'src/crypto/poly1305/poly1305_arm.c',
    'src/crypto/poly1305/poly1305_vec.c',
    'src/crypto/rand/rand.c',
    'src/crypto/rand/urandom.c',
    'src/crypto/rand/windows.c',
    'src/crypto/rc4/rc4.c',
    'src/crypto/refcount_c11.c',
    'src/crypto/refcount_lock.c',
    'src/crypto/rsa/blinding.c',
    'src/crypto/rsa/padding.c',
    'src/crypto/rsa/rsa.c',
    'src/crypto/rsa/rsa_asn1.c',
    'src/crypto/rsa/rsa_impl.c',
    'src/crypto/sha/sha1.c',
    'src/crypto/sha/sha256.c',
    'src/crypto/sha/sha512.c',
    'src/crypto/stack/stack.c',
    'src/crypto/thread.c',
    'src/crypto/thread_none.c',
    'src/crypto/thread_pthread.c',
    'src/crypto/thread_win.c',
    'src/crypto/time_support.c',
    'src/crypto/x509/a_digest.c',
    'src/crypto/x509/a_sign.c',
    'src/crypto/x509/a_strex.c',
    'src/crypto/x509/a_verify.c',
    'src/crypto/x509/asn1_gen.c',
    'src/crypto/x509/by_dir.c',
    'src/crypto/x509/by_file.c',
    'src/crypto/x509/i2d_pr.c',
    'src/crypto/x509/pkcs7.c',
    'src/crypto/x509/t_crl.c',
    'src/crypto/x509/t_req.c',
    'src/crypto/x509/t_x509.c',
    'src/crypto/x509/t_x509a.c',
    'src/crypto/x509/x509.c',
    'src/crypto/x509/x509_att.c',
    'src/crypto/x509/x509_cmp.c',
    'src/crypto/x509/x509_d2.c',
    'src/crypto/x509/x509_def.c',
    'src/crypto/x509/x509_ext.c',
    'src/crypto/x509/x509_lu.c',
    'src/crypto/x509/x509_obj.c',
    'src/crypto/x509/x509_r2x.c',
    'src/crypto/x509/x509_req.c',
    'src/crypto/x509/x509_set.c',
    'src/crypto/x509/x509_trs.c',
    'src/crypto/x509/x509_txt.c',
    'src/crypto/x509/x509_v3.c',
    'src/crypto/x509/x509_vfy.c',
    'src/crypto/x509/x509_vpm.c',
    'src/crypto/x509/x509cset.c',
    'src/crypto/x509/x509name.c',
    'src/crypto/x509/x509rset.c',
    'src/crypto/x509/x509spki.c',
    'src/crypto/x509/x509type.c',
    'src/crypto/x509/x_algor.c',
    'src/crypto/x509/x_all.c',
    'src/crypto/x509/x_attrib.c',
    'src/crypto/x509/x_crl.c',
    'src/crypto/x509/x_exten.c',
    'src/crypto/x509/x_info.c',
    'src/crypto/x509/x_name.c',
    'src/crypto/x509/x_pkey.c',
    'src/crypto/x509/x_pubkey.c',
    'src/crypto/x509/x_req.c',
    'src/crypto/x509/x_sig.c',
    'src/crypto/x509/x_spki.c',
    'src/crypto/x509/x_val.c',
    'src/crypto/x509/x_x509.c',
    'src/crypto/x509/x_x509a.c',
    'src/crypto/x509v3/pcy_cache.c',
    'src/crypto/x509v3/pcy_data.c',
    'src/crypto/x509v3/pcy_lib.c',
    'src/crypto/x509v3/pcy_map.c',
    'src/crypto/x509v3/pcy_node.c',
    'src/crypto/x509v3/pcy_tree.c',
    'src/crypto/x509v3/v3_akey.c',
    'src/crypto/x509v3/v3_akeya.c',
    'src/crypto/x509v3/v3_alt.c',
    'src/crypto/x509v3/v3_bcons.c',
    'src/crypto/x509v3/v3_bitst.c',
    'src/crypto/x509v3/v3_conf.c',
    'src/crypto/x509v3/v3_cpols.c',
    'src/crypto/x509v3/v3_crld.c',
    'src/crypto/x509v3/v3_enum.c',
    'src/crypto/x509v3/v3_extku.c',
    'src/crypto/x509v3/v3_genn.c',
    'src/crypto/x509v3/v3_ia5.c',
    'src/crypto/x509v3/v3_info.c',
    'src/crypto/x509v3/v3_int.c',
    'src/crypto/x509v3/v3_lib.c',
    'src/crypto/x509v3/v3_ncons.c',
    'src/crypto/x509v3/v3_pci.c',
    'src/crypto/x509v3/v3_pcia.c',
    'src/crypto/x509v3/v3_pcons.c',
    'src/crypto/x509v3/v3_pku.c',
    'src/crypto/x509v3/v3_pmaps.c',
    'src/crypto/x509v3/v3_prn.c',
    'src/crypto/x509v3/v3_purp.c',
    'src/crypto/x509v3/v3_skey.c',
    'src/crypto/x509v3/v3_sxnet.c',
    'src/crypto/x509v3/v3_utl.c',
    'src/ssl/custom_extensions.c',
    'src/ssl/d1_both.c',
    'src/ssl/d1_clnt.c',
    'src/ssl/d1_lib.c',
    'src/ssl/d1_meth.c',
    'src/ssl/d1_pkt.c',
    'src/ssl/d1_srtp.c',
    'src/ssl/d1_srvr.c',
    'src/ssl/dtls_record.c',
    'src/ssl/pqueue/pqueue.c',
    'src/ssl/s3_both.c',
    'src/ssl/s3_clnt.c',
    'src/ssl/s3_enc.c',
    'src/ssl/s3_lib.c',
    'src/ssl/s3_meth.c',
    'src/ssl/s3_pkt.c',
    'src/ssl/s3_srvr.c',
    'src/ssl/ssl_aead_ctx.c',
    'src/ssl/ssl_asn1.c',
    'src/ssl/ssl_buffer.c',
    'src/ssl/ssl_cert.c',
    'src/ssl/ssl_cipher.c',
    'src/ssl/ssl_ecdh.c',
    'src/ssl/ssl_file.c',
    'src/ssl/ssl_lib.c',
    'src/ssl/ssl_rsa.c',
    'src/ssl/ssl_session.c',
    'src/ssl/ssl_stat.c',
    'src/ssl/t1_enc.c',
    'src/ssl/t1_lib.c',
    'src/ssl/tls_record.c',
  ],
  hdrs = [
    'src/crypto/aes/internal.h',
    'src/crypto/asn1/asn1_locl.h',
    'src/crypto/bio/internal.h',
    'src/crypto/bn/internal.h',
    'src/crypto/bn/rsaz_exp.h',
    'src/crypto/bytestring/internal.h',
    'src/crypto/cipher/internal.h',
    'src/crypto/conf/conf_def.h',
    'src/crypto/conf/internal.h',
    'src/crypto/curve25519/internal.h',
    'src/crypto/des/internal.h',
    'src/crypto/dh/internal.h',
    'src/crypto/digest/internal.h',
    'src/crypto/digest/md32_common.h',
    'src/crypto/directory.h',
    'src/crypto/ec/internal.h',
    'src/crypto/ec/p256-x86_64-table.h',
    'src/crypto/evp/internal.h',
    'src/crypto/internal.h',
    'src/crypto/modes/internal.h',
    'src/crypto/obj/obj_dat.h',
    'src/crypto/obj/obj_xref.h',
    'src/crypto/pkcs8/internal.h',
    'src/crypto/rand/internal.h',
    'src/crypto/rsa/internal.h',
    'src/crypto/test/scoped_types.h',
    'src/crypto/test/test_util.h',
    'src/crypto/x509/charmap.h',
    'src/crypto/x509/vpm_int.h',
    'src/crypto/x509v3/ext_dat.h',
    'src/crypto/x509v3/pcy_int.h',
    'src/include/openssl/aead.h',
    'src/include/openssl/aes.h',
    'src/include/openssl/arm_arch.h',
    'src/include/openssl/asn1.h',
    'src/include/openssl/asn1_mac.h',
    'src/include/openssl/asn1t.h',
    'src/include/openssl/base.h',
    'src/include/openssl/base64.h',
    'src/include/openssl/bio.h',
    'src/include/openssl/blowfish.h',
    'src/include/openssl/bn.h',
    'src/include/openssl/buf.h',
    'src/include/openssl/buffer.h',
    'src/include/openssl/bytestring.h',
    'src/include/openssl/cast.h',
    'src/include/openssl/chacha.h',
    'src/include/openssl/cipher.h',
    'src/include/openssl/cmac.h',
    'src/include/openssl/conf.h',
    'src/include/openssl/cpu.h',
    'src/include/openssl/crypto.h',
    'src/include/openssl/curve25519.h',
    'src/include/openssl/des.h',
    'src/include/openssl/dh.h',
    'src/include/openssl/digest.h',
    'src/include/openssl/dsa.h',
    'src/include/openssl/dtls1.h',
    'src/include/openssl/ec.h',
    'src/include/openssl/ec_key.h',
    'src/include/openssl/ecdh.h',
    'src/include/openssl/ecdsa.h',
    'src/include/openssl/engine.h',
    'src/include/openssl/err.h',
    'src/include/openssl/evp.h',
    'src/include/openssl/ex_data.h',
    'src/include/openssl/hkdf.h',
    'src/include/openssl/hmac.h',
    'src/include/openssl/lhash.h',
    'src/include/openssl/lhash_macros.h',
    'src/include/openssl/md4.h',
    'src/include/openssl/md5.h',
    'src/include/openssl/mem.h',
    'src/include/openssl/obj.h',
    'src/include/openssl/obj_mac.h',
    'src/include/openssl/objects.h',
    'src/include/openssl/opensslfeatures.h',
    'src/include/openssl/opensslv.h',
    'src/include/openssl/ossl_typ.h',
    'src/include/openssl/pem.h',
    'src/include/openssl/pkcs12.h',
    'src/include/openssl/pkcs7.h',
    'src/include/openssl/pkcs8.h',
    'src/include/openssl/poly1305.h',
    'src/include/openssl/pqueue.h',
    'src/include/openssl/rand.h',
    'src/include/openssl/rc4.h',
    'src/include/openssl/rsa.h',
    'src/include/openssl/safestack.h',
    'src/include/openssl/sha.h',
    'src/include/openssl/srtp.h',
    'src/include/openssl/ssl.h',
    'src/include/openssl/ssl3.h',
    'src/include/openssl/stack.h',
    'src/include/openssl/stack_macros.h',
    'src/include/openssl/thread.h',
    'src/include/openssl/time_support.h',
    'src/include/openssl/tls1.h',
    'src/include/openssl/type_check.h',
    'src/include/openssl/x509.h',
    'src/include/openssl/x509_vfy.h',
    'src/include/openssl/x509v3.h',
    'src/ssl/internal.h',
    'src/ssl/test/async_bio.h',
    'src/ssl/test/packeted_bio.h',
    'src/ssl/test/scoped_types.h',
    'src/ssl/test/test_config.h',
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_test_util",
  linkstatic = 1,
  srcs = [
    'src/crypto/test/file_test.cc',
    'src/crypto/test/malloc.cc',
    'src/crypto/test/test_util.cc',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_aes_test_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/aes/aes_test.cc',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_asn1_test_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/asn1/asn1_test.cc',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_base64_test_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/base64/base64_test.cc',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_bio_test_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/bio/bio_test.cc',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_bn_test_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/bn/bn_test.cc',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_bytestring_test_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/bytestring/bytestring_test.cc',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_aead_test_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/cipher/aead_test.cc',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_cipher_test_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/cipher/cipher_test.cc',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_cmac_test_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/cmac/cmac_test.cc',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_constant_time_test_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/constant_time_test.c',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_ed25519_test_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/curve25519/ed25519_test.cc',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_x25519_test_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/curve25519/x25519_test.cc',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_dh_test_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/dh/dh_test.cc',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_digest_test_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/digest/digest_test.cc',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_dsa_test_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/dsa/dsa_test.c',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_ec_test_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/ec/ec_test.cc',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_example_mul_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/ec/example_mul.c',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_ecdsa_test_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/ecdsa/ecdsa_test.cc',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_err_test_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/err/err_test.cc',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_evp_extra_test_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/evp/evp_extra_test.cc',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_evp_test_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/evp/evp_test.cc',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_pbkdf_test_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/evp/pbkdf_test.cc',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_hkdf_test_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/hkdf/hkdf_test.c',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_hmac_test_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/hmac/hmac_test.cc',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_lhash_test_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/lhash/lhash_test.c',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_gcm_test_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/modes/gcm_test.c',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_pkcs12_test_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/pkcs8/pkcs12_test.cc',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_pkcs8_test_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/pkcs8/pkcs8_test.cc',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_poly1305_test_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/poly1305/poly1305_test.cc',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_refcount_test_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/refcount_test.c',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_rsa_test_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/rsa/rsa_test.cc',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_thread_test_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/thread_test.c',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_pkcs7_test_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/x509/pkcs7_test.c',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_x509_test_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/x509/x509_test.cc',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_tab_test_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/x509v3/tab_test.c',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_v3name_test_lib",
  linkstatic = 1,
  srcs = [
    'src/crypto/x509v3/v3name_test.c',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_pqueue_test_lib",
  linkstatic = 1,
  srcs = [
    'src/ssl/pqueue/pqueue_test.c',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)
cc_library(
  name = "boringssl_ssl_test_lib",
  linkstatic = 1,
  srcs = [
    'src/ssl/ssl_test.cc',
  ],
  hdrs = [
  ],
  includes = [
      'src/include',
  ],
  visibility = [
    "//visibility:public",
  ],
)

