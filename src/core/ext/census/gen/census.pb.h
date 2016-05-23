/*
 *
 * Copyright 2016, Google Inc.
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
/* Automatically generated nanopb header */
/* Generated by nanopb-0.3.5-dev */

#ifndef PB_CENSUS_PB_H_INCLUDED
#define PB_CENSUS_PB_H_INCLUDED
#include "third_party/nanopb/pb.h"
#include "google/protobuf/duration.pb.h"

#include "google/protobuf/timestamp.pb.h"

#if PB_PROTO_HEADER_VERSION != 30
#error Regenerate this file with the current version of nanopb generator.
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Enum definitions */
typedef enum _google_census_Metric_BasicUnit_Measure {
    google_census_Metric_BasicUnit_Measure_UNKNOWN = 0,
    google_census_Metric_BasicUnit_Measure_BITS = 1,
    google_census_Metric_BasicUnit_Measure_BYTES = 2,
    google_census_Metric_BasicUnit_Measure_SECS = 3,
    google_census_Metric_BasicUnit_Measure_CORES = 4,
    google_census_Metric_BasicUnit_Measure_MAX_UNITS = 5
} google_census_Metric_BasicUnit_Measure;

/* Struct definitions */
typedef struct _google_census_AggregationDescriptor_BucketBoundaries {
    pb_callback_t bounds;
} google_census_AggregationDescriptor_BucketBoundaries;

typedef struct _google_census_AggregationDescriptor_IntervalBoundaries {
    pb_callback_t window_size;
} google_census_AggregationDescriptor_IntervalBoundaries;

typedef struct _google_census_IntervalStats {
    pb_callback_t window;
} google_census_IntervalStats;

typedef struct _google_census_AggregationDescriptor {
    pb_size_t which_options;
    union {
        google_census_AggregationDescriptor_BucketBoundaries bucket_boundaries;
        google_census_AggregationDescriptor_IntervalBoundaries interval_boundaries;
    } options;
} google_census_AggregationDescriptor;

typedef struct _google_census_Distribution_Range {
    bool has_min;
    double min;
    bool has_max;
    double max;
} google_census_Distribution_Range;

typedef struct _google_census_IntervalStats_Window {
    bool has_window_size;
    google_protobuf_Duration window_size;
    bool has_count;
    int64_t count;
    bool has_mean;
    double mean;
} google_census_IntervalStats_Window;

typedef struct _google_census_Metric_BasicUnit {
    bool has_type;
    google_census_Metric_BasicUnit_Measure type;
} google_census_Metric_BasicUnit;

typedef struct _google_census_Metric_MeasurementUnit {
    bool has_prefix;
    int32_t prefix;
    pb_callback_t numerator;
    pb_callback_t denominator;
} google_census_Metric_MeasurementUnit;

typedef struct _google_census_Tag {
    bool has_key;
    char key[255];
    bool has_value;
    char value[255];
} google_census_Tag;

typedef struct _google_census_ViewAggregations {
    pb_callback_t aggregation;
    bool has_start;
    google_protobuf_Timestamp start;
    bool has_end;
    google_protobuf_Timestamp end;
} google_census_ViewAggregations;

typedef struct _google_census_Distribution {
    bool has_count;
    int64_t count;
    bool has_mean;
    double mean;
    bool has_sum_of_squared_deviation;
    double sum_of_squared_deviation;
    bool has_range;
    google_census_Distribution_Range range;
    pb_callback_t bucket_count;
} google_census_Distribution;

typedef struct _google_census_Metric {
    pb_callback_t name;
    pb_callback_t description;
    bool has_unit;
    google_census_Metric_MeasurementUnit unit;
    bool has_id;
    int32_t id;
} google_census_Metric;

typedef struct _google_census_View {
    pb_callback_t name;
    pb_callback_t description;
    bool has_metric_id;
    int32_t metric_id;
    bool has_aggregation;
    google_census_AggregationDescriptor aggregation;
    pb_callback_t tag_key;
} google_census_View;

typedef struct _google_census_Aggregation {
    pb_callback_t name;
    pb_callback_t description;
    pb_size_t which_data;
    union {
        google_census_Distribution distribution;
        google_census_IntervalStats interval_stats;
    } data;
    pb_callback_t tag;
} google_census_Aggregation;

/* Default values for struct fields */

