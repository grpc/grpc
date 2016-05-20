#!/usr/bin/env python2.7
# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import datetime
import json
import os
import re
import select
import subprocess
import sys
import time

# Import big_query_utils module
bq_utils_dir = os.path.abspath(os.path.join(
    os.path.dirname(__file__), '../utils'))
sys.path.append(bq_utils_dir)
import big_query_utils as bq_utils


class EventType:
  STARTING = 'STARTING'
  RUNNING = 'RUNNING'
  SUCCESS = 'SUCCESS'
  FAILURE = 'FAILURE'


class BigQueryHelper:
  """Helper class for the stress test wrappers to interact with BigQuery.
  """

  def __init__(self, run_id, image_type, pod_name, project_id, dataset_id,
               summary_table_id, qps_table_id):
    self.run_id = run_id
    self.image_type = image_type
    self.pod_name = pod_name
    self.project_id = project_id
    self.dataset_id = dataset_id
    self.summary_table_id = summary_table_id
    self.qps_table_id = qps_table_id

  def initialize(self):
    self.bq = bq_utils.create_big_query()

  def setup_tables(self):
    return bq_utils.create_dataset(self.bq, self.project_id, self.dataset_id) \
        and self.__create_summary_table() \
        and self.__create_qps_table()

  def insert_summary_row(self, event_type, details):
    row_values_dict = {
        'run_id': self.run_id,
        'image_type': self.image_type,
        'pod_name': self.pod_name,
        'event_date': datetime.datetime.now().isoformat(),
        'event_type': event_type,
        'details': details
    }
    # row_unique_id is something that uniquely identifies the row (BigQuery uses
    # it for duplicate detection).
    row_unique_id = '%s_%s_%s' % (self.run_id, self.pod_name, event_type)
    row = bq_utils.make_row(row_unique_id, row_values_dict)
    return bq_utils.insert_rows(self.bq, self.project_id, self.dataset_id,
                                self.summary_table_id, [row])

  def insert_qps_row(self, qps, recorded_at):
    row_values_dict = {
        'run_id': self.run_id,
        'pod_name': self.pod_name,
        'recorded_at': recorded_at,
        'qps': qps
    }

    # row_unique_id is something that uniquely identifies the row (BigQuery uses
    # it for duplicate detection).
    row_unique_id = '%s_%s_%s' % (self.run_id, self.pod_name, recorded_at)
    row = bq_utils.make_row(row_unique_id, row_values_dict)
    return bq_utils.insert_rows(self.bq, self.project_id, self.dataset_id,
                                self.qps_table_id, [row])

  def check_if_any_tests_failed(self, num_query_retries=3, timeout_msec=30000):
    query = ('SELECT event_type FROM %s.%s WHERE run_id = \'%s\' AND '
             'event_type="%s"') % (self.dataset_id, self.summary_table_id,
                                   self.run_id, EventType.FAILURE)
    page = None
    try:
      query_job = bq_utils.sync_query_job(self.bq, self.project_id, query)
      job_id = query_job['jobReference']['jobId']
      project_id = query_job['jobReference']['projectId']
      page = self.bq.jobs().getQueryResults(
          projectId=project_id,
          jobId=job_id,
          timeoutMs=timeout_msec).execute(num_retries=num_query_retries)

      if not page['jobComplete']:
        print('TIMEOUT ERROR: The query %s timed out. Current timeout value is'
              ' %d msec. Returning False (i.e assuming there are no failures)'
             ) % (query, timeoout_msec)
        return False

      num_failures = int(page['totalRows'])
      print 'num rows: ', num_failures
      return num_failures > 0
    except:
      print 'Exception in check_if_any_tests_failed(). Info: ', sys.exc_info()
      print 'Query: ', query

  def print_summary_records(self, num_query_retries=3):
    line = '-' * 120
    print line
    print 'Summary records'
    print 'Run Id: ', self.run_id
    print 'Dataset Id: ', self.dataset_id
    print line
    query = ('SELECT pod_name, image_type, event_type, event_date, details'
             ' FROM %s.%s WHERE run_id = \'%s\' ORDER by event_date;') % (
                 self.dataset_id, self.summary_table_id, self.run_id)
    query_job = bq_utils.sync_query_job(self.bq, self.project_id, query)

    print '{:<25} {:<12} {:<12} {:<30} {}'.format('Pod name', 'Image type',
                                                  'Event type', 'Date',
                                                  'Details')
    print line
    page_token = None
    while True:
      page = self.bq.jobs().getQueryResults(
          pageToken=page_token,
          **query_job['jobReference']).execute(num_retries=num_query_retries)
      rows = page.get('rows', [])
      for row in rows:
        print '{:<25} {:<12} {:<12} {:<30} {}'.format(row['f'][0]['v'],
                                                      row['f'][1]['v'],
                                                      row['f'][2]['v'],
                                                      row['f'][3]['v'],
                                                      row['f'][4]['v'])
      page_token = page.get('pageToken')
      if not page_token:
        break

  def print_qps_records(self, num_query_retries=3):
    line = '-' * 80
    print line
    print 'QPS Summary'
    print 'Run Id: ', self.run_id
    print 'Dataset Id: ', self.dataset_id
    print line
    query = (
        'SELECT pod_name, recorded_at, qps FROM %s.%s WHERE run_id = \'%s\' '
        'ORDER by recorded_at;') % (self.dataset_id, self.qps_table_id,
                                    self.run_id)
    query_job = bq_utils.sync_query_job(self.bq, self.project_id, query)
    print '{:<25} {:30} {}'.format('Pod name', 'Recorded at', 'Qps')
    print line
    page_token = None
    while True:
      page = self.bq.jobs().getQueryResults(
          pageToken=page_token,
          **query_job['jobReference']).execute(num_retries=num_query_retries)
      rows = page.get('rows', [])
      for row in rows:
        print '{:<25} {:30} {}'.format(row['f'][0]['v'], row['f'][1]['v'],
                                       row['f'][2]['v'])
      page_token = page.get('pageToken')
      if not page_token:
        break

  def __create_summary_table(self):
    summary_table_schema = [
        ('run_id', 'STRING', 'Test run id'),
        ('image_type', 'STRING', 'Client or Server?'),
        ('pod_name', 'STRING', 'GKE pod hosting this image'),
        ('event_date', 'STRING', 'The date of this event'),
        ('event_type', 'STRING', 'STARTING/RUNNING/SUCCESS/FAILURE'),
        ('details', 'STRING', 'Any other relevant details')
    ]
    desc = ('The table that contains STARTING/RUNNING/SUCCESS/FAILURE events '
            'for the stress test clients and servers')
    return bq_utils.create_table(self.bq, self.project_id, self.dataset_id,
                                 self.summary_table_id, summary_table_schema,
                                 desc)

  def __create_qps_table(self):
    qps_table_schema = [
        ('run_id', 'STRING', 'Test run id'),
        ('pod_name', 'STRING', 'GKE pod hosting this image'),
        ('recorded_at', 'STRING', 'Metrics recorded at time'),
        ('qps', 'INTEGER', 'Queries per second')
    ]
    desc = 'The table that cointains the qps recorded at various intervals'
    return bq_utils.create_table(self.bq, self.project_id, self.dataset_id,
                                 self.qps_table_id, qps_table_schema, desc)
