import argparse
import json
import uuid
import httplib2

from apiclient import discovery
from apiclient.errors import HttpError
from oauth2client.client import GoogleCredentials

NUM_RETRIES = 3


def create_bq():
  """Authenticates with cloud platform and gets a BiqQuery service object
  """
  creds = GoogleCredentials.get_application_default()
  return discovery.build('bigquery', 'v2', credentials=creds)


def create_ds(biq_query, project_id, dataset_id):
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


def make_field(field_name, field_type, field_description):
  return {
      'name': field_name,
      'type': field_type,
      'description': field_description
  }


def create_table(big_query, project_id, dataset_id, table_id, fields_list,
                 description):
  is_success = True
  body = {
      'description': description,
      'schema': {
          'fields': fields_list
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
    print body
    res = insert_req.execute(num_retries=NUM_RETRIES)
    print res
  except HttpError as http_error:
    print 'Error in inserting rows in the table %s' % table_id
    is_success = False
  return is_success

#####################


def make_emp_row(emp_id, emp_name, emp_email):
  return {
      'insertId': str(emp_id),
      'json': {
          'emp_id': emp_id,
          'emp_name': emp_name,
          'emp_email_id': emp_email
      }
  }


def get_emp_table_fields_list():
  return [
      make_field('emp_id', 'INTEGER', 'Employee id'),
      make_field('emp_name', 'STRING', 'Employee name'),
      make_field('emp_email_id', 'STRING', 'Employee email id')
  ]


def insert_emp_rows(big_query, project_id, dataset_id, table_id, start_idx,
                    num_rows):
  rows_list = [make_emp_row(i, 'sree_%d' % i, 'sreecha_%d@gmail.com' % i)
               for i in range(start_idx, start_idx + num_rows)]
  insert_rows(big_query, project_id, dataset_id, table_id, rows_list)


def create_emp_table(big_query, project_id, dataset_id, table_id):
  fields_list = get_emp_table_fields_list()
  description = 'Test table created by sree'
  create_table(big_query, project_id, dataset_id, table_id, fields_list,
               description)


def sync_query(big_query, project_id, query, timeout=5000):
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

#[Start query_emp_records]
def query_emp_records(big_query, project_id, dataset_id, table_id):
  query = 'SELECT emp_id, emp_name FROM %s.%s ORDER BY emp_id;' % (dataset_id, table_id)
  print query
  query_job = sync_query(big_query, project_id, query, 5000)
  job_id = query_job['jobReference']

  print query_job
  print '**Starting paging **'
  #[Start Paging]
  page_token = None
  while True:
    page = big_query.jobs().getQueryResults(
        pageToken=page_token,
        **query_job['jobReference']).execute(num_retries=NUM_RETRIES)
    rows = page['rows']
    for row in rows:
      print row['f'][0]['v'], "---", row['f'][1]['v']
    page_token = page.get('pageToken')
    if not page_token:
      break
  #[End Paging]
#[End query_emp_records]

#########################
DATASET_SEQ_NUM = 1
TABLE_SEQ_NUM = 11

PROJECT_ID = 'sree-gce'
DATASET_ID = 'sree_test_dataset_%d' % DATASET_SEQ_NUM
TABLE_ID = 'sree_test_table_%d' % TABLE_SEQ_NUM

EMP_ROW_IDX = 10
EMP_NUM_ROWS = 5

bq = create_bq()
create_ds(bq, PROJECT_ID, DATASET_ID)
create_emp_table(bq, PROJECT_ID, DATASET_ID, TABLE_ID)
insert_emp_rows(bq, PROJECT_ID, DATASET_ID, TABLE_ID, EMP_ROW_IDX, EMP_NUM_ROWS)
query_emp_records(bq, PROJECT_ID, DATASET_ID, TABLE_ID)
