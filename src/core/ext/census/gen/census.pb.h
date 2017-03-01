/*
 *
 * Copyright 2016, gRPC authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
/* Automatically generated nanopb header */
/* Generated by nanopb-0.3.5-dev */

#ifndef GRPC_CORE_EXT_CENSUS_GEN_CENSUS_PB_H
#define GRPC_CORE_EXT_CENSUS_GEN_CENSUS_PB_H
#include "third_party/nanopb/pb.h"
#if PB_PROTO_HEADER_VERSION != 30
#error Regenerate this file with the current version of nanopb generator.
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Enum definitions */
typedef enum _google_census_Resource_BasicUnit {
    google_census_Resource_BasicUnit_UNKNOWN = 0,
    google_census_Resource_BasicUnit_BITS = 1,
    google_census_Resource_BasicUnit_BYTES = 2,
    google_census_Resource_BasicUnit_SECS = 3,
    google_census_Resource_BasicUnit_CORES = 4,
    google_census_Resource_BasicUnit_MAX_UNITS = 5
} google_census_Resource_BasicUnit;

typedef enum _google_census_AggregationDescriptor_AggregationType {
    google_census_AggregationDescriptor_AggregationType_UNKNOWN = 0,
    google_census_AggregationDescriptor_AggregationType_COUNT = 1,
    google_census_AggregationDescriptor_AggregationType_DISTRIBUTION = 2,
    google_census_AggregationDescriptor_AggregationType_INTERVAL = 3
} google_census_AggregationDescriptor_AggregationType;

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
    bool has_type;
    google_census_AggregationDescriptor_AggregationType type;
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

typedef struct _google_census_Duration {
    bool has_seconds;
    int64_t seconds;
    bool has_nanos;
    int32_t nanos;
} google_census_Duration;

typedef struct _google_census_Resource_MeasurementUnit {
    bool has_prefix;
    int32_t prefix;
    pb_callback_t numerator;
    pb_callback_t denominator;
} google_census_Resource_MeasurementUnit;

typedef struct _google_census_Tag {
    bool has_key;
    char key[255];
    bool has_value;
    char value[255];
} google_census_Tag;

typedef struct _google_census_Timestamp {
    bool has_seconds;
    int64_t seconds;
    bool has_nanos;
    int32_t nanos;
} google_census_Timestamp;

typedef struct _google_census_Distribution {
    bool has_count;
    int64_t count;
    bool has_mean;
    double mean;
    bool has_range;
    google_census_Distribution_Range range;
    pb_callback_t bucket_count;
} google_census_Distribution;

typedef struct _google_census_IntervalStats_Window {
    bool has_window_size;
    google_census_Duration window_size;
    bool has_count;
    int64_t count;
    bool has_mean;
    double mean;
} google_census_IntervalStats_Window;

typedef struct _google_census_Metric {
    pb_callback_t view_name;
    pb_callback_t aggregation;
    bool has_start;
    google_census_Timestamp start;
    bool has_end;
    google_census_Timestamp end;
} google_census_Metric;

typedef struct _google_census_Resource {
    pb_callback_t name;
    pb_callback_t description;
    bool has_unit;
    google_census_Resource_MeasurementUnit unit;
} google_census_Resource;

typedef struct _google_census_View {
    pb_callback_t name;
    pb_callback_t description;
    pb_callback_t resource_name;
    bool has_aggregation;
    google_census_AggregationDescriptor aggregation;
    pb_callback_t tag_key;
} google_census_View;

typedef struct _google_census_Aggregation {
    pb_callback_t name;
    pb_callback_t description;
    pb_size_t which_data;
    union {
        uint64_t count;
        google_census_IntervalStats interval_stats;
    } data;
    pb_callback_t tag;
} google_census_Aggregation;

/* Default values for struct fields */

