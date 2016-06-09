PHP_ARG_ENABLE(grpc, whether to enable grpc support,
[  --enable-grpc           Enable grpc support])

if test "$PHP_GRPC" != "no"; then
  dnl Write more examples of tests here...

  dnl # --with-grpc -> add include path
  PHP_ADD_INCLUDE(../../grpc/include)
  PHP_ADD_INCLUDE(../../grpc/src/php/ext/grpc)
  PHP_ADD_INCLUDE(../../grpc/third_party/boringssl/include)

  LIBS="-lpthread $LIBS"

  GRPC_SHARED_LIBADD="-lpthread $GRPC_SHARED_LIBADD"
  PHP_ADD_LIBRARY(pthread)

  PHP_ADD_LIBRARY(dl,,GRPC_SHARED_LIBADD)
  PHP_ADD_LIBRARY(dl)

  case $host in
    *darwin*) ;;
    *)
      PHP_ADD_LIBRARY(rt,,GRPC_SHARED_LIBADD)
      PHP_ADD_LIBRARY(rt)
      ;;
  esac

  PHP_NEW_EXTENSION(grpc,
    src/php/ext/grpc/byte_buffer.c \
    src/php/ext/grpc/call.c \
    src/php/ext/grpc/call_credentials.c \
    src/php/ext/grpc/channel.c \
    src/php/ext/grpc/channel_credentials.c \
    src/php/ext/grpc/completion_queue.c \
    src/php/ext/grpc/php_grpc.c \
    src/php/ext/grpc/server.c \
    src/php/ext/grpc/server_credentials.c \
    src/php/ext/grpc/timeval.c \
    src/core/lib/profiling/basic_timers.c \
    src/core/lib/profiling/stap_timers.c \
    src/core/lib/support/alloc.c \
    src/core/lib/support/avl.c \
    src/core/lib/support/backoff.c \
    src/core/lib/support/cmdline.c \
    src/core/lib/support/cpu_iphone.c \
    src/core/lib/support/cpu_linux.c \
    src/core/lib/support/cpu_posix.c \
    src/core/lib/support/cpu_windows.c \
    src/core/lib/support/env_linux.c \
    src/core/lib/support/env_posix.c \
    src/core/lib/support/env_windows.c \
    src/core/lib/support/histogram.c \
    src/core/lib/support/host_port.c \
    src/core/lib/support/load_file.c \
    src/core/lib/support/log.c \
    src/core/lib/support/log_android.c \
    src/core/lib/support/log_linux.c \
    src/core/lib/support/log_posix.c \
    src/core/lib/support/log_windows.c \
    src/core/lib/support/murmur_hash.c \
    src/core/lib/support/slice.c \
    src/core/lib/support/slice_buffer.c \
    src/core/lib/support/stack_lockfree.c \
    src/core/lib/support/string.c \
    src/core/lib/support/string_posix.c \
    src/core/lib/support/string_util_windows.c \
    src/core/lib/support/string_windows.c \
    src/core/lib/support/subprocess_posix.c \
    src/core/lib/support/subprocess_windows.c \
    src/core/lib/support/sync.c \
    src/core/lib/support/sync_posix.c \
    src/core/lib/support/sync_windows.c \
    src/core/lib/support/thd.c \
    src/core/lib/support/thd_posix.c \
    src/core/lib/support/thd_windows.c \
    src/core/lib/support/time.c \
    src/core/lib/support/time_posix.c \
    src/core/lib/support/time_precise.c \
    src/core/lib/support/time_windows.c \
    src/core/lib/support/tls_pthread.c \
    src/core/lib/support/tmpfile_msys.c \
    src/core/lib/support/tmpfile_posix.c \
    src/core/lib/support/tmpfile_windows.c \
    src/core/lib/support/wrap_memcpy.c \
    src/core/lib/surface/init.c \
    src/core/lib/channel/channel_args.c \
    src/core/lib/channel/channel_stack.c \
    src/core/lib/channel/channel_stack_builder.c \
    src/core/lib/channel/compress_filter.c \
    src/core/lib/channel/connected_channel.c \
    src/core/lib/channel/http_client_filter.c \
    src/core/lib/channel/http_server_filter.c \
    src/core/lib/compression/compression.c \
    src/core/lib/compression/message_compress.c \
    src/core/lib/debug/trace.c \
    src/core/lib/http/format_request.c \
    src/core/lib/http/httpcli.c \
    src/core/lib/http/parser.c \
    src/core/lib/iomgr/closure.c \
    src/core/lib/iomgr/endpoint.c \
    src/core/lib/iomgr/endpoint_pair_posix.c \
    src/core/lib/iomgr/endpoint_pair_windows.c \
    src/core/lib/iomgr/ev_poll_and_epoll_posix.c \
    src/core/lib/iomgr/ev_poll_posix.c \
    src/core/lib/iomgr/ev_posix.c \
    src/core/lib/iomgr/exec_ctx.c \
    src/core/lib/iomgr/executor.c \
    src/core/lib/iomgr/iocp_windows.c \
    src/core/lib/iomgr/iomgr.c \
    src/core/lib/iomgr/iomgr_posix.c \
    src/core/lib/iomgr/iomgr_windows.c \
    src/core/lib/iomgr/polling_entity.c \
    src/core/lib/iomgr/pollset_set_windows.c \
    src/core/lib/iomgr/pollset_windows.c \
    src/core/lib/iomgr/resolve_address_posix.c \
    src/core/lib/iomgr/resolve_address_windows.c \
    src/core/lib/iomgr/sockaddr_utils.c \
    src/core/lib/iomgr/socket_utils_common_posix.c \
    src/core/lib/iomgr/socket_utils_linux.c \
    src/core/lib/iomgr/socket_utils_posix.c \
    src/core/lib/iomgr/socket_windows.c \
    src/core/lib/iomgr/tcp_client_posix.c \
    src/core/lib/iomgr/tcp_client_windows.c \
    src/core/lib/iomgr/tcp_posix.c \
    src/core/lib/iomgr/tcp_server_posix.c \
    src/core/lib/iomgr/tcp_server_windows.c \
    src/core/lib/iomgr/tcp_windows.c \
    src/core/lib/iomgr/time_averaged_stats.c \
    src/core/lib/iomgr/timer.c \
    src/core/lib/iomgr/timer_heap.c \
    src/core/lib/iomgr/udp_server.c \
    src/core/lib/iomgr/unix_sockets_posix.c \
    src/core/lib/iomgr/unix_sockets_posix_noop.c \
    src/core/lib/iomgr/wakeup_fd_eventfd.c \
    src/core/lib/iomgr/wakeup_fd_nospecial.c \
    src/core/lib/iomgr/wakeup_fd_pipe.c \
    src/core/lib/iomgr/wakeup_fd_posix.c \
    src/core/lib/iomgr/workqueue_posix.c \
    src/core/lib/iomgr/workqueue_windows.c \
    src/core/lib/json/json.c \
    src/core/lib/json/json_reader.c \
    src/core/lib/json/json_string.c \
    src/core/lib/json/json_writer.c \
    src/core/lib/surface/alarm.c \
    src/core/lib/surface/api_trace.c \
    src/core/lib/surface/byte_buffer.c \
    src/core/lib/surface/byte_buffer_reader.c \
    src/core/lib/surface/call.c \
    src/core/lib/surface/call_details.c \
    src/core/lib/surface/call_log_batch.c \
    src/core/lib/surface/channel.c \
    src/core/lib/surface/channel_init.c \
    src/core/lib/surface/channel_ping.c \
    src/core/lib/surface/channel_stack_type.c \
    src/core/lib/surface/completion_queue.c \
    src/core/lib/surface/event_string.c \
    src/core/lib/surface/lame_client.c \
    src/core/lib/surface/metadata_array.c \
    src/core/lib/surface/server.c \
    src/core/lib/surface/validate_metadata.c \
    src/core/lib/surface/version.c \
    src/core/lib/transport/byte_stream.c \
    src/core/lib/transport/connectivity_state.c \
    src/core/lib/transport/metadata.c \
    src/core/lib/transport/metadata_batch.c \
    src/core/lib/transport/static_metadata.c \
    src/core/lib/transport/transport.c \
    src/core/lib/transport/transport_op_string.c \
    src/core/ext/transport/chttp2/server/secure/server_secure_chttp2.c \
    src/core/ext/transport/chttp2/transport/bin_encoder.c \
    src/core/ext/transport/chttp2/transport/chttp2_plugin.c \
    src/core/ext/transport/chttp2/transport/chttp2_transport.c \
    src/core/ext/transport/chttp2/transport/frame_data.c \
    src/core/ext/transport/chttp2/transport/frame_goaway.c \
    src/core/ext/transport/chttp2/transport/frame_ping.c \
    src/core/ext/transport/chttp2/transport/frame_rst_stream.c \
    src/core/ext/transport/chttp2/transport/frame_settings.c \
    src/core/ext/transport/chttp2/transport/frame_window_update.c \
    src/core/ext/transport/chttp2/transport/hpack_encoder.c \
    src/core/ext/transport/chttp2/transport/hpack_parser.c \
    src/core/ext/transport/chttp2/transport/hpack_table.c \
    src/core/ext/transport/chttp2/transport/huffsyms.c \
    src/core/ext/transport/chttp2/transport/incoming_metadata.c \
    src/core/ext/transport/chttp2/transport/parsing.c \
    src/core/ext/transport/chttp2/transport/status_conversion.c \
    src/core/ext/transport/chttp2/transport/stream_lists.c \
    src/core/ext/transport/chttp2/transport/stream_map.c \
    src/core/ext/transport/chttp2/transport/timeout_encoding.c \
    src/core/ext/transport/chttp2/transport/varint.c \
    src/core/ext/transport/chttp2/transport/writing.c \
    src/core/ext/transport/chttp2/alpn/alpn.c \
    src/core/lib/http/httpcli_security_connector.c \
    src/core/lib/security/context/security_context.c \
    src/core/lib/security/credentials/composite/composite_credentials.c \
    src/core/lib/security/credentials/credentials.c \
    src/core/lib/security/credentials/credentials_metadata.c \
    src/core/lib/security/credentials/fake/fake_credentials.c \
    src/core/lib/security/credentials/google_default/credentials_posix.c \
    src/core/lib/security/credentials/google_default/credentials_windows.c \
    src/core/lib/security/credentials/google_default/google_default_credentials.c \
    src/core/lib/security/credentials/iam/iam_credentials.c \
    src/core/lib/security/credentials/jwt/json_token.c \
    src/core/lib/security/credentials/jwt/jwt_credentials.c \
    src/core/lib/security/credentials/jwt/jwt_verifier.c \
    src/core/lib/security/credentials/oauth2/oauth2_credentials.c \
    src/core/lib/security/credentials/plugin/plugin_credentials.c \
    src/core/lib/security/credentials/ssl/ssl_credentials.c \
    src/core/lib/security/transport/client_auth_filter.c \
    src/core/lib/security/transport/handshake.c \
    src/core/lib/security/transport/secure_endpoint.c \
    src/core/lib/security/transport/security_connector.c \
    src/core/lib/security/transport/server_auth_filter.c \
    src/core/lib/security/util/b64.c \
    src/core/lib/security/util/json_util.c \
    src/core/lib/surface/init_secure.c \
    src/core/lib/tsi/fake_transport_security.c \
    src/core/lib/tsi/ssl_transport_security.c \
    src/core/lib/tsi/transport_security.c \
    src/core/ext/transport/chttp2/client/secure/secure_channel_create.c \
    src/core/ext/client_config/channel_connectivity.c \
    src/core/ext/client_config/client_channel.c \
    src/core/ext/client_config/client_channel_factory.c \
    src/core/ext/client_config/client_config.c \
    src/core/ext/client_config/client_config_plugin.c \
    src/core/ext/client_config/connector.c \
    src/core/ext/client_config/default_initial_connect_string.c \
    src/core/ext/client_config/initial_connect_string.c \
    src/core/ext/client_config/lb_policy.c \
    src/core/ext/client_config/lb_policy_factory.c \
    src/core/ext/client_config/lb_policy_registry.c \
    src/core/ext/client_config/parse_address.c \
    src/core/ext/client_config/resolver.c \
    src/core/ext/client_config/resolver_factory.c \
    src/core/ext/client_config/resolver_registry.c \
    src/core/ext/client_config/subchannel.c \
    src/core/ext/client_config/subchannel_call_holder.c \
    src/core/ext/client_config/subchannel_index.c \
    src/core/ext/client_config/uri_parser.c \
    src/core/ext/transport/chttp2/server/insecure/server_chttp2.c \
    src/core/ext/transport/chttp2/server/insecure/server_chttp2_posix.c \
    src/core/ext/transport/chttp2/client/insecure/channel_create.c \
    src/core/ext/transport/chttp2/client/insecure/channel_create_posix.c \
    src/core/ext/lb_policy/grpclb/load_balancer_api.c \
    src/core/ext/lb_policy/grpclb/proto/grpc/lb/v1/load_balancer.pb.c \
    third_party/nanopb/pb_common.c \
    third_party/nanopb/pb_decode.c \
    third_party/nanopb/pb_encode.c \
    src/core/ext/lb_policy/pick_first/pick_first.c \
    src/core/ext/lb_policy/round_robin/round_robin.c \
    src/core/ext/resolver/dns/native/dns_resolver.c \
    src/core/ext/resolver/sockaddr/sockaddr_resolver.c \
    src/core/ext/load_reporting/load_reporting.c \
    src/core/ext/load_reporting/load_reporting_filter.c \
    src/core/ext/census/context.c \
    src/core/ext/census/gen/census.pb.c \
    src/core/ext/census/grpc_context.c \
    src/core/ext/census/grpc_filter.c \
    src/core/ext/census/grpc_plugin.c \
    src/core/ext/census/initialize.c \
    src/core/ext/census/mlog.c \
    src/core/ext/census/operation.c \
    src/core/ext/census/placeholders.c \
    src/core/ext/census/tracing.c \
    src/core/plugin_registry/grpc_plugin_registry.c \
    src/boringssl/err_data.c \
    third_party/boringssl/crypto/aes/aes.c \
    third_party/boringssl/crypto/aes/mode_wrappers.c \
    third_party/boringssl/crypto/asn1/a_bitstr.c \
    third_party/boringssl/crypto/asn1/a_bool.c \
    third_party/boringssl/crypto/asn1/a_bytes.c \
    third_party/boringssl/crypto/asn1/a_d2i_fp.c \
    third_party/boringssl/crypto/asn1/a_dup.c \
    third_party/boringssl/crypto/asn1/a_enum.c \
    third_party/boringssl/crypto/asn1/a_gentm.c \
    third_party/boringssl/crypto/asn1/a_i2d_fp.c \
    third_party/boringssl/crypto/asn1/a_int.c \
    third_party/boringssl/crypto/asn1/a_mbstr.c \
    third_party/boringssl/crypto/asn1/a_object.c \
    third_party/boringssl/crypto/asn1/a_octet.c \
    third_party/boringssl/crypto/asn1/a_print.c \
    third_party/boringssl/crypto/asn1/a_strnid.c \
    third_party/boringssl/crypto/asn1/a_time.c \
    third_party/boringssl/crypto/asn1/a_type.c \
    third_party/boringssl/crypto/asn1/a_utctm.c \
    third_party/boringssl/crypto/asn1/a_utf8.c \
    third_party/boringssl/crypto/asn1/asn1_lib.c \
    third_party/boringssl/crypto/asn1/asn1_par.c \
    third_party/boringssl/crypto/asn1/asn_pack.c \
    third_party/boringssl/crypto/asn1/bio_asn1.c \
    third_party/boringssl/crypto/asn1/bio_ndef.c \
    third_party/boringssl/crypto/asn1/f_enum.c \
    third_party/boringssl/crypto/asn1/f_int.c \
    third_party/boringssl/crypto/asn1/f_string.c \
    third_party/boringssl/crypto/asn1/t_bitst.c \
    third_party/boringssl/crypto/asn1/t_pkey.c \
    third_party/boringssl/crypto/asn1/tasn_dec.c \
    third_party/boringssl/crypto/asn1/tasn_enc.c \
    third_party/boringssl/crypto/asn1/tasn_fre.c \
    third_party/boringssl/crypto/asn1/tasn_new.c \
    third_party/boringssl/crypto/asn1/tasn_prn.c \
    third_party/boringssl/crypto/asn1/tasn_typ.c \
    third_party/boringssl/crypto/asn1/tasn_utl.c \
    third_party/boringssl/crypto/asn1/x_bignum.c \
    third_party/boringssl/crypto/asn1/x_long.c \
    third_party/boringssl/crypto/base64/base64.c \
    third_party/boringssl/crypto/bio/bio.c \
    third_party/boringssl/crypto/bio/bio_mem.c \
    third_party/boringssl/crypto/bio/buffer.c \
    third_party/boringssl/crypto/bio/connect.c \
    third_party/boringssl/crypto/bio/fd.c \
    third_party/boringssl/crypto/bio/file.c \
    third_party/boringssl/crypto/bio/hexdump.c \
    third_party/boringssl/crypto/bio/pair.c \
    third_party/boringssl/crypto/bio/printf.c \
    third_party/boringssl/crypto/bio/socket.c \
    third_party/boringssl/crypto/bio/socket_helper.c \
    third_party/boringssl/crypto/bn/add.c \
    third_party/boringssl/crypto/bn/asm/x86_64-gcc.c \
    third_party/boringssl/crypto/bn/bn.c \
    third_party/boringssl/crypto/bn/bn_asn1.c \
    third_party/boringssl/crypto/bn/cmp.c \
    third_party/boringssl/crypto/bn/convert.c \
    third_party/boringssl/crypto/bn/ctx.c \
    third_party/boringssl/crypto/bn/div.c \
    third_party/boringssl/crypto/bn/exponentiation.c \
    third_party/boringssl/crypto/bn/gcd.c \
    third_party/boringssl/crypto/bn/generic.c \
    third_party/boringssl/crypto/bn/kronecker.c \
    third_party/boringssl/crypto/bn/montgomery.c \
    third_party/boringssl/crypto/bn/mul.c \
    third_party/boringssl/crypto/bn/prime.c \
    third_party/boringssl/crypto/bn/random.c \
    third_party/boringssl/crypto/bn/rsaz_exp.c \
    third_party/boringssl/crypto/bn/shift.c \
    third_party/boringssl/crypto/bn/sqrt.c \
    third_party/boringssl/crypto/buf/buf.c \
    third_party/boringssl/crypto/bytestring/asn1_compat.c \
    third_party/boringssl/crypto/bytestring/ber.c \
    third_party/boringssl/crypto/bytestring/cbb.c \
    third_party/boringssl/crypto/bytestring/cbs.c \
    third_party/boringssl/crypto/chacha/chacha_generic.c \
    third_party/boringssl/crypto/chacha/chacha_vec.c \
    third_party/boringssl/crypto/cipher/aead.c \
    third_party/boringssl/crypto/cipher/cipher.c \
    third_party/boringssl/crypto/cipher/derive_key.c \
    third_party/boringssl/crypto/cipher/e_aes.c \
    third_party/boringssl/crypto/cipher/e_chacha20poly1305.c \
    third_party/boringssl/crypto/cipher/e_des.c \
    third_party/boringssl/crypto/cipher/e_null.c \
    third_party/boringssl/crypto/cipher/e_rc2.c \
    third_party/boringssl/crypto/cipher/e_rc4.c \
    third_party/boringssl/crypto/cipher/e_ssl3.c \
    third_party/boringssl/crypto/cipher/e_tls.c \
    third_party/boringssl/crypto/cipher/tls_cbc.c \
    third_party/boringssl/crypto/cmac/cmac.c \
    third_party/boringssl/crypto/conf/conf.c \
    third_party/boringssl/crypto/cpu-arm.c \
    third_party/boringssl/crypto/cpu-intel.c \
    third_party/boringssl/crypto/crypto.c \
    third_party/boringssl/crypto/curve25519/curve25519.c \
    third_party/boringssl/crypto/curve25519/x25519-x86_64.c \
    third_party/boringssl/crypto/des/des.c \
    third_party/boringssl/crypto/dh/check.c \
    third_party/boringssl/crypto/dh/dh.c \
    third_party/boringssl/crypto/dh/dh_asn1.c \
    third_party/boringssl/crypto/dh/params.c \
    third_party/boringssl/crypto/digest/digest.c \
    third_party/boringssl/crypto/digest/digests.c \
    third_party/boringssl/crypto/directory_posix.c \
    third_party/boringssl/crypto/directory_win.c \
    third_party/boringssl/crypto/dsa/dsa.c \
    third_party/boringssl/crypto/dsa/dsa_asn1.c \
    third_party/boringssl/crypto/ec/ec.c \
    third_party/boringssl/crypto/ec/ec_asn1.c \
    third_party/boringssl/crypto/ec/ec_key.c \
    third_party/boringssl/crypto/ec/ec_montgomery.c \
    third_party/boringssl/crypto/ec/oct.c \
    third_party/boringssl/crypto/ec/p224-64.c \
    third_party/boringssl/crypto/ec/p256-64.c \
    third_party/boringssl/crypto/ec/p256-x86_64.c \
    third_party/boringssl/crypto/ec/simple.c \
    third_party/boringssl/crypto/ec/util-64.c \
    third_party/boringssl/crypto/ec/wnaf.c \
    third_party/boringssl/crypto/ecdh/ecdh.c \
    third_party/boringssl/crypto/ecdsa/ecdsa.c \
    third_party/boringssl/crypto/ecdsa/ecdsa_asn1.c \
    third_party/boringssl/crypto/engine/engine.c \
    third_party/boringssl/crypto/err/err.c \
    third_party/boringssl/crypto/evp/algorithm.c \
    third_party/boringssl/crypto/evp/digestsign.c \
    third_party/boringssl/crypto/evp/evp.c \
    third_party/boringssl/crypto/evp/evp_asn1.c \
    third_party/boringssl/crypto/evp/evp_ctx.c \
    third_party/boringssl/crypto/evp/p_dsa_asn1.c \
    third_party/boringssl/crypto/evp/p_ec.c \
    third_party/boringssl/crypto/evp/p_ec_asn1.c \
    third_party/boringssl/crypto/evp/p_rsa.c \
    third_party/boringssl/crypto/evp/p_rsa_asn1.c \
    third_party/boringssl/crypto/evp/pbkdf.c \
    third_party/boringssl/crypto/evp/sign.c \
    third_party/boringssl/crypto/ex_data.c \
    third_party/boringssl/crypto/hkdf/hkdf.c \
    third_party/boringssl/crypto/hmac/hmac.c \
    third_party/boringssl/crypto/lhash/lhash.c \
    third_party/boringssl/crypto/md4/md4.c \
    third_party/boringssl/crypto/md5/md5.c \
    third_party/boringssl/crypto/mem.c \
    third_party/boringssl/crypto/modes/cbc.c \
    third_party/boringssl/crypto/modes/cfb.c \
    third_party/boringssl/crypto/modes/ctr.c \
    third_party/boringssl/crypto/modes/gcm.c \
    third_party/boringssl/crypto/modes/ofb.c \
    third_party/boringssl/crypto/obj/obj.c \
    third_party/boringssl/crypto/obj/obj_xref.c \
    third_party/boringssl/crypto/pem/pem_all.c \
    third_party/boringssl/crypto/pem/pem_info.c \
    third_party/boringssl/crypto/pem/pem_lib.c \
    third_party/boringssl/crypto/pem/pem_oth.c \
    third_party/boringssl/crypto/pem/pem_pk8.c \
    third_party/boringssl/crypto/pem/pem_pkey.c \
    third_party/boringssl/crypto/pem/pem_x509.c \
    third_party/boringssl/crypto/pem/pem_xaux.c \
    third_party/boringssl/crypto/pkcs8/p5_pbe.c \
    third_party/boringssl/crypto/pkcs8/p5_pbev2.c \
    third_party/boringssl/crypto/pkcs8/p8_pkey.c \
    third_party/boringssl/crypto/pkcs8/pkcs8.c \
    third_party/boringssl/crypto/poly1305/poly1305.c \
    third_party/boringssl/crypto/poly1305/poly1305_arm.c \
    third_party/boringssl/crypto/poly1305/poly1305_vec.c \
    third_party/boringssl/crypto/rand/rand.c \
    third_party/boringssl/crypto/rand/urandom.c \
    third_party/boringssl/crypto/rand/windows.c \
    third_party/boringssl/crypto/rc4/rc4.c \
    third_party/boringssl/crypto/refcount_c11.c \
    third_party/boringssl/crypto/refcount_lock.c \
    third_party/boringssl/crypto/rsa/blinding.c \
    third_party/boringssl/crypto/rsa/padding.c \
    third_party/boringssl/crypto/rsa/rsa.c \
    third_party/boringssl/crypto/rsa/rsa_asn1.c \
    third_party/boringssl/crypto/rsa/rsa_impl.c \
    third_party/boringssl/crypto/sha/sha1.c \
    third_party/boringssl/crypto/sha/sha256.c \
    third_party/boringssl/crypto/sha/sha512.c \
    third_party/boringssl/crypto/stack/stack.c \
    third_party/boringssl/crypto/thread.c \
    third_party/boringssl/crypto/thread_none.c \
    third_party/boringssl/crypto/thread_pthread.c \
    third_party/boringssl/crypto/thread_win.c \
    third_party/boringssl/crypto/time_support.c \
    third_party/boringssl/crypto/x509/a_digest.c \
    third_party/boringssl/crypto/x509/a_sign.c \
    third_party/boringssl/crypto/x509/a_strex.c \
    third_party/boringssl/crypto/x509/a_verify.c \
    third_party/boringssl/crypto/x509/asn1_gen.c \
    third_party/boringssl/crypto/x509/by_dir.c \
    third_party/boringssl/crypto/x509/by_file.c \
    third_party/boringssl/crypto/x509/i2d_pr.c \
    third_party/boringssl/crypto/x509/pkcs7.c \
    third_party/boringssl/crypto/x509/t_crl.c \
    third_party/boringssl/crypto/x509/t_req.c \
    third_party/boringssl/crypto/x509/t_x509.c \
    third_party/boringssl/crypto/x509/t_x509a.c \
    third_party/boringssl/crypto/x509/x509.c \
    third_party/boringssl/crypto/x509/x509_att.c \
    third_party/boringssl/crypto/x509/x509_cmp.c \
    third_party/boringssl/crypto/x509/x509_d2.c \
    third_party/boringssl/crypto/x509/x509_def.c \
    third_party/boringssl/crypto/x509/x509_ext.c \
    third_party/boringssl/crypto/x509/x509_lu.c \
    third_party/boringssl/crypto/x509/x509_obj.c \
    third_party/boringssl/crypto/x509/x509_r2x.c \
    third_party/boringssl/crypto/x509/x509_req.c \
    third_party/boringssl/crypto/x509/x509_set.c \
    third_party/boringssl/crypto/x509/x509_trs.c \
    third_party/boringssl/crypto/x509/x509_txt.c \
    third_party/boringssl/crypto/x509/x509_v3.c \
    third_party/boringssl/crypto/x509/x509_vfy.c \
    third_party/boringssl/crypto/x509/x509_vpm.c \
    third_party/boringssl/crypto/x509/x509cset.c \
    third_party/boringssl/crypto/x509/x509name.c \
    third_party/boringssl/crypto/x509/x509rset.c \
    third_party/boringssl/crypto/x509/x509spki.c \
    third_party/boringssl/crypto/x509/x509type.c \
    third_party/boringssl/crypto/x509/x_algor.c \
    third_party/boringssl/crypto/x509/x_all.c \
    third_party/boringssl/crypto/x509/x_attrib.c \
    third_party/boringssl/crypto/x509/x_crl.c \
    third_party/boringssl/crypto/x509/x_exten.c \
    third_party/boringssl/crypto/x509/x_info.c \
    third_party/boringssl/crypto/x509/x_name.c \
    third_party/boringssl/crypto/x509/x_pkey.c \
    third_party/boringssl/crypto/x509/x_pubkey.c \
    third_party/boringssl/crypto/x509/x_req.c \
    third_party/boringssl/crypto/x509/x_sig.c \
    third_party/boringssl/crypto/x509/x_spki.c \
    third_party/boringssl/crypto/x509/x_val.c \
    third_party/boringssl/crypto/x509/x_x509.c \
    third_party/boringssl/crypto/x509/x_x509a.c \
    third_party/boringssl/crypto/x509v3/pcy_cache.c \
    third_party/boringssl/crypto/x509v3/pcy_data.c \
    third_party/boringssl/crypto/x509v3/pcy_lib.c \
    third_party/boringssl/crypto/x509v3/pcy_map.c \
    third_party/boringssl/crypto/x509v3/pcy_node.c \
    third_party/boringssl/crypto/x509v3/pcy_tree.c \
    third_party/boringssl/crypto/x509v3/v3_akey.c \
    third_party/boringssl/crypto/x509v3/v3_akeya.c \
    third_party/boringssl/crypto/x509v3/v3_alt.c \
    third_party/boringssl/crypto/x509v3/v3_bcons.c \
    third_party/boringssl/crypto/x509v3/v3_bitst.c \
    third_party/boringssl/crypto/x509v3/v3_conf.c \
    third_party/boringssl/crypto/x509v3/v3_cpols.c \
    third_party/boringssl/crypto/x509v3/v3_crld.c \
    third_party/boringssl/crypto/x509v3/v3_enum.c \
    third_party/boringssl/crypto/x509v3/v3_extku.c \
    third_party/boringssl/crypto/x509v3/v3_genn.c \
    third_party/boringssl/crypto/x509v3/v3_ia5.c \
    third_party/boringssl/crypto/x509v3/v3_info.c \
    third_party/boringssl/crypto/x509v3/v3_int.c \
    third_party/boringssl/crypto/x509v3/v3_lib.c \
    third_party/boringssl/crypto/x509v3/v3_ncons.c \
    third_party/boringssl/crypto/x509v3/v3_pci.c \
    third_party/boringssl/crypto/x509v3/v3_pcia.c \
    third_party/boringssl/crypto/x509v3/v3_pcons.c \
    third_party/boringssl/crypto/x509v3/v3_pku.c \
    third_party/boringssl/crypto/x509v3/v3_pmaps.c \
    third_party/boringssl/crypto/x509v3/v3_prn.c \
    third_party/boringssl/crypto/x509v3/v3_purp.c \
    third_party/boringssl/crypto/x509v3/v3_skey.c \
    third_party/boringssl/crypto/x509v3/v3_sxnet.c \
    third_party/boringssl/crypto/x509v3/v3_utl.c \
    third_party/boringssl/ssl/custom_extensions.c \
    third_party/boringssl/ssl/d1_both.c \
    third_party/boringssl/ssl/d1_clnt.c \
    third_party/boringssl/ssl/d1_lib.c \
    third_party/boringssl/ssl/d1_meth.c \
    third_party/boringssl/ssl/d1_pkt.c \
    third_party/boringssl/ssl/d1_srtp.c \
    third_party/boringssl/ssl/d1_srvr.c \
    third_party/boringssl/ssl/dtls_record.c \
    third_party/boringssl/ssl/pqueue/pqueue.c \
    third_party/boringssl/ssl/s3_both.c \
    third_party/boringssl/ssl/s3_clnt.c \
    third_party/boringssl/ssl/s3_enc.c \
    third_party/boringssl/ssl/s3_lib.c \
    third_party/boringssl/ssl/s3_meth.c \
    third_party/boringssl/ssl/s3_pkt.c \
    third_party/boringssl/ssl/s3_srvr.c \
    third_party/boringssl/ssl/ssl_aead_ctx.c \
    third_party/boringssl/ssl/ssl_asn1.c \
    third_party/boringssl/ssl/ssl_buffer.c \
    third_party/boringssl/ssl/ssl_cert.c \
    third_party/boringssl/ssl/ssl_cipher.c \
    third_party/boringssl/ssl/ssl_ecdh.c \
    third_party/boringssl/ssl/ssl_file.c \
    third_party/boringssl/ssl/ssl_lib.c \
    third_party/boringssl/ssl/ssl_rsa.c \
    third_party/boringssl/ssl/ssl_session.c \
    third_party/boringssl/ssl/ssl_stat.c \
    third_party/boringssl/ssl/t1_enc.c \
    third_party/boringssl/ssl/t1_lib.c \
    third_party/boringssl/ssl/tls_record.c \
    , $ext_shared, , -Wall -Werror \
    -Wno-parentheses-equality -Wno-unused-value -std=c11 \
    -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN \
    -D_HAS_EXCEPTIONS=0 -DNOMINMAX)

  PHP_ADD_BUILD_DIR($ext_builddir/src/php/ext/grpc)

  PHP_ADD_BUILD_DIR($ext_builddir/src/boringssl)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/ext/census)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/ext/census/gen)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/ext/client_config)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/ext/lb_policy/grpclb)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/ext/lb_policy/grpclb/proto/grpc/lb/v1)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/ext/lb_policy/pick_first)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/ext/lb_policy/round_robin)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/ext/load_reporting)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/ext/resolver/dns/native)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/ext/resolver/sockaddr)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/ext/transport/chttp2/alpn)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/ext/transport/chttp2/client/insecure)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/ext/transport/chttp2/client/secure)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/ext/transport/chttp2/server/insecure)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/ext/transport/chttp2/server/secure)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/ext/transport/chttp2/transport)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/lib/channel)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/lib/compression)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/lib/debug)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/lib/http)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/lib/iomgr)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/lib/json)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/lib/profiling)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/lib/security/context)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/lib/security/credentials)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/lib/security/credentials/composite)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/lib/security/credentials/fake)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/lib/security/credentials/google_default)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/lib/security/credentials/iam)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/lib/security/credentials/jwt)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/lib/security/credentials/oauth2)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/lib/security/credentials/plugin)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/lib/security/credentials/ssl)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/lib/security/transport)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/lib/security/util)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/lib/support)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/lib/surface)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/lib/transport)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/lib/tsi)
  PHP_ADD_BUILD_DIR($ext_builddir/src/core/plugin_registry)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/aes)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/asn1)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/base64)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/bio)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/bn)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/bn/asm)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/buf)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/bytestring)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/chacha)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/cipher)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/cmac)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/conf)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/curve25519)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/des)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/dh)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/digest)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/dsa)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/ec)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/ecdh)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/ecdsa)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/engine)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/err)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/evp)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/hkdf)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/hmac)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/lhash)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/md4)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/md5)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/modes)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/obj)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/pem)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/pkcs8)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/poly1305)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/rand)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/rc4)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/rsa)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/sha)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/stack)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/x509)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/crypto/x509v3)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/ssl)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/boringssl/ssl/pqueue)
  PHP_ADD_BUILD_DIR($ext_builddir/third_party/nanopb)
fi
