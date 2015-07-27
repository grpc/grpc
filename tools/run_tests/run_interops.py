import xml.etree.cElementTree as ET
import jobset

build_steps = 'tools/run_tests/run_interops_build.sh'
cxx_steps = 'tools/run_tests/run_interops_test.sh'

build_job = jobset.JobSpec(cmdline=build_steps, shortname='build')
cxx_job = jobset.JobSpec(cmdline=cxx_steps, shortname='cxx')
root = ET.Element('testsuites')
testsuite = ET.SubElement(root, 'testsuite', id='1', package='grpc', name='tests')

jobset.run([build_job], maxjobs=1, xml_report=testsuite)
jobset.run([cxx_job], maxjobs=1, xml_report=testsuite)

tree = ET.ElementTree(root)
tree.write('report.xml', encoding='UTF-8')

jobset.message ('Good', 'Just starting')