/* Initializer values for message structs */
#define google_census_Duration_init_default      {false, 0, false, 0}
#define google_census_Timestamp_init_default     {false, 0, false, 0}
#define google_census_Resource_init_default      {{{NULL}, NULL}, {{NULL}, NULL}, false, google_census_Resource_MeasurementUnit_init_default}
#define google_census_Resource_MeasurementUnit_init_default {false, 0, {{NULL}, NULL}, {{NULL}, NULL}}
#define google_census_AggregationDescriptor_init_default {false, (google_census_AggregationDescriptor_AggregationType)0, 0, {google_census_AggregationDescriptor_BucketBoundaries_init_default}}
#define google_census_AggregationDescriptor_BucketBoundaries_init_default {{{NULL}, NULL}}
#define google_census_AggregationDescriptor_IntervalBoundaries_init_default {{{NULL}, NULL}}
#define google_census_Distribution_init_default  {false, 0, false, 0, false, google_census_Distribution_Range_init_default, {{NULL}, NULL}}
#define google_census_Distribution_Range_init_default {false, 0, false, 0}
#define google_census_IntervalStats_init_default {{{NULL}, NULL}}
#define google_census_IntervalStats_Window_init_default {false, google_census_Duration_init_default, false, 0, false, 0}
#define google_census_Tag_init_default           {false, "", false, ""}
#define google_census_View_init_default          {{{NULL}, NULL}, {{NULL}, NULL}, {{NULL}, NULL}, false, google_census_AggregationDescriptor_init_default, {{NULL}, NULL}}
#define google_census_Aggregation_init_default   {{{NULL}, NULL}, {{NULL}, NULL}, 0, {0}, {{NULL}, NULL}}
#define google_census_Metric_init_default        {{{NULL}, NULL}, {{NULL}, NULL}, false, google_census_Timestamp_init_default, false, google_census_Timestamp_init_default}
#define google_census_Duration_init_zero         {false, 0, false, 0}
#define google_census_Timestamp_init_zero        {false, 0, false, 0}
#define google_census_Resource_init_zero         {{{NULL}, NULL}, {{NULL}, NULL}, false, google_census_Resource_MeasurementUnit_init_zero}
#define google_census_Resource_MeasurementUnit_init_zero {false, 0, {{NULL}, NULL}, {{NULL}, NULL}}
#define google_census_AggregationDescriptor_init_zero {false, (google_census_AggregationDescriptor_AggregationType)0, 0, {google_census_AggregationDescriptor_BucketBoundaries_init_zero}}
#define google_census_AggregationDescriptor_BucketBoundaries_init_zero {{{NULL}, NULL}}
#define google_census_AggregationDescriptor_IntervalBoundaries_init_zero {{{NULL}, NULL}}
#define google_census_Distribution_init_zero     {false, 0, false, 0, false, google_census_Distribution_Range_init_zero, {{NULL}, NULL}}
#define google_census_Distribution_Range_init_zero {false, 0, false, 0}
#define google_census_IntervalStats_init_zero    {{{NULL}, NULL}}
#define google_census_IntervalStats_Window_init_zero {false, google_census_Duration_init_zero, false, 0, false, 0}
#define google_census_Tag_init_zero              {false, "", false, ""}
#define google_census_View_init_zero             {{{NULL}, NULL}, {{NULL}, NULL}, {{NULL}, NULL}, false, google_census_AggregationDescriptor_init_zero, {{NULL}, NULL}}
#define google_census_Aggregation_init_zero      {{{NULL}, NULL}, {{NULL}, NULL}, 0, {0}, {{NULL}, NULL}}
#define google_census_Metric_init_zero           {{{NULL}, NULL}, {{NULL}, NULL}, false, google_census_Timestamp_init_zero, false, google_census_Timestamp_init_zero}

/* Field tags (for use in manual encoding/decoding) */
#define google_census_AggregationDescriptor_BucketBoundaries_bounds_tag 1
#define google_census_AggregationDescriptor_IntervalBoundaries_window_size_tag 1
#define google_census_IntervalStats_window_tag   1
#define google_census_AggregationDescriptor_bucket_boundaries_tag 2

#define google_census_AggregationDescriptor_interval_boundaries_tag 3
#define google_census_AggregationDescriptor_type_tag 1
#define google_census_Distribution_Range_min_tag 1
#define google_census_Distribution_Range_max_tag 2
#define google_census_Duration_seconds_tag       1
#define google_census_Duration_nanos_tag         2
#define google_census_Resource_MeasurementUnit_prefix_tag 1
#define google_census_Resource_MeasurementUnit_numerator_tag 2
#define google_census_Resource_MeasurementUnit_denominator_tag 3
#define google_census_Tag_key_tag                1
#define google_census_Tag_value_tag              2
#define google_census_Timestamp_seconds_tag      1
#define google_census_Timestamp_nanos_tag        2
#define google_census_Distribution_count_tag     1
#define google_census_Distribution_mean_tag      2
#define google_census_Distribution_range_tag     3
#define google_census_Distribution_bucket_count_tag 4
#define google_census_IntervalStats_Window_window_size_tag 1
#define google_census_IntervalStats_Window_count_tag 2
#define google_census_IntervalStats_Window_mean_tag 3
#define google_census_Metric_view_name_tag       1
#define google_census_Metric_aggregation_tag     2
#define google_census_Metric_start_tag           3
#define google_census_Metric_end_tag             4
#define google_census_Resource_name_tag          1
#define google_census_Resource_description_tag   2
#define google_census_Resource_unit_tag          3
#define google_census_View_name_tag              1
#define google_census_View_description_tag       2
#define google_census_View_resource_name_tag     3
#define google_census_View_aggregation_tag       4
#define google_census_View_tag_key_tag           5
#define google_census_Aggregation_count_tag      3


#define google_census_Aggregation_interval_stats_tag 5
#define google_census_Aggregation_name_tag       1
#define google_census_Aggregation_description_tag 2
#define google_census_Aggregation_tag_tag        6

/* Struct field encoding specification for nanopb */
extern const pb_field_t google_census_Duration_fields[3];
extern const pb_field_t google_census_Timestamp_fields[3];
extern const pb_field_t google_census_Resource_fields[4];
extern const pb_field_t google_census_Resource_MeasurementUnit_fields[4];
extern const pb_field_t google_census_AggregationDescriptor_fields[4];
extern const pb_field_t google_census_AggregationDescriptor_BucketBoundaries_fields[2];
extern const pb_field_t google_census_AggregationDescriptor_IntervalBoundaries_fields[2];
extern const pb_field_t google_census_Distribution_fields[5];
extern const pb_field_t google_census_Distribution_Range_fields[3];
extern const pb_field_t google_census_IntervalStats_fields[2];
extern const pb_field_t google_census_IntervalStats_Window_fields[4];
extern const pb_field_t google_census_Tag_fields[3];
extern const pb_field_t google_census_View_fields[6];
extern const pb_field_t google_census_Aggregation_fields[7];
extern const pb_field_t google_census_Metric_fields[5];

/* Maximum encoded size of messages (where known) */
#define google_census_Duration_size              22
#define google_census_Timestamp_size             22
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

#endif /* GRPC_CORE_EXT_CENSUS_GEN_CENSUS_PB_H */
