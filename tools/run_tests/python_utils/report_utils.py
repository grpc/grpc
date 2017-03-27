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

"""Generate XML and HTML test reports."""

from __future__ import print_function

try:
  from mako.runtime import Context
  from mako.template import Template
  from mako import exceptions
except (ImportError):
  pass  # Mako not installed but it is ok.
import os
import string
import xml.etree.cElementTree as ET
import six


def _filter_msg(msg, output_format):
  """Filters out nonprintable and illegal characters from the message."""
  if output_format in ['XML', 'HTML']:
    # keep whitespaces but remove formfeed and vertical tab characters
    # that make XML report unparseable.
    filtered_msg = filter(
        lambda x: x in string.printable and x != '\f' and x != '\v',
        msg.decode('UTF-8', 'ignore'))
    if output_format == 'HTML':
      filtered_msg = filtered_msg.replace('"', '&quot;')
    return filtered_msg
  else:
    return msg


def render_junit_xml_report(resultset, xml_report, suite_package='grpc',
                            suite_name='tests'):
  """Generate JUnit-like XML report."""
  root = ET.Element('testsuites')
  testsuite = ET.SubElement(root, 'testsuite', id='1', package=suite_package,
                            name=suite_name)
  for shortname, results in six.iteritems(resultset):
    for result in results:
      xml_test = ET.SubElement(testsuite, 'testcase', name=shortname)
      if result.elapsed_time:
        xml_test.set('time', str(result.elapsed_time))
      ET.SubElement(xml_test, 'system-out').text = _filter_msg(result.message,
                                                               'XML')
      if result.state == 'FAILED':
        ET.SubElement(xml_test, 'failure', message='Failure')
      elif result.state == 'TIMEOUT':
        ET.SubElement(xml_test, 'error', message='Timeout')
      elif result.state == 'SKIPPED':
        ET.SubElement(xml_test, 'skipped', message='Skipped')
  tree = ET.ElementTree(root)
  tree.write(xml_report, encoding='UTF-8')

def render_interop_html_report(
  client_langs, server_langs, test_cases, auth_test_cases, http2_cases,
  http2_server_cases, resultset,
  num_failures, cloud_to_prod, prod_servers, http2_interop):
  """Generate HTML report for interop tests."""
  template_file = 'tools/run_tests/interop/interop_html_report.template'
  try:
    mytemplate = Template(filename=template_file, format_exceptions=True)
  except NameError:
    print('Mako template is not installed. Skipping HTML report generation.')
    return
  except IOError as e:
    print('Failed to find the template %s: %s' % (template_file, e))
    return

  sorted_test_cases = sorted(test_cases)
  sorted_auth_test_cases = sorted(auth_test_cases)
  sorted_http2_cases = sorted(http2_cases)
  sorted_http2_server_cases = sorted(http2_server_cases)
  sorted_client_langs = sorted(client_langs)
  sorted_server_langs = sorted(server_langs)
  sorted_prod_servers = sorted(prod_servers)

  args = {'client_langs': sorted_client_langs,
          'server_langs': sorted_server_langs,
          'test_cases': sorted_test_cases,
          'auth_test_cases': sorted_auth_test_cases,
          'http2_cases': sorted_http2_cases,
          'http2_server_cases': sorted_http2_server_cases,
          'resultset': resultset,
          'num_failures': num_failures,
          'cloud_to_prod': cloud_to_prod,
          'prod_servers': sorted_prod_servers,
          'http2_interop': http2_interop}

  html_report_out_dir = 'reports'
  if not os.path.exists(html_report_out_dir):
    os.mkdir(html_report_out_dir)
  html_file_path = os.path.join(html_report_out_dir, 'index.html')
  try:
    with open(html_file_path, 'w') as output_file:
      mytemplate.render_context(Context(output_file, **args))
  except:
    print(exceptions.text_error_template().render())
    raise

def render_perf_profiling_results(output_filepath, profile_names):
  with open(output_filepath, 'w') as output_file:
    output_file.write('<ul>\n')
    for name in profile_names:
      output_file.write('<li><a href=%s>%s</a></li>\n' % (name, name))
    output_file.write('</ul>\n')
