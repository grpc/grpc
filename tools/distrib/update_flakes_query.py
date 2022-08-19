# Copyright 2022 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

QUERY = """
#standardSQL

-- See https://console.cloud.google.com/bigquery?sq=830293263384:5a8549832dfb48d9b2c04312a4ae3181 for the original query

WITH

  runs AS (
  SELECT
    RTRIM(LTRIM(REGEXP_REPLACE(test_target, r'(@poller=.+)', ''))) AS test_binary,
    REGEXP_EXTRACT(test_target, r'poller=(\w+)') AS poll_strategy,
    job_name,
    test_target,
    test_class_name,
    CASE
      # in case of timeout / retry / segfault the "test_case" fields will contain weird stuff
      # e.g. "test_shard2_run0_attempt0" or "shard_2/20"
      # when aggregating, we want to display all of these as a single category of problems
      WHEN test_case like 'test_shard_%_run%_attempt%' THEN 'CANNOT_DETERMINE'
      WHEN test_case like '%shard_%/%' THEN 'CANNOT_DETERMINE'
      # when test_case looks suspiciously like test_target
      # its value is probably meaningless and it means that the entire target has failed
      # e.g. test_target="//test/cpp/client:destroy_grpclb_channel_with_active_connect_stress_test" and test_case="test/cpp/client/destroy_grpclb_channel_with_active_connect_stress_test.exe"                         
      WHEN STRPOS(test_case, REPLACE(SUBSTR(test_target, 3), ":", "/")) != 0 THEN 'CANNOT_DETERMINE'
      ELSE test_case
    END AS test_case,
    result,
    build_id,
    timestamp
  FROM
    `grpc-testing.jenkins_test_results.rbe_test_results`
  WHERE
    DATETIME_DIFF(CURRENT_DATETIME(),
      dateTIME(timestamp),
      HOUR) < {lookback_hours}
    ),

  results_counts_per_build AS (
  SELECT
    test_binary,
    #test_target, # aggregate data over all pollers
    test_class_name,
    test_case,
    SUM(SAFE_CAST(result != 'PASSED'
        AND result != 'SKIPPED' AS INT64)) AS runs_failed,
    SUM(SAFE_CAST(result != 'SKIPPED' AS INT64)) AS runs_total,
    job_name,
    build_id
  FROM
    runs
  GROUP BY
    test_binary,
    test_class_name,
    test_case,
    job_name,
    build_id),
    
  builds_with_missing_cannot_determine_testcase_entry AS (
    SELECT 
      test_binary,
      job_name,
      build_id,
    FROM
      results_counts_per_build
    GROUP BY
      test_binary,
      job_name,
      build_id
    HAVING COUNTIF(test_case = 'CANNOT_DETERMINE') = 0
  ),
  
  # for each test target and build, generate a fake entry with "CANNOT_DETERMINE" test_case
  # if not already present.
  # this is because in many builds, there will be no "CANNOT_DETERMINE" entry
  # and we want to avoid skewing the statistics
  results_counts_per_build_with_fake_cannot_determine_test_case_entries AS (
    (SELECT * FROM results_counts_per_build)
    UNION ALL
    (SELECT
      test_binary,
      '' AS test_class_name,  # when test_case is 'CANNOT_DETERMINE', test class is empty string
      'CANNOT_DETERMINE' AS test_case,  # see table "runs"
      0 AS runs_failed,
      1 AS runs_total,
      job_name,
      build_id
    FROM
      builds_with_missing_cannot_determine_testcase_entry)
  ),
    
  results_counts AS (
  SELECT
    test_binary,
    test_class_name,
    test_case,
    job_name,
    SUM(runs_failed) AS runs_failed,
    SUM(runs_total) AS runs_total,
    SUM(SAFE_CAST(runs_failed > 0 AS INT64)) AS builds_failed,
    COUNT(build_id) AS builds_total,
    STRING_AGG(CASE
        WHEN runs_failed > 0 THEN 'X'
        ELSE '_' END, ''
    ORDER BY
      build_id ASC) AS build_failure_pattern,
    FORMAT("%T", ARRAY_AGG(build_id
      ORDER BY
        build_id ASC)) AS builds
  FROM
    #results_counts_per_build
    results_counts_per_build_with_fake_cannot_determine_test_case_entries
  GROUP BY
    test_binary,
    test_class_name,
    test_case,
    job_name
  HAVING
    runs_failed > 0)

SELECT
  ROUND(100*builds_failed / builds_total, 2) AS pct_builds_failed,
  ROUND(100*runs_failed / runs_total, 2) AS pct_runs_failed,
  test_binary,
  test_class_name,
  test_case,
  job_name,
  build_failure_pattern
           
FROM
  results_counts
ORDER BY
  pct_builds_failed DESC
"""
