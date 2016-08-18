/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "route_guide_client.h"
#include "route_guide.grpc.pbc.h"
#include "route_guide_db.h"

#include <pb_decode.h>
#include <pb_encode.h>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/**
 * Nanopb callbacks for string encoding/decoding.
 */

static bool write_string_from_arg(pb_ostream_t *stream, const pb_field_t *field,
                                  void *const *arg) {
  const char *str = *arg;
  if (!pb_encode_tag_for_field(stream, field)) return false;

  return pb_encode_string(stream, (uint8_t *)str, strlen(str));
}

/**
 * This callback function reads a string from Nanopb stream and copies it into
 * the callback args.
 * Users need to free the string after use.
 */
static bool read_string_store_in_arg(pb_istream_t *stream,
                                     const pb_field_t *field, void **arg) {
  size_t len = stream->bytes_left;
  char *str = malloc(len + 1);
  if (!pb_read(stream, str, len)) return false;
  str[len] = '\0';
  *arg = str;
  return true;
}

static const float kCoordFactor = 10000000.0;

bool get_one_feature(GRPC_channel *channel, routeguide_Point point,
                     routeguide_Feature *feature) {
  GRPC_client_context *context = GRPC_client_context_create(channel);
  feature->name.funcs.decode = read_string_store_in_arg;
  feature->name.arg = NULL;
  GRPC_status status =
      routeguide_RouteGuide_GetFeature(context, point, feature);
  if (!status.ok) {
    if (status.details_length > 0 && status.details != NULL) {
      printf("GetFeature rpc failed. Code = %d. Details: %s\n", status.code,
             status.details);
    } else {
      printf("GetFeature rpc failed. Code = %d.\n", status.code);
    }
    GRPC_client_context_destroy(&context);
    return false;
  }
  if (!feature->has_location) {
    printf("Server returns incomplete feature.\n");
    GRPC_client_context_destroy(&context);
    return false;
  }
  if (feature->name.arg == NULL || strcmp(feature->name.arg, "") == 0) {
    printf("Found no feature at %.6f, %.6f\n",
           feature->location.latitude / kCoordFactor,
           feature->location.longitude / kCoordFactor);
  } else {
    printf("Found feature called %s at %.6f, %.6f\n", (char *)feature->name.arg,
           feature->location.latitude / kCoordFactor,
           feature->location.longitude / kCoordFactor);
  }
  GRPC_client_context_destroy(&context);
  return true;
}

void get_feature(GRPC_channel *chan) {
  routeguide_Point point = {true, 409146138, true, -746188906};
  routeguide_Feature feature;
  get_one_feature(chan, point, &feature);

  /* free name string */
  if (feature.name.arg != NULL) {
    free(feature.name.arg);
    feature.name.arg = NULL;
  }

  point = (routeguide_Point){false, 0, false, 0};
  get_one_feature(chan, point, &feature);

  /* free name string */
  if (feature.name.arg != NULL) {
    free(feature.name.arg);
    feature.name.arg = NULL;
  }
}

void list_features(GRPC_channel *chan) {
  routeguide_Rectangle rect = {.has_lo = true,
                               .lo = {.has_latitude = true,
                                      .latitude = 400000000,
                                      .has_longitude = true,
                                      .longitude = -750000000},
                               .has_hi = true,
                               .hi = {.has_latitude = true,
                                      .latitude = 420000000,
                                      .has_longitude = true,
                                      .longitude = -730000000}};
  routeguide_Feature feature;
  feature.name.funcs.decode = read_string_store_in_arg;
  GRPC_client_context *context = GRPC_client_context_create(chan);

  printf("Looking for features between 40, -75 and 42, -73\n");

  GRPC_client_reader *reader =
      routeguide_RouteGuide_ListFeatures(context, rect);

  while (routeguide_RouteGuide_ListFeatures_Read(reader, &feature)) {
    char *name = "";
    if (feature.name.arg) name = feature.name.arg;
    printf("Found feature called %s at %.6f, %.6f\n", name,
           feature.location.latitude / kCoordFactor,
           feature.location.longitude / kCoordFactor);

    /* free name string */
    if (feature.name.arg != NULL) {
      free(feature.name.arg);
      feature.name.arg = NULL;
    }
  }

  GRPC_status status = routeguide_RouteGuide_ListFeatures_Terminate(reader);
  if (status.ok) {
    printf("ListFeatures rpc succeeded.\n");
  } else {
    printf("ListFeatures rpc failed.\n");
  }
  GRPC_client_context_destroy(&context);
}

