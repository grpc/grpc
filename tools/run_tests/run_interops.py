import xml.etree.cElementTree as ET
import jobset

cxx_steps = 'tools/run_tests/run_interops.sh'

cxx_job = jobset.JobSpec(cmdline=cxx_steps)
root = ET.Element('testsuites')
testsuite = ET.SubElement(root, 'testsuite', id='1', package='grpc', name='tests')

jobset.run([cxx_job], maxjobs=3, xml_report=testsuite)

tree = ET.ElementTree(root)
tree.write('report.xml', encoding='UTF-8')

jobset.message ('Good', 'Just starting')

