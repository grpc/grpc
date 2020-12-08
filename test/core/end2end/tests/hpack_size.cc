/*
 *
 * Copyright 2015 gRPC authors.
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

#include "test/core/end2end/end2end_tests.h"

#include <stdio.h>
#include <string.h>

#include <string>

#include "absl/strings/str_format.h"

#include <grpc/byte_buffer.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "test/core/end2end/cq_verifier.h"

static void* tag(intptr_t t) { return reinterpret_cast<void*>(t); }

const char* hobbits[][2] = {
    {"Adaldrida", "Brandybuck"}, {"Adamanta", "Took"},
    {"Adalgrim", "Took"},        {"Adelard", "Took"},
    {"Amaranth", "Brandybuck"},  {"Andwise", "Roper"},
    {"Angelica", "Baggins"},     {"Asphodel", "Burrows"},
    {"Balbo", "Baggins"},        {"Bandobras", "Took"},
    {"Belba", "Bolger"},         {"Bell", "Gamgee"},
    {"Belladonna", "Baggins"},   {"Berylla", "Baggins"},
    {"Bilbo", "Baggins"},        {"Bilbo", "Gardner"},
    {"Bill", "Butcher"},         {"Bingo", "Baggins"},
    {"Bodo", "Proudfoot"},       {"Bowman", "Cotton"},
    {"Bungo", "Baggins"},        {"Camellia", "Sackville"},
    {"Carl", "Cotton"},          {"Celandine", "Brandybuck"},
    {"Chica", "Baggins"},        {"Daddy", "Twofoot"},
    {"Daisy", "Boffin"},         {"Diamond", "Took"},
    {"Dinodas", "Brandybuck"},   {"Doderic", "Brandybuck"},
    {"Dodinas", "Brandybuck"},   {"Donnamira", "Boffin"},
    {"Dora", "Baggins"},         {"Drogo", "Baggins"},
    {"Dudo", "Baggins"},         {"Eglantine", "Took"},
    {"Elanor", "Fairbairn"},     {"Elfstan", "Fairbairn"},
    {"Esmeralda", "Brandybuck"}, {"Estella", "Brandybuck"},
    {"Everard", "Took"},         {"Falco", "Chubb-Baggins"},
    {"Faramir", "Took"},         {"Farmer", "Maggot"},
    {"Fastolph", "Bolger"},      {"Ferdibrand", "Took"},
    {"Ferdinand", "Took"},       {"Ferumbras", "Took"},
    {"Ferumbras", "Took"},       {"Filibert", "Bolger"},
    {"Firiel", "Fairbairn"},     {"Flambard", "Took"},
    {"Folco", "Boffin"},         {"Fortinbras", "Took"},
    {"Fortinbras", "Took"},      {"Fosco", "Baggins"},
    {"Fredegar", "Bolger"},      {"Frodo", "Baggins"},
    {"Frodo", "Gardner"},        {"Gerontius", "Took"},
    {"Gilly", "Baggins"},        {"Goldilocks", "Took"},
    {"Gorbadoc", "Brandybuck"},  {"Gorbulas", "Brandybuck"},
    {"Gorhendad", "Brandybuck"}, {"Gormadoc", "Brandybuck"},
    {"Griffo", "Boffin"},        {"Halfast", "Gamgee"},
    {"Halfred", "Gamgee"},       {"Halfred", "Greenhand"},
    {"Hanna", "Brandybuck"},     {"Hamfast", "Gamgee"},
    {"Hamfast", "Gardner"},      {"Hamson", "Gamgee"},
    {"Harding", "Gardner"},      {"Hilda", "Brandybuck"},
    {"Hildibrand", "Took"},      {"Hildifons", "Took"},
    {"Hildigard", "Took"},       {"Hildigrim", "Took"},
    {"Hob", "Gammidge"},         {"Hob", "Hayward"},
    {"Hobson", "Gamgee"},        {"Holfast", "Gardner"},
    {"Holman", "Cotton"},        {"Holman", "Greenhand"},
    {"Hugo", "Boffin"},          {"Hugo", "Bracegirdle"},
    {"Ilberic", "Brandybuck"},   {"Isembard", "Took"},
    {"Isembold", "Took"},        {"Isengar", "Took"},
    {"Isengrim", "Took"},        {"Isengrim", "Took"},
    {"Isumbras", "Took"},        {"Isumbras", "Took"},
    {"Jolly", "Cotton"},
    /*
    {"Lalia", "Took"},
    {"Largo", "Baggins"},
    {"Laura", "Baggins"},
    {"Lily", "Goodbody"},
    {"Lily", "Cotton"},
    {"Linda", "Proudfoot"},
    {"Lobelia", "Sackville-Baggins"},
    {"Longo", "Baggins"},
    {"Lotho", "Sackville-Baggins"},
    {"Madoc", "Brandybuck"},
    {"Malva", "Brandybuck"},
    {"Marigold", "Cotton"},
    {"Marmadas", "Brandybuck"},
    {"Marmadoc", "Brandybuck"},
    {"Marroc", "Brandybuck"},
    {"May", "Gamgee"},
    {"Melilot", "Brandybuck"},
    {"Menegilda", "Brandybuck"},
    {"Mentha", "Brandybuck"},
    {"Meriadoc", "Brandybuck"},
    {"Merimac", "Brandybuck"},
    {"Merimas", "Brandybuck"},
    {"Merry", "Gardner"},
    {"Milo", "Burrows"},
    {"Mimosa", "Baggins"},
    {"Minto", "Burrows"},
    {"Mirabella", "Brandybuck"},
    {"Moro", "Burrows"},
    {"Mosco", "Burrows"},
    {"Mungo", "Baggins"},
    {"Myrtle", "Burrows"},
    {"Odo", "Proudfoot"},
    {"Odovacar", "Bolger"},
    {"Olo", "Proudfoot"},
    {"Orgulas", "Brandybuck"},
    {"Otho", "Sackville-Baggins"},
    {"Paladin", "Took"},
    {"Pansy", "Bolger"},
    {"Pearl", "Took"},
    {"Peony", "Burrows"},
    {"Peregrin", "Took"},
    {"Pervinca", "Took"},
    {"Pimpernel", "Took"},
    {"Pippin", "Gardner"},
    {"Polo", "Baggins"},
    {"Ponto", "Baggins"},
    {"Porto", "Baggins"},
    {"Posco", "Baggins"},
    {"Poppy", "Bolger"},
    {"Primrose", "Gardner"},
    {"Primula", "Baggins"},
    {"Prisca", "Bolger"},
    {"Reginard", "Took"},
    {"Robin", "Smallburrow"},
    {"Robin", "Gardner"},
    {"Rorimac", "Brandybuck"},
    {"Rosa", "Took"},
    {"Rosamunda", "Bolger"},
    {"Rose", "Gardner"},
    {"Ruby", "Baggins"},
    {"Ruby", "Gardner"},
    {"Rudigar", "Bolger"},
    {"Rufus", "Burrows"},
    {"Sadoc", "Brandybuck"},
    {"Salvia", "Bolger"},
    {"Samwise", "Gamgee"},
    {"Sancho", "Proudfoot"},
    {"Saradas", "Brandybuck"},
    {"Saradoc", "Brandybuck"},
    {"Seredic", "Brandybuck"},
    {"Sigismond", "Took"},
    {"Smeagol", "Gollum"},
    {"Tanta", "Baggins"},
    {"Ted", "Sandyman"},
    {"Tobold", "Hornblower"},
    {"Togo", "Goodbody"},
    {"Tolman", "Cotton"},
    {"Tolman", "Gardner"},
    {"Widow", "Rumble"},
    {"Wilcome", "Cotton"},
    {"Wilcome", "Cotton"},
    {"Wilibald", "Bolger"},
    {"Will", "Whitfoot"},
    {"Wiseman", "Gamwich"}*/
};