void record_route(GRPC_channel *chan) {
  routeguide_Point point;
  routeguide_RouteSummary stats;
  GRPC_client_context *context = GRPC_client_context_create(chan);
  const int kPoints = 10;

  srand(time(NULL));

  GRPC_client_writer *writer =
      routeguide_RouteGuide_RecordRoute(context, &stats);

  int i;
  for (i = 0; i < kPoints; i++) {
    const route_feature db_feature =
        route_guide_database[rand() % num_route_features_in_database];
    const routeguide_Feature f = {
        .name = {.arg = (void *)db_feature.name,
                 .funcs.encode = write_string_from_arg},
        .has_location = true,
        .location = {.has_latitude = true,
                     .latitude = db_feature.location.latitude,
                     .has_longitude = true,
                     .longitude = db_feature.location.longitude}};
    printf("Visiting point %.6f, %.6f\n", f.location.latitude / kCoordFactor,
           f.location.longitude / kCoordFactor);
    if (!routeguide_RouteGuide_RecordRoute_Write(writer, f.location)) {
      // Broken stream.
      break;
    }
    usleep((500 + rand() % 1000) * 1000);
  }
  GRPC_status status = routeguide_RouteGuide_RecordRoute_Terminate(writer);
  if (status.ok) {
    printf(
        "Finished trip with %d points\nPassed %d features\nTravelled %d "
        "meters\nIt took %d seconds\n",
        stats.point_count, stats.feature_count, stats.distance,
        stats.elapsed_time);
  } else {
    printf("RecordRoute rpc failed.\n");
  }
}

void route_chat_thread(GRPC_client_reader_writer *reader_writer) {
  routeguide_RouteNote notes[] = {
      {.has_location = true,
       .location = {.has_latitude = false,
                    .latitude = 0,
                    .has_longitude = false,
                    .longitude = 0},
       .message = {.arg = (void *)"First message",
                   .funcs.encode = write_string_from_arg}},
      {.has_location = true,
       .location = {.has_latitude = false,
                    .latitude = 0,
                    .has_longitude = true,
                    .longitude = 1},
       .message = {.arg = (void *)"Second message",
                   .funcs.encode = write_string_from_arg}},
      {.has_location = true,
       .location = {.has_latitude = true,
                    .latitude = 1,
                    .has_longitude = false,
                    .longitude = 0},
       .message = {.arg = (void *)"Third message",
                   .funcs.encode = write_string_from_arg}},
      {.has_location = true,
       .location = {.has_latitude = false,
                    .latitude = 0,
                    .has_longitude = false,
                    .longitude = 0},
       .message = {.arg = (void *)"Fourth message",
                   .funcs.encode = write_string_from_arg}}};

  int i;
  for (i = 0; i < 4; i++) {
    printf("Sending message %s at %d, %d\n", (char *)notes[i].message.arg,
           notes[i].location.latitude, notes[i].location.longitude);
    routeguide_RouteGuide_RouteChat_Write(reader_writer, notes[i]);
  }
  routeguide_RouteGuide_RouteChat_Writes_Done(reader_writer);
}

void route_chat(GRPC_channel *chan) {
  GRPC_client_context *context = GRPC_client_context_create(chan);

  GRPC_client_reader_writer *reader_writer =
      routeguide_RouteGuide_RouteChat(context);

  pthread_t tid;
  pthread_create(&tid, NULL, route_chat_thread, reader_writer);

  routeguide_RouteNote server_note;
  server_note.message.funcs.decode = read_string_store_in_arg;
  while (routeguide_RouteGuide_RouteChat_Read(reader_writer, &server_note)) {
    printf("Got message %s at %d, %d\n", (char *)server_note.message.arg,
           server_note.location.latitude, server_note.location.longitude);
  }
  pthread_join(tid, NULL);
  GRPC_status status = routeguide_RouteGuide_RouteChat_Terminate(reader_writer);
  if (!status.ok) {
    printf("RouteChat rpc failed.\n");
  }
}

int main(int argc, char **argv) {
  GRPC_channel *chan = GRPC_channel_create("0.0.0.0:50051");

  printf("-------------- GetFeature --------------\n");
  get_feature(chan);
  printf("-------------- ListFeatures --------------\n");
  list_features(chan);
  printf("-------------- RecordRoute --------------\n");
  record_route(chan);
  printf("-------------- RouteChat --------------\n");
  route_chat(chan);

  GRPC_channel_destroy(&chan);
  return 0;
}
