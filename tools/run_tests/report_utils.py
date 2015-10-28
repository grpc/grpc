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

import os
import string
import xml.etree.cElementTree as ET


def _filter_msg(msg, output_format):
  """Filters out nonprintable and illegal characters from the message."""
  if output_format in ['XML', 'HTML']:
    # keep whitespaces but remove formfeed and vertical tab characters
    # that make XML report unparseable.
    filtered_msg = filter(
        lambda x: x in string.printable and x != '\f' and x != '\v',
        msg.decode(errors='ignore'))
    if output_format == 'HTML':
      filtered_msg = filtered_msg.replace('"', '&quot;')
    return filtered_msg
  else:
    return msg


def render_xml_report(resultset, xml_report):
  """Generate JUnit-like XML report."""
  root = ET.Element('testsuites')
  testsuite = ET.SubElement(root, 'testsuite', id='1', package='grpc', 
                            name='tests')
  for shortname, results in resultset.iteritems(): 
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
  tree = ET.ElementTree(root)
  tree.write(xml_report, encoding='UTF-8')


# TODO(adelez): Use mako template.
def fill_one_test_result(shortname, resultset, html_str):
  if shortname in resultset:
    # Because interop tests does not have runs_per_test flag, each test is run
    # once. So there should only be one element for each result.
    result = resultset[shortname][0] 
    if result.state == 'PASSED':
      html_str = '%s<td bgcolor=\"green\">PASS</td>\n' % html_str
    else:
      tooltip = ''
      if result.returncode > 0 or result.message:
        if result.returncode > 0:
          tooltip = 'returncode: %d ' % result.returncode
        if result.message:
          escaped_msg = _filter_msg(result.message, 'HTML')
          tooltip = '%smessage: %s' % (tooltip, escaped_msg)       
      if result.state == 'FAILED':
        html_str = '%s<td bgcolor=\"red\">' % html_str
        if tooltip:  
          html_str = ('%s<a href=\"#\" data-toggle=\"tooltip\" '
                      'data-placement=\"auto\" title=\"%s\">FAIL</a></td>\n' % 
                      (html_str, tooltip))
        else:
          html_str = '%sFAIL</td>\n' % html_str
      elif result.state == 'TIMEOUT':
        html_str = '%s<td bgcolor=\"yellow\">' % html_str
        if tooltip:
          html_str = ('%s<a href=\"#\" data-toggle=\"tooltip\" '
                      'data-placement=\"auto\" title=\"%s\">TIMEOUT</a></td>\n' 
                      % (html_str, tooltip))
        else:
          html_str = '%sTIMEOUT</td>\n' % html_str
  else:
    html_str = '%s<td bgcolor=\"magenta\">Not implemented</td>\n' % html_str
  
  return html_str


