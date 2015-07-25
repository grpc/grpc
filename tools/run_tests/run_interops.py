import argparse
import xml.etree.cElementTree as ET
import jobset

argp = argparse.ArgumentParser(description='Run interop tests.')
argp.add_argument('-l', '--language',
                  choices=['build_only', 'c++'],
                  nargs='+',
                  default=['build_only'])
args = argp.parse_args()

_TESTS = ['large_unary', 'empty_unary', 'ping_pong']

jobs = []
jobNumber = 0

build_job = jobset.JobSpec(cmdline='tools/run_tests/run_interops.sh', shortname='interop')
jobs.append(build_job)
jobNumber+=1

if args.language == 'cxx':
  testCommand = 'sudo docker run grpc/cxx /var/local/git/grpc/bins/opt/interop_client --enable_ssl --use_prod_roots --server_host_override=grpc-test.sandbox.google.com --server_host=grpc-test.sandbox.google.com --server_port=443 --test_case='
  for test in _TESTS:
    job = jobset.JobSpec(cmdline=testCommand+test, shortname=test)
    jobs.append(job)
    jobNumber+=1

root = ET.Element('testsuites')
testsuite = ET.SubElement(root, 'testsuite', id='1', package='grpc', name='tests')

jobset.run(jobs, maxjobs=jobNumber, xml_report=testsuite)

tree = ET.ElementTree(root)
tree.write('report.xml', encoding='UTF-8')

jobset.message ('Good', 'Just starting')

