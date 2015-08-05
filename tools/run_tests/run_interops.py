import argparse
import xml.etree.cElementTree as ET
import jobset

argp = argparse.ArgumentParser(description='Run interop tests.')
argp.add_argument('-l', '--language',
                  choices=['build_only', 'c++'],
                  nargs='+',
                  default=['build_only'])
args = argp.parse_args()

# build job
build_steps = 'tools/run_tests/run_interops_build.sh'
build_job = jobset.JobSpec(cmdline=build_steps, shortname='build')

# test jobs
_TESTS = ['large_unary', 'empty_unary', 'ping_pong', 'client_streaming', 'server_streaming']
jobs = []
jobNumber = 0
for lang in args.language:
  for test in _TESTS:
    test_job = jobset.JobSpec(
        cmdline=['tools/run_tests/run_interops_test.sh', '%s' % lang, '%s' % test],
        shortname=test,
        timeout_seconds=15*60)
    jobs.append(test_job)
    jobNumber+=1

root = ET.Element('testsuites')
testsuite = ET.SubElement(root, 'testsuite', id='1', package='grpc', name='tests')

# always do the build of docker first, and then all the tests can run in parallel
jobset.run([build_job], maxjobs=1, xml_report=testsuite)
jobset.run(jobs, maxjobs=jobNumber, xml_report=testsuite)

tree = ET.ElementTree(root)
tree.write('report.xml', encoding='UTF-8')