def render_html_report(client_langs, server_langs, test_cases, auth_test_cases,
                       http2_cases, resultset, num_failures, cloud_to_prod, 
                       http2_interop):
  """Generate html report."""
  sorted_test_cases = sorted(test_cases)
  sorted_auth_test_cases = sorted(auth_test_cases)
  sorted_http2_cases = sorted(http2_cases)
  sorted_client_langs = sorted(client_langs)
  sorted_server_langs = sorted(server_langs)
  html_str = ('<!DOCTYPE html>\n'
              '<html lang=\"en\">\n'
              '<head><title>Interop Test Result</title></head>\n'
              '<body>\n')
  if num_failures > 1:
    html_str = (
        '%s<p><h2><font color=\"red\">%d tests failed!</font></h2></p>\n' % 
        (html_str, num_failures))
  elif num_failures:
    html_str = (
        '%s<p><h2><font color=\"red\">%d test failed!</font></h2></p>\n' % 
        (html_str, num_failures))
  else:
    html_str = (
        '%s<p><h2><font color=\"green\">All tests passed!</font></h2></p>\n' % 
        html_str)
  if cloud_to_prod:
    # Each column header is the client language.
    html_str = ('%s<h2>Cloud to Prod</h2>\n' 
                '<table style=\"width:100%%\" border=\"1\">\n'
                '<tr bgcolor=\"#00BFFF\">\n'
                '<th>Client languages &#9658;</th>\n') % html_str
    for client_lang in sorted_client_langs:
      html_str = '%s<th>%s\n' % (html_str, client_lang)
    html_str = '%s</tr>\n' % html_str
    for test_case in sorted_test_cases + sorted_auth_test_cases:
      html_str = '%s<tr><td><b>%s</b></td>\n' % (html_str, test_case)
      for client_lang in sorted_client_langs:
        if not test_case in sorted_auth_test_cases:
          shortname = 'cloud_to_prod:%s:%s' % (client_lang, test_case)
        else:
          shortname = 'cloud_to_prod_auth:%s:%s' % (client_lang, test_case)
        html_str = fill_one_test_result(shortname, resultset, html_str)
      html_str = '%s</tr>\n' % html_str 
    html_str = '%s</table>\n' % html_str
  if server_langs:
    for test_case in sorted_test_cases:
      # Each column header is the client language.
      html_str = ('%s<h2>%s</h2>\n' 
                  '<table style=\"width:100%%\" border=\"1\">\n'
                  '<tr bgcolor=\"#00BFFF\">\n'
                  '<th>Client languages &#9658;<br/>'
                  'Server languages &#9660;</th>\n') % (html_str, test_case)
      for client_lang in sorted_client_langs:
        html_str = '%s<th>%s\n' % (html_str, client_lang)
      html_str = '%s</tr>\n' % html_str
      # Each row head is the server language.
      for server_lang in sorted_server_langs:
        html_str = '%s<tr><td><b>%s</b></td>\n' % (html_str, server_lang)
        # Fill up the cells with test result.
        for client_lang in sorted_client_langs:
          shortname = 'cloud_to_cloud:%s:%s_server:%s' % (
              client_lang, server_lang, test_case)
          html_str = fill_one_test_result(shortname, resultset, html_str)
        html_str = '%s</tr>\n' % html_str
      html_str = '%s</table>\n' % html_str
  if http2_interop:
    # Each column header is the server language.
    html_str = ('%s<h2>HTTP/2 Interop</h2>\n' 
                '<table style=\"width:100%%\" border=\"1\">\n'
                '<tr bgcolor=\"#00BFFF\">\n'
                '<th>Servers &#9658;<br/>'
                'Test Cases &#9660;</th>\n') % html_str
    for server_lang in sorted_server_langs:
      html_str = '%s<th>%s\n' % (html_str, server_lang)
    if cloud_to_prod:
      html_str = '%s<th>%s\n' % (html_str, "prod")
    html_str = '%s</tr>\n' % html_str
    for test_case in sorted_http2_cases:
      html_str = '%s<tr><td><b>%s</b></td>\n' % (html_str, test_case)
      # Fill up the cells with test result.
      for server_lang in sorted_server_langs:
        shortname = 'cloud_to_cloud:%s:%s_server:%s' % (
            "http2", server_lang, test_case)
        html_str = fill_one_test_result(shortname, resultset, html_str)
      if cloud_to_prod:
        shortname = 'cloud_to_prod:%s:%s' % ("http2", test_case)
        html_str = fill_one_test_result(shortname, resultset, html_str)
      html_str = '%s</tr>\n' % html_str
    html_str = '%s</table>\n' % html_str

  html_str = ('%s\n'
              '<script>\n'
              '$(document).ready(function(){'
              '$(\'[data-toggle=\"tooltip\"]\').tooltip();\n'   
              '});\n'
              '</script>\n'
              '</body>\n'
              '</html>') % html_str  
  
  # Write to reports/index.html as set up in Jenkins plugin.
  html_report_dir = 'reports'
  if not os.path.exists(html_report_dir):
    os.mkdir(html_report_dir)
  html_file_path = os.path.join(html_report_dir, 'index.html')
  with open(html_file_path, 'w') as f:
    f.write(html_str)
