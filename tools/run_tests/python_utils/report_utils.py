# Copyright 2015 gRPC authors.
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
"""Generate XML and HTML test reports."""

try:
    from mako.runtime import Context
    from mako.template import Template
    from mako import exceptions
except (ImportError):
    pass  # Mako not installed but it is ok.
import datetime
import os
import string
import xml.etree.cElementTree as ET
import six


def _filter_msg(msg, output_format):
    """Filters out nonprintable and illegal characters from the message."""
    if output_format in ['XML', 'HTML']:
        # keep whitespaces but remove formfeed and vertical tab characters
        # that make XML report unparsable.
        filtered_msg = ''.join(
            filter(lambda x: x in string.printable and x != '\f' and x != '\v',
                   msg.decode('UTF-8', 'ignore')))
        if output_format == 'HTML':
            filtered_msg = filtered_msg.replace('"', '&quot;')
        return filtered_msg
    else:
        return msg


def new_junit_xml_tree():
    return ET.ElementTree(ET.Element('testsuites'))


def render_junit_xml_report(resultset,
                            report_file,
                            suite_package='grpc',
                            suite_name='tests',
                            replace_dots=True,
                            multi_target=False):
    """Generate JUnit-like XML report."""
    if not multi_target:
        tree = new_junit_xml_tree()
        append_junit_xml_results(tree, resultset, suite_package, suite_name,
                                 '1', replace_dots)
        create_xml_report_file(tree, report_file)
    else:
        # To have each test result displayed as a separate target by the Resultstore/Sponge UI,
        # we generate a separate XML report file for each test result
        for shortname, results in six.iteritems(resultset):
            one_result = {shortname: results}
            tree = new_junit_xml_tree()
            append_junit_xml_results(tree, one_result,
                                     '%s_%s' % (suite_package, shortname),
                                     '%s_%s' % (suite_name, shortname), '1',
                                     replace_dots)
            per_suite_report_file = os.path.join(os.path.dirname(report_file),
                                                 shortname,
                                                 os.path.basename(report_file))
            create_xml_report_file(tree, per_suite_report_file)


def create_xml_report_file(tree, report_file):
    """Generate JUnit-like report file from xml tree ."""
    # ensure the report directory exists
    report_dir = os.path.dirname(os.path.abspath(report_file))
    if not os.path.exists(report_dir):
        os.makedirs(report_dir)
    tree.write(report_file, encoding='UTF-8')


def append_junit_xml_results(tree,
                             resultset,
                             suite_package,
                             suite_name,
                             id,
                             replace_dots=True):
    """Append a JUnit-like XML report tree with test results as a new suite."""
    if replace_dots:
        # ResultStore UI displays test suite names containing dots only as the component
        # after the last dot, which results bad info being displayed in the UI.
        # We replace dots by another character to avoid this problem.
        suite_name = suite_name.replace('.', '_')
    testsuite = ET.SubElement(tree.getroot(),
                              'testsuite',
                              id=id,
                              package=suite_package,
                              name=suite_name,
                              timestamp=datetime.datetime.now().isoformat())
    failure_count = 0
    error_count = 0
    for shortname, results in six.iteritems(resultset):
        for result in results:
            xml_test = ET.SubElement(testsuite, 'testcase', name=shortname)
            if result.elapsed_time:
                xml_test.set('time', str(result.elapsed_time))
            filtered_msg = _filter_msg(result.message, 'XML')
            if result.state == 'FAILED':
                ET.SubElement(xml_test, 'failure',
                              message='Failure').text = filtered_msg
                failure_count += 1
            elif result.state == 'TIMEOUT':
                ET.SubElement(xml_test, 'error',
                              message='Timeout').text = filtered_msg
                error_count += 1
            elif result.state == 'SKIPPED':
                ET.SubElement(xml_test, 'skipped', message='Skipped')
    testsuite.set('failures', str(failure_count))
    testsuite.set('errors', str(error_count))


def render_interop_html_report(client_langs, server_langs, test_cases,
                               auth_test_cases, http2_cases, http2_server_cases,
                               resultset, num_failures, cloud_to_prod,
                               prod_servers, http2_interop):
    """Generate HTML report for interop tests."""
    template_file = 'tools/run_tests/interop/interop_html_report.template'
    try:
        mytemplate = Template(filename=template_file, format_exceptions=True)
    except NameError:
        print(
            'Mako template is not installed. Skipping HTML report generation.')
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

    args = {
        'client_langs': sorted_client_langs,
        'server_langs': sorted_server_langs,
        'test_cases': sorted_test_cases,
        'auth_test_cases': sorted_auth_test_cases,
        'http2_cases': sorted_http2_cases,
        'http2_server_cases': sorted_http2_server_cases,
        'resultset': resultset,
        'num_failures': num_failures,
        'cloud_to_prod': cloud_to_prod,
        'prod_servers': sorted_prod_servers,
        'http2_interop': http2_interop
    }

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