const char* dragons[] = {"Ancalagon", "Glaurung", "Scatha",
                         "Smaug the Magnificent"};

static grpc_end2end_test_fixture begin_test(grpc_end2end_test_config config,
                                            const char* test_name,
                                            grpc_channel_args* client_args,
                                            grpc_channel_args* server_args) {
  grpc_end2end_test_fixture f;
  gpr_log(GPR_INFO, "Running test: %s/%s", test_name, config.name);
  f = config.create_fixture(client_args, server_args);
  config.init_server(&f, server_args);
  config.init_client(&f, client_args);
  return f;
}

static gpr_timespec n_seconds_from_now(int n) {
  return grpc_timeout_seconds_to_deadline(n);
}

static gpr_timespec five_seconds_from_now(void) {
  return n_seconds_from_now(5);
}

static void drain_cq(grpc_completion_queue* cq) {
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(cq, five_seconds_from_now(), nullptr);
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
}

static void shutdown_server(grpc_end2end_test_fixture* f) {
  if (!f->server) return;
  grpc_server_shutdown_and_notify(f->server, f->shutdown_cq, tag(1000));
  GPR_ASSERT(grpc_completion_queue_pluck(f->shutdown_cq, tag(1000),
                                         grpc_timeout_seconds_to_deadline(5),
                                         nullptr)
                 .type == GRPC_OP_COMPLETE);
  grpc_server_destroy(f->server);
  f->server = nullptr;
}

