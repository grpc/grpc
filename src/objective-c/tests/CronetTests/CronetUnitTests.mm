/*
 *
 * Copyright 2016 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#import <XCTest/XCTest.h>
#import <netinet/in.h>
#import <sys/socket.h>

#import "../ConfigureCronet.h"

#import <Cronet/Cronet.h>
#import <grpc/grpc.h>
#import <grpc/grpc_cronet.h>
#import "test/core/end2end/cq_verifier.h"
#import "test/core/util/port.h"

#import <grpc/support/alloc.h>
#import <grpc/support/log.h>

#import "src/core/lib/channel/channel_args.h"
#import "src/core/lib/gpr/string.h"
#import "src/core/lib/gpr/tmpfile.h"
#import "src/core/lib/gprpp/env.h"
#import "src/core/lib/gprpp/host_port.h"
#import "test/core/end2end/data/ssl_test_data.h"
#import "test/core/util/test_config.h"

#if COCOAPODS
#import <openssl_grpc/ssl.h>
#else
#import <openssl/ssl.h>
#endif

static void drain_cq(grpc_completion_queue *cq) {
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(cq, grpc_timeout_seconds_to_deadline(5), NULL);
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
}

@interface CronetUnitTests : XCTestCase

@end

@implementation CronetUnitTests

+ (void)setUp {
  [super setUp];

  char *argv[] = {(char *)"CoreCronetEnd2EndTests"};
  int argc = 1;
  grpc_test_init(&argc, argv);

  grpc_init();
  configureCronet(/*enable_netlog=*/false);
  init_ssl();
}

+ (void)tearDown {
  grpc_shutdown();
  cleanup_ssl();

  [super tearDown];
}

void init_ssl(void) {
  SSL_load_error_strings();
  OpenSSL_add_ssl_algorithms();
}

void cleanup_ssl(void) { EVP_cleanup(); }

int alpn_cb(SSL *ssl, const unsigned char **out, unsigned char *outlen, const unsigned char *in,
            unsigned int inlen, void *arg) {
  // Always select "h2" as the ALPN protocol to be used
  *out = (const unsigned char *)"h2";
  *outlen = 2;
  return SSL_TLSEXT_ERR_OK;
}

void init_ctx(SSL_CTX *ctx) {
  // Install server certificate
  BIO *pem = BIO_new_mem_buf((void *)test_server1_cert, (int)strlen(test_server1_cert));
  X509 *cert = PEM_read_bio_X509_AUX(pem, NULL, NULL, (char *)"");
  SSL_CTX_use_certificate(ctx, cert);
  X509_free(cert);
  BIO_free(pem);

  // Install server private key
  pem = BIO_new_mem_buf((void *)test_server1_key, (int)strlen(test_server1_key));
  EVP_PKEY *key = PEM_read_bio_PrivateKey(pem, NULL, NULL, (char *)"");
  SSL_CTX_use_PrivateKey(ctx, key);
  EVP_PKEY_free(key);
  BIO_free(pem);

  // Select cipher suite
  SSL_CTX_set_cipher_list(ctx, "ECDHE-RSA-AES128-GCM-SHA256");

  // Select ALPN protocol
  SSL_CTX_set_alpn_select_cb(ctx, alpn_cb, NULL);
}

unsigned int parse_h2_length(const char *field) {
  return ((unsigned int)(unsigned char)(field[0])) * 65536 +
         ((unsigned int)(unsigned char)(field[1])) * 256 +
         ((unsigned int)(unsigned char)(field[2]));
}

- (void)testInternalError {
  grpc_call *c;
  grpc_slice request_payload_slice = grpc_slice_from_copied_string("hello world");
  grpc_byte_buffer *request_payload = grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(5);
  grpc_metadata meta_c[2] = {{grpc_slice_from_static_string("key1"),
                              grpc_slice_from_static_string("val1"),
                              0,
                              {{NULL, NULL, NULL, NULL}}},
                             {grpc_slice_from_static_string("key2"),
                              grpc_slice_from_static_string("val2"),
                              0,
                              {{NULL, NULL, NULL, NULL}}}};

  int port = grpc_pick_unused_port_or_die();
  std::string addr = grpc_core::JoinHostPort("127.0.0.1", port);
  grpc_completion_queue *cq = grpc_completion_queue_create_for_next(NULL);
  stream_engine *cronetEngine = [Cronet getGlobalEngine];
  grpc_channel *client = grpc_cronet_secure_channel_create(cronetEngine, addr.c_str(), NULL, NULL);

  grpc_core::CqVerifier cqv(cq);
  grpc_op ops[6];
  grpc_op *op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_byte_buffer *response_payload_recv = NULL;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;

  c = grpc_channel_create_call(client, NULL, GRPC_PROPAGATE_DEFAULTS, cq,
                               grpc_slice_from_static_string("/foo"), NULL, deadline, NULL);
  GPR_ASSERT(c);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);

  int sl = socket(AF_INET, SOCK_STREAM, 0);
  GPR_ASSERT(sl >= 0);

  // Make an TCP endpoint to accept the connection
  struct sockaddr_in s_addr;
  memset(&s_addr, 0, sizeof(s_addr));
  s_addr.sin_family = AF_INET;
  s_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  s_addr.sin_port = htons(port);
  GPR_ASSERT(0 == bind(sl, (struct sockaddr *)&s_addr, sizeof(s_addr)));
  GPR_ASSERT(0 == listen(sl, 5));

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 2;
  op->data.send_initial_metadata.metadata = meta_c;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = request_payload;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &response_payload_recv;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), (void *)1, NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
    int s = accept(sl, NULL, NULL);
    GPR_ASSERT(s >= 0);

    // Close the connection after 1 second to trigger Cronet's on_failed()
    sleep(1);
    close(s);
    close(sl);
  });

  cqv.Expect((void *)1, true);
  cqv.Verify();

  GPR_ASSERT(status == GRPC_STATUS_UNAVAILABLE);

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  grpc_call_unref(c);

  grpc_byte_buffer_destroy(request_payload);
  grpc_byte_buffer_destroy(response_payload_recv);

  grpc_channel_destroy(client);
  grpc_completion_queue_shutdown(cq);
  drain_cq(cq);
  grpc_completion_queue_destroy(cq);
}

