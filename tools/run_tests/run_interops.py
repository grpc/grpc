import xml.etree.cElementTree as ET
import jobset

java_steps = '../gce_setup/time_test_java.sh'
cxx_steps = '../gce_setup/time_test_cxx.sh'
node_steps = '../gce_setup/time_test_node.sh'

java_job = jobset.JobSpec(cmdline=java_steps)
cxx_job = jobset.JobSpec(cmdline=cxx_steps)
node_job = jobset.JobSpec(cmdline=node_steps)
root = ET.Element('testsuites')
testsuite = ET.SubElement(root, 'testsuite', id='1', package='grpc', name='tests')

jobset.run([java_job, cxx_job, node_job], maxjobs=3, xml_report=testsuite)

tree = ET.ElementTree(root)
tree.write('temp.xml', encoding='UTF-8')

jobset.message ('Good', 'Just starting')


