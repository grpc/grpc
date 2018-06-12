#standardSQL
WITH calibration AS (
  SELECT
    RTRIM(LTRIM(REGEXP_REPLACE(filtered_test_name, r'(/\d+)|(bins/.+/)|(cmake/.+/.+/)', ''))) AS test_binary,
    REGEXP_EXTRACT(test_name, r'GRPC_POLL_STRATEGY=(\w+)') AS poll_strategy,
    job_name,
    build_id
  FROM (
    SELECT
      REGEXP_REPLACE(test_name, r'(/\d+)|(GRPC_POLL_STRATEGY=.+)', '') AS filtered_test_name,
      test_name,
      job_name,
      build_id,
      timestamp
    FROM
      `grpc-testing.jenkins_test_results.aggregate_results`
    WHERE
      timestamp > TIMESTAMP(DATETIME("{calibration_begin} 00:00:00", "America/Los_Angeles"))
      AND timestamp <= TIMESTAMP(DATETIME("{calibration_end} 23:59:59", "America/Los_Angeles"))
      AND NOT REGEXP_CONTAINS(job_name,
        'portability')
      AND result != 'PASSED'
      AND result != 'SKIPPED' )),
  reporting AS (
  SELECT
    RTRIM(LTRIM(REGEXP_REPLACE(filtered_test_name, r'(/\d+)|(bins/.+/)|(cmake/.+/.+/)', ''))) AS test_binary,
    REGEXP_EXTRACT(test_name, r'GRPC_POLL_STRATEGY=(\w+)') AS poll_strategy,
    job_name,
    build_id,
    timestamp
  FROM (
    SELECT
      REGEXP_REPLACE(test_name, r'(/\d+)|(GRPC_POLL_STRATEGY=.+)', '') AS filtered_test_name,
      test_name,
      job_name,
      build_id,
      timestamp
    FROM
      `grpc-testing.jenkins_test_results.aggregate_results`
    WHERE
      timestamp > TIMESTAMP(DATETIME("{reporting_begin} 00:00:00", "America/Los_Angeles"))
      AND timestamp <= TIMESTAMP(DATETIME("{reporting_end} 23:59:59", "America/Los_Angeles"))
      AND NOT REGEXP_CONTAINS(job_name,
        'portability')
      AND result != 'PASSED'
      AND result != 'SKIPPED' ))
SELECT
  reporting.test_binary,
  reporting.poll_strategy,
  reporting.job_name,
  reporting.build_id,
  STRING(reporting.timestamp, "America/Los_Angeles") as timestamp_MTV
FROM
  reporting
LEFT JOIN
  calibration
ON
  reporting.test_binary = calibration.test_binary
WHERE
  calibration.test_binary IS NULL
ORDER BY
  timestamp DESC;
