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
/* Automatically generated nanopb constant definitions */
/* Generated by nanopb-0.3.5-dev */

#include "src/core/ext/census/gen/census.pb.h"

#if PB_PROTO_HEADER_VERSION != 30
#error Regenerate this file with the current version of nanopb generator.
#endif



const pb_field_t google_census_Duration_fields[3] = {
    PB_FIELD(  1, INT64   , OPTIONAL, STATIC  , FIRST, google_census_Duration, seconds, seconds, 0),
    PB_FIELD(  2, INT32   , OPTIONAL, STATIC  , OTHER, google_census_Duration, nanos, seconds, 0),
    PB_LAST_FIELD
};

const pb_field_t google_census_Timestamp_fields[3] = {
    PB_FIELD(  1, INT64   , OPTIONAL, STATIC  , FIRST, google_census_Timestamp, seconds, seconds, 0),
    PB_FIELD(  2, INT32   , OPTIONAL, STATIC  , OTHER, google_census_Timestamp, nanos, seconds, 0),
    PB_LAST_FIELD
};

const pb_field_t google_census_Resource_fields[4] = {
    PB_FIELD(  1, STRING  , OPTIONAL, CALLBACK, FIRST, google_census_Resource, name, name, 0),
    PB_FIELD(  2, STRING  , OPTIONAL, CALLBACK, OTHER, google_census_Resource, description, name, 0),
    PB_FIELD(  3, MESSAGE , OPTIONAL, STATIC  , OTHER, google_census_Resource, unit, description, &google_census_Resource_MeasurementUnit_fields),
    PB_LAST_FIELD
};

const pb_field_t google_census_Resource_MeasurementUnit_fields[4] = {
    PB_FIELD(  1, INT32   , OPTIONAL, STATIC  , FIRST, google_census_Resource_MeasurementUnit, prefix, prefix, 0),
    PB_FIELD(  2, UENUM   , REPEATED, CALLBACK, OTHER, google_census_Resource_MeasurementUnit, numerator, prefix, 0),
    PB_FIELD(  3, UENUM   , REPEATED, CALLBACK, OTHER, google_census_Resource_MeasurementUnit, denominator, numerator, 0),
    PB_LAST_FIELD
};

const pb_field_t google_census_AggregationDescriptor_fields[4] = {
    PB_FIELD(  1, UENUM   , OPTIONAL, STATIC  , FIRST, google_census_AggregationDescriptor, type, type, 0),
    PB_ONEOF_FIELD(options,   2, MESSAGE , ONEOF, STATIC  , OTHER, google_census_AggregationDescriptor, bucket_boundaries, type, &google_census_AggregationDescriptor_BucketBoundaries_fields),
    PB_ONEOF_FIELD(options,   3, MESSAGE , ONEOF, STATIC  , OTHER, google_census_AggregationDescriptor, interval_boundaries, type, &google_census_AggregationDescriptor_IntervalBoundaries_fields),
    PB_LAST_FIELD
};

const pb_field_t google_census_AggregationDescriptor_BucketBoundaries_fields[2] = {
    PB_FIELD(  1, DOUBLE  , REPEATED, CALLBACK, FIRST, google_census_AggregationDescriptor_BucketBoundaries, bounds, bounds, 0),
    PB_LAST_FIELD
};

const pb_field_t google_census_AggregationDescriptor_IntervalBoundaries_fields[2] = {
    PB_FIELD(  1, DOUBLE  , REPEATED, CALLBACK, FIRST, google_census_AggregationDescriptor_IntervalBoundaries, window_size, window_size, 0),
    PB_LAST_FIELD
};

const pb_field_t google_census_Distribution_fields[5] = {
    PB_FIELD(  1, INT64   , OPTIONAL, STATIC  , FIRST, google_census_Distribution, count, count, 0),
    PB_FIELD(  2, DOUBLE  , OPTIONAL, STATIC  , OTHER, google_census_Distribution, mean, count, 0),
    PB_FIELD(  3, MESSAGE , OPTIONAL, STATIC  , OTHER, google_census_Distribution, range, mean, &google_census_Distribution_Range_fields),
    PB_FIELD(  4, INT64   , REPEATED, CALLBACK, OTHER, google_census_Distribution, bucket_count, range, 0),
    PB_LAST_FIELD
};

const pb_field_t google_census_Distribution_Range_fields[3] = {
    PB_FIELD(  1, DOUBLE  , OPTIONAL, STATIC  , FIRST, google_census_Distribution_Range, min, min, 0),
    PB_FIELD(  2, DOUBLE  , OPTIONAL, STATIC  , OTHER, google_census_Distribution_Range, max, min, 0),
    PB_LAST_FIELD
};

