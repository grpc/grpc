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

#ifndef GRPC_CORE_LIB_IOMGR_TIME_AVERAGED_STATS_H
#define GRPC_CORE_LIB_IOMGR_TIME_AVERAGED_STATS_H

/* This tracks a time-decaying weighted average.  It works by collecting
   batches of samples and then mixing their average into a time-decaying
   weighted mean.  It is designed for batch operations where we do many adds
   before updating the average. */

typedef struct {
  /* The initial average value.  This is the reported average until the first
     grpc_time_averaged_stats_update_average call.  If a positive regress_weight
     is used, we also regress towards this value on each update. */
  double init_avg;
  /* The sample weight of "init_avg" that is mixed in with each call to
     grpc_time_averaged_stats_update_average.  If the calls to
     grpc_time_averaged_stats_add_sample stop, this will cause the average to
     regress back to the mean.  This should be non-negative.  Set it to 0 to
     disable the bias.  A value of 1 has the effect of adding in 1 bonus sample
     with value init_avg to each sample period. */
  double regress_weight;
  /* This determines the rate of decay of the time-averaging from one period
     to the next by scaling the aggregate_total_weight of samples from prior
     periods when combining with the latest period.  It should be in the range
     [0,1].  A higher value adapts more slowly.  With a value of 0.5, if the
     batches each have k samples, the samples_in_avg_ will grow to 2 k, so the
     weighting of the time average will eventually be 1/3 new batch and 2/3
     old average. */
  double persistence_factor;

  /* The total value of samples since the last UpdateAverage(). */
  double batch_total_value;
  /* The number of samples since the last UpdateAverage(). */
  double batch_num_samples;
  /* The time-decayed sum of batch_num_samples_ over previous batches.  This is
     the "weight" of the old aggregate_weighted_avg_ when updating the
     average. */
  double aggregate_total_weight;
  /* A time-decayed average of the (batch_total_value_ / batch_num_samples_),
     computed by decaying the samples_in_avg_ weight in the weighted average. */
  double aggregate_weighted_avg;
} grpc_time_averaged_stats;

/* See the comments on the members above for an explanation of init_avg,
   regress_weight, and persistence_factor. */
void grpc_time_averaged_stats_init(grpc_time_averaged_stats* stats,
                                   double init_avg, double regress_weight,
                                   double persistence_factor);
/* Add a sample to the current batch. */
void grpc_time_averaged_stats_add_sample(grpc_time_averaged_stats* stats,
                                         double value);
/* Complete a batch and compute the new estimate of the average sample
   value. */
double grpc_time_averaged_stats_update_average(grpc_time_averaged_stats* stats);

#endif /* GRPC_CORE_LIB_IOMGR_TIME_AVERAGED_STATS_H */
