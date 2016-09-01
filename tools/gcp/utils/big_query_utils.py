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

import argparse
import json
import uuid
import httplib2

from apiclient import discovery
from apiclient.errors import HttpError
from oauth2client.client import GoogleCredentials

NUM_RETRIES = 3


def create_big_query():
  """Authenticates with cloud platform and gets a BiqQuery service object
  """
  creds = GoogleCredentials.get_application_default()
  return discovery.build('bigquery', 'v2', credentials=creds)


def create_dataset(biq_query, project_id, dataset_id):
  is_success = True
  body = {
      'datasetReference': {
          'projectId': project_id,
          'datasetId': dataset_id
      }
  }

  try:
    dataset_req = biq_query.datasets().insert(projectId=project_id, body=body)
    dataset_req.execute(num_retries=NUM_RETRIES)
  except HttpError as http_error:
    if http_error.resp.status == 409:
      print 'Warning: The dataset %s already exists' % dataset_id
    else:
      # Note: For more debugging info, print "http_error.content"
      print 'Error in creating dataset: %s. Err: %s' % (dataset_id, http_error)
      is_success = False
  return is_success


def create_table(big_query, project_id, dataset_id, table_id, table_schema,
                 description):
  fields = [{'name': field_name,
             'type': field_type,
             'description': field_description
             } for (field_name, field_type, field_description) in table_schema]
  return create_table2(big_query, project_id, dataset_id, table_id,
                       fields, description)


def create_table2(big_query, project_id, dataset_id, table_id, fields_schema,
                 description):
  is_success = True

  body = {
      'description': description,
      'schema': {
          'fields': fields_schema
      },
      'tableReference': {
          'datasetId': dataset_id,
          'projectId': project_id,
          'tableId': table_id
      }
  }

  try:
    table_req = big_query.tables().insert(projectId=project_id,
                                          datasetId=dataset_id,
                                          body=body)
    res = table_req.execute(num_retries=NUM_RETRIES)
    print 'Successfully created %s "%s"' % (res['kind'], res['id'])
  except HttpError as http_error:
    if http_error.resp.status == 409:
      print 'Warning: Table %s already exists' % table_id
    else:
      print 'Error in creating table: %s. Err: %s' % (table_id, http_error)
      is_success = False
  return is_success


def insert_rows(big_query, project_id, dataset_id, table_id, rows_list):
  is_success = True
  body = {'rows': rows_list}
  try:
    insert_req = big_query.tabledata().insertAll(projectId=project_id,
                                                 datasetId=dataset_id,
                                                 tableId=table_id,
                                                 body=body)
    res = insert_req.execute(num_retries=NUM_RETRIES)
    if res.get('insertErrors', None):
      print 'Error inserting rows! Response: %s' % res
      is_success = False
  except HttpError as http_error:
    print 'Error inserting rows to the table %s' % table_id
    is_success = False

  return is_success


def sync_query_job(big_query, project_id, query, timeout=5000):
  query_data = {'query': query, 'timeoutMs': timeout}
  query_job = None
  try:
    query_job = big_query.jobs().query(
        projectId=project_id,
        body=query_data).execute(num_retries=NUM_RETRIES)
  except HttpError as http_error:
    print 'Query execute job failed with error: %s' % http_error
    print http_error.content
  return query_job

  # List of (column name, column type, description) tuples
def make_row(unique_row_id, row_values_dict):
  """row_values_dict is a dictionary of column name and column value.
  """
  return {'insertId': unique_row_id, 'json': row_values_dict}