static void shutdown_client(grpc_end2end_test_fixture* f) {
  if (!f->client) return;
  grpc_channel_destroy(f->client);
  f->client = nullptr;
}

static void end_test(grpc_end2end_test_fixture* f) {
  shutdown_server(f);
  shutdown_client(f);

  grpc_completion_queue_shutdown(f->cq);
  drain_cq(f->cq);
  grpc_completion_queue_destroy(f->cq);
  grpc_completion_queue_destroy(f->shutdown_cq);
}

static void simple_request_body(grpc_end2end_test_config /*config*/,
                                grpc_end2end_test_fixture f, size_t index) {
  grpc_call* c;
  grpc_call* s;
  cq_verifier* cqv = cq_verifier_create(f.cq);
  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  grpc_metadata extra_metadata[3];
  grpc_slice details;
  int was_cancelled = 2;

  memset(extra_metadata, 0, sizeof(extra_metadata));
  extra_metadata[0].key = grpc_slice_from_static_string("hobbit-first-name");
  extra_metadata[0].value = grpc_slice_from_static_string(
      hobbits[index % GPR_ARRAY_SIZE(hobbits)][0]);
  extra_metadata[1].key = grpc_slice_from_static_string("hobbit-second-name");
  extra_metadata[1].value = grpc_slice_from_static_string(
      hobbits[index % GPR_ARRAY_SIZE(hobbits)][1]);
  extra_metadata[2].key = grpc_slice_from_static_string("dragon");
  extra_metadata[2].value =
      grpc_slice_from_static_string(dragons[index % GPR_ARRAY_SIZE(dragons)]);

  gpr_timespec deadline = five_seconds_from_now();
  c = grpc_channel_create_call(f.client, nullptr, GRPC_PROPAGATE_DEFAULTS, f.cq,
                               grpc_slice_from_static_string("/foo"), nullptr,
                               deadline, nullptr);
  GPR_ASSERT(c);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = GPR_ARRAY_SIZE(extra_metadata);
  op->data.send_initial_metadata.metadata = extra_metadata;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), tag(1),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(101), 1);
  cq_verify(cqv);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_UNIMPLEMENTED;
  grpc_slice status_details = grpc_slice_from_static_string("xyz");
  op->data.send_status_from_server.status_details = &status_details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops), tag(102),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(102), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
  cq_verify(cqv);

  GPR_ASSERT(status == GRPC_STATUS_UNIMPLEMENTED);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "xyz"));
  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/foo"));
  GPR_ASSERT(was_cancelled == 0);

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  grpc_call_unref(c);
  grpc_call_unref(s);

  cq_verifier_destroy(cqv);
}

static void test_size(grpc_end2end_test_config config, int encode_size,
                      int decode_size) {
  size_t i;
  grpc_end2end_test_fixture f;
  grpc_arg server_arg;
  grpc_channel_args server_args;
  grpc_arg client_arg;
  grpc_channel_args client_args;

  server_arg.type = GRPC_ARG_INTEGER;
  server_arg.key = const_cast<char*>(GRPC_ARG_HTTP2_HPACK_TABLE_SIZE_DECODER);
  server_arg.value.integer = decode_size;
  server_args.num_args = 1;
  server_args.args = &server_arg;

  client_arg.type = GRPC_ARG_INTEGER;
  client_arg.key = const_cast<char*>(GRPC_ARG_HTTP2_HPACK_TABLE_SIZE_ENCODER);
  client_arg.value.integer = encode_size;
  client_args.num_args = 1;
  client_args.args = &client_arg;

  std::string name =
      absl::StrFormat("test_size:e=%d:d=%d", encode_size, decode_size);
  f = begin_test(config, name.c_str(),
                 encode_size != 4096 ? &client_args : nullptr,
                 decode_size != 4096 ? &server_args : nullptr);
  for (i = 0; i < 4 * GPR_ARRAY_SIZE(hobbits); i++) {
    simple_request_body(config, f, i);
  }
  end_test(&f);
  config.tear_down_data(&f);
}

void hpack_size(grpc_end2end_test_config config) {
  static const int interesting_sizes[] = {4096, 0,     100,
                                          1000, 32768, 4 * 1024 * 1024};
  size_t i, j;

  for (i = 0; i < GPR_ARRAY_SIZE(interesting_sizes); i++) {
    for (j = 0; j < GPR_ARRAY_SIZE(interesting_sizes); j++) {
      test_size(config, interesting_sizes[i], interesting_sizes[j]);
    }
  }
}

void hpack_size_pre_init(void) {}