- (void)packetCoalescing:(BOOL)useCoalescing {
  grpc_arg arg;
  arg.key = (char *)GRPC_ARG_USE_CRONET_PACKET_COALESCING;
  arg.type = GRPC_ARG_INTEGER;
  arg.value.integer = useCoalescing ? 1 : 0;
  grpc_channel_args *args = grpc_channel_args_copy_and_add(NULL, &arg, 1);

  grpc_call *c;
  grpc_slice request_payload_slice = grpc_slice_from_copied_string("hello world");
  grpc_byte_buffer *request_payload = grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(5);
  grpc_metadata meta_c[2] = {{grpc_slice_from_static_string("key1"),
                              grpc_slice_from_static_string("val1"),
                              0,
                              {{NULL, NULL, NULL, NULL}}},
                             {grpc_slice_from_static_string("key2"),
                              grpc_slice_from_static_string("val2"),
                              0,
                              {{NULL, NULL, NULL, NULL}}}};

  int port = grpc_pick_unused_port_or_die();
  std::string addr = grpc_core::JoinHostPort("127.0.0.1", port);
  grpc_completion_queue *cq = grpc_completion_queue_create_for_next(NULL);
  stream_engine *cronetEngine = [Cronet getGlobalEngine];
  grpc_channel *client = grpc_cronet_secure_channel_create(cronetEngine, addr.c_str(), args, NULL);

  grpc_core::CqVerifier cqv(cq);
  grpc_op ops[6];
  grpc_op *op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_byte_buffer *response_payload_recv = NULL;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;

  c = grpc_channel_create_call(client, NULL, GRPC_PROPAGATE_DEFAULTS, cq,
                               grpc_slice_from_static_string("/foo"), NULL, deadline, NULL);
  GPR_ASSERT(c);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);

  __weak XCTestExpectation *expectation = [self expectationWithDescription:@"Coalescing"];

  int sl = socket(AF_INET, SOCK_STREAM, 0);
  GPR_ASSERT(sl >= 0);
  struct sockaddr_in s_addr;
  memset(&s_addr, 0, sizeof(s_addr));
  s_addr.sin_family = AF_INET;
  s_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  s_addr.sin_port = htons(port);
  GPR_ASSERT(0 == bind(sl, (struct sockaddr *)&s_addr, sizeof(s_addr)));
  GPR_ASSERT(0 == listen(sl, 5));

  dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
    int s = accept(sl, NULL, NULL);
    GPR_ASSERT(s >= 0);
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Make an TLS endpoint to receive Cronet's transmission
    SSL_CTX *ctx = SSL_CTX_new(TLSv1_2_server_method());
    init_ctx(ctx);
    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, s);
    SSL_accept(ssl);

    const char magic[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

    char buf[4096];
    long len;
    BOOL coalesced = NO;
    while ((len = SSL_read(ssl, buf, sizeof(buf))) > 0) {
      gpr_log(GPR_DEBUG, "Read len: %ld", len);

      // Analyze the HTTP/2 frames in the same TLS PDU to identify if
      // coalescing is successful
      unsigned int p = 0;
      while (p < len) {
        if (len - p >= 24 && 0 == memcmp(&buf[p], magic, 24)) {
          p += 24;
          continue;
        }

        if (buf[p + 3] == 0 &&                   // Type is DATA
            parse_h2_length(&buf[p]) == 0x10 &&  // Length is correct
            (buf[p + 4] & 1) != 0 &&             // EOS bit is set
            0 == memcmp("hello world", &buf[p + 14],
                        11)) {  // Message is correct
          coalesced = YES;
          break;
        }
        p += (parse_h2_length(&buf[p]) + 9);
      }
      if (coalesced) {
        break;
      }
    }

    XCTAssert(coalesced == useCoalescing);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    close(s);
    close(sl);
    [expectation fulfill];
  });

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 2;
  op->data.send_initial_metadata.metadata = meta_c;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = request_payload;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &response_payload_recv;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), (void *)1, NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  cqv.Expect((void *)1, true);
  cqv.Verify();

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  grpc_call_unref(c);

  grpc_byte_buffer_destroy(request_payload);
  grpc_byte_buffer_destroy(response_payload_recv);

  grpc_channel_destroy(client);
  grpc_completion_queue_shutdown(cq);
  drain_cq(cq);
  grpc_completion_queue_destroy(cq);

  [self waitForExpectationsWithTimeout:4 handler:nil];
}

- (void)testPacketCoalescing {
  [self packetCoalescing:YES];
  [self packetCoalescing:NO];
}

@end