/* Initializer values for message structs */
#define google_census_Metric_init_default        {{{NULL}, NULL}, {{NULL}, NULL}, false, google_census_Metric_MeasurementUnit_init_default, false, 0}
#define google_census_Metric_BasicUnit_init_default {false, (google_census_Metric_BasicUnit_Measure)0}
#define google_census_Metric_MeasurementUnit_init_default {false, 0, {{NULL}, NULL}, {{NULL}, NULL}}
#define google_census_AggregationDescriptor_init_default {0, {google_census_AggregationDescriptor_BucketBoundaries_init_default}}
#define google_census_AggregationDescriptor_BucketBoundaries_init_default {{{NULL}, NULL}}
#define google_census_AggregationDescriptor_IntervalBoundaries_init_default {{{NULL}, NULL}}
#define google_census_Distribution_init_default  {false, 0, false, 0, false, 0, false, google_census_Distribution_Range_init_default, {{NULL}, NULL}}
#define google_census_Distribution_Range_init_default {false, 0, false, 0}
#define google_census_IntervalStats_init_default {{{NULL}, NULL}}
#define google_census_IntervalStats_Window_init_default {false, google_protobuf_Duration_init_default, false, 0, false, 0}
#define google_census_Tag_init_default           {false, "", false, ""}
#define google_census_View_init_default          {{{NULL}, NULL}, {{NULL}, NULL}, false, 0, false, google_census_AggregationDescriptor_init_default, {{NULL}, NULL}}
#define google_census_Aggregation_init_default   {{{NULL}, NULL}, {{NULL}, NULL}, 0, {google_census_Distribution_init_default}, {{NULL}, NULL}}
#define google_census_ViewAggregations_init_default {{{NULL}, NULL}, false, google_protobuf_Timestamp_init_default, false, google_protobuf_Timestamp_init_default}
#define google_census_Metric_init_zero           {{{NULL}, NULL}, {{NULL}, NULL}, false, google_census_Metric_MeasurementUnit_init_zero, false, 0}
#define google_census_Metric_BasicUnit_init_zero {false, (google_census_Metric_BasicUnit_Measure)0}
#define google_census_Metric_MeasurementUnit_init_zero {false, 0, {{NULL}, NULL}, {{NULL}, NULL}}
#define google_census_AggregationDescriptor_init_zero {0, {google_census_AggregationDescriptor_BucketBoundaries_init_zero}}
#define google_census_AggregationDescriptor_BucketBoundaries_init_zero {{{NULL}, NULL}}
#define google_census_AggregationDescriptor_IntervalBoundaries_init_zero {{{NULL}, NULL}}
#define google_census_Distribution_init_zero     {false, 0, false, 0, false, 0, false, google_census_Distribution_Range_init_zero, {{NULL}, NULL}}
#define google_census_Distribution_Range_init_zero {false, 0, false, 0}
#define google_census_IntervalStats_init_zero    {{{NULL}, NULL}}
#define google_census_IntervalStats_Window_init_zero {false, google_protobuf_Duration_init_zero, false, 0, false, 0}
#define google_census_Tag_init_zero              {false, "", false, ""}
#define google_census_View_init_zero             {{{NULL}, NULL}, {{NULL}, NULL}, false, 0, false, google_census_AggregationDescriptor_init_zero, {{NULL}, NULL}}
#define google_census_Aggregation_init_zero      {{{NULL}, NULL}, {{NULL}, NULL}, 0, {google_census_Distribution_init_zero}, {{NULL}, NULL}}
#define google_census_ViewAggregations_init_zero {{{NULL}, NULL}, false, google_protobuf_Timestamp_init_zero, false, google_protobuf_Timestamp_init_zero}

/* Field tags (for use in manual encoding/decoding) */
#define google_census_AggregationDescriptor_BucketBoundaries_bounds_tag 1
#define google_census_AggregationDescriptor_IntervalBoundaries_window_size_tag 1
#define google_census_IntervalStats_window_tag   1
#define google_census_AggregationDescriptor_bucket_boundaries_tag 1

#define google_census_AggregationDescriptor_interval_boundaries_tag 2
#define google_census_Distribution_Range_min_tag 1
#define google_census_Distribution_Range_max_tag 2
#define google_census_IntervalStats_Window_window_size_tag 1
#define google_census_IntervalStats_Window_count_tag 2
#define google_census_IntervalStats_Window_mean_tag 3
#define google_census_Metric_BasicUnit_type_tag  1
#define google_census_Metric_MeasurementUnit_prefix_tag 1
#define google_census_Metric_MeasurementUnit_numerator_tag 2
#define google_census_Metric_MeasurementUnit_denominator_tag 3
#define google_census_Tag_key_tag                1
#define google_census_Tag_value_tag              2
#define google_census_ViewAggregations_aggregation_tag 1
#define google_census_ViewAggregations_start_tag 2
#define google_census_ViewAggregations_end_tag   3
#define google_census_Distribution_count_tag     1
#define google_census_Distribution_mean_tag      2
#define google_census_Distribution_sum_of_squared_deviation_tag 3
#define google_census_Distribution_range_tag     4
#define google_census_Distribution_bucket_count_tag 5
#define google_census_Metric_name_tag            1
#define google_census_Metric_description_tag     2
#define google_census_Metric_unit_tag            3
#define google_census_Metric_id_tag              4
#define google_census_View_name_tag              1
#define google_census_View_description_tag       2
#define google_census_View_metric_id_tag         3
#define google_census_View_aggregation_tag       4
#define google_census_View_tag_key_tag           5
#define google_census_Aggregation_distribution_tag 3

#define google_census_Aggregation_interval_stats_tag 4
#define google_census_Aggregation_name_tag       1
#define google_census_Aggregation_description_tag 2
#define google_census_Aggregation_tag_tag        5

/* Struct field encoding specification for nanopb */
extern const pb_field_t google_census_Metric_fields[5];
extern const pb_field_t google_census_Metric_BasicUnit_fields[2];
extern const pb_field_t google_census_Metric_MeasurementUnit_fields[4];
extern const pb_field_t google_census_AggregationDescriptor_fields[3];
extern const pb_field_t google_census_AggregationDescriptor_BucketBoundaries_fields[2];
extern const pb_field_t google_census_AggregationDescriptor_IntervalBoundaries_fields[2];
extern const pb_field_t google_census_Distribution_fields[6];
extern const pb_field_t google_census_Distribution_Range_fields[3];
extern const pb_field_t google_census_IntervalStats_fields[2];
extern const pb_field_t google_census_IntervalStats_Window_fields[4];
extern const pb_field_t google_census_Tag_fields[3];
extern const pb_field_t google_census_View_fields[6];
extern const pb_field_t google_census_Aggregation_fields[6];
extern const pb_field_t google_census_ViewAggregations_fields[4];

/* Maximum encoded size of messages (where known) */
#define google_census_Metric_BasicUnit_size      2
#define google_census_Distribution_Range_size    18
#define google_census_IntervalStats_Window_size  44
#define google_census_Tag_size                   516

/* Message IDs (where set with "msgid" option) */
#ifdef PB_MSGID

#define CENSUS_MESSAGES \


#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