const pb_field_t google_census_IntervalStats_fields[2] = {
    PB_FIELD(  1, MESSAGE , REPEATED, CALLBACK, FIRST, google_census_IntervalStats, window, window, &google_census_IntervalStats_Window_fields),
    PB_LAST_FIELD
};

const pb_field_t google_census_IntervalStats_Window_fields[4] = {
    PB_FIELD(  1, MESSAGE , OPTIONAL, STATIC  , FIRST, google_census_IntervalStats_Window, window_size, window_size, &google_census_Duration_fields),
    PB_FIELD(  2, INT64   , OPTIONAL, STATIC  , OTHER, google_census_IntervalStats_Window, count, window_size, 0),
    PB_FIELD(  3, DOUBLE  , OPTIONAL, STATIC  , OTHER, google_census_IntervalStats_Window, mean, count, 0),
    PB_LAST_FIELD
};

const pb_field_t google_census_Tag_fields[3] = {
    PB_FIELD(  1, STRING  , OPTIONAL, STATIC  , FIRST, google_census_Tag, key, key, 0),
    PB_FIELD(  2, STRING  , OPTIONAL, STATIC  , OTHER, google_census_Tag, value, key, 0),
    PB_LAST_FIELD
};

const pb_field_t google_census_View_fields[6] = {
    PB_FIELD(  1, STRING  , OPTIONAL, CALLBACK, FIRST, google_census_View, name, name, 0),
    PB_FIELD(  2, STRING  , OPTIONAL, CALLBACK, OTHER, google_census_View, description, name, 0),
    PB_FIELD(  3, STRING  , OPTIONAL, CALLBACK, OTHER, google_census_View, resource_name, description, 0),
    PB_FIELD(  4, MESSAGE , OPTIONAL, STATIC  , OTHER, google_census_View, aggregation, resource_name, &google_census_AggregationDescriptor_fields),
    PB_FIELD(  5, STRING  , REPEATED, CALLBACK, OTHER, google_census_View, tag_key, aggregation, 0),
    PB_LAST_FIELD
};

const pb_field_t google_census_Aggregation_fields[7] = {
    PB_FIELD(  1, STRING  , OPTIONAL, CALLBACK, FIRST, google_census_Aggregation, name, name, 0),
    PB_FIELD(  2, STRING  , OPTIONAL, CALLBACK, OTHER, google_census_Aggregation, description, name, 0),
    PB_ONEOF_FIELD(data,   3, UINT64  , ONEOF, STATIC  , OTHER, google_census_Aggregation, count, description, 0),
    PB_ONEOF_FIELD(data,   5, MESSAGE , ONEOF, STATIC  , OTHER, google_census_Aggregation, interval_stats, description, &google_census_IntervalStats_fields),
    PB_FIELD(  6, MESSAGE , REPEATED, CALLBACK, OTHER, google_census_Aggregation, tag, data.interval_stats, &google_census_Tag_fields),
    PB_LAST_FIELD
};

const pb_field_t google_census_Metric_fields[5] = {
    PB_FIELD(  1, STRING  , OPTIONAL, CALLBACK, FIRST, google_census_Metric, view_name, view_name, 0),
    PB_FIELD(  2, MESSAGE , REPEATED, CALLBACK, OTHER, google_census_Metric, aggregation, view_name, &google_census_Aggregation_fields),
    PB_FIELD(  3, MESSAGE , OPTIONAL, STATIC  , OTHER, google_census_Metric, start, aggregation, &google_census_Timestamp_fields),
    PB_FIELD(  4, MESSAGE , OPTIONAL, STATIC  , OTHER, google_census_Metric, end, start, &google_census_Timestamp_fields),
    PB_LAST_FIELD
};


/* Check that field information fits in pb_field_t */
#if !defined(PB_FIELD_32BIT)
/* If you get an error here, it means that you need to define PB_FIELD_32BIT
 * compile-time option. You can do that in pb.h or on compiler command line.
 * 
 * The reason you need to do this is that some of your messages contain tag
 * numbers or field sizes that are larger than what can fit in 8 or 16 bit
 * field descriptors.
 */
#endif

#if !defined(PB_FIELD_16BIT) && !defined(PB_FIELD_32BIT)
/* If you get an error here, it means that you need to define PB_FIELD_16BIT
 * compile-time option. You can do that in pb.h or on compiler command line.
 * 
 * The reason you need to do this is that some of your messages contain tag
 * numbers or field sizes that are larger than what can fit in the default
 * 8 bit descriptors.
 */
#endif


/* On some platforms (such as AVR), double is really float.
 * These are not directly supported by nanopb, but see example_avr_double.
 * To get rid of this error, remove any double fields from your .proto.
 */
PB_STATIC_ASSERT(sizeof(double) == 8, DOUBLE_MUST_BE_8_BYTES)

