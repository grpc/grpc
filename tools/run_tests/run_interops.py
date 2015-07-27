import xml.etree.cElementTree as ET
import jobset

build_steps = 'tools/run_tests/run_interops_build.sh'

_TESTS = ['large_unary', 'empty_unary', 'ping_pong', 'client_streaming', 'server_streaming']

build_job = jobset.JobSpec(cmdline=build_steps, shortname='build')

jobs = []
jobNumber = 0

for test in _TESTS:
  test_job = jobset.JobSpec(cmdline=['tools/run_tests/run_interops_test.sh', '%s' % test], shortname='cxx')
  jobs.append(test_job)
  jobNumber+=1

root = ET.Element('testsuites')
testsuite = ET.SubElement(root, 'testsuite', id='1', package='grpc', name='tests')

jobset.run([build_job], maxjobs=1, xml_report=testsuite)
jobset.run(jobs, maxjobs=jobNumber, xml_report=testsuite)

tree = ET.ElementTree(root)
tree.write('report.xml', encoding='UTF-8')

jobset.message ('Good', 'Just starting')

