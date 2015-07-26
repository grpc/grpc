import xml.etree.cElementTree as ET
import jobset

#cxx_steps = 'tools/run_tests/run_interops.sh'
cxx_steps = 'sudo docker run grpc/cxx /var/local/git/grpc/bins/opt/interop_client --enable_ssl --use_prod_roots --server_host_override
=grpc-test.sandbox.google.com --server_host=grpc-test.sandbox.google.com --server_port=443 --test_case=large_unary'

cxx_job = jobset.JobSpec(cmdline=cxx_steps, shortname='interop')
root = ET.Element('testsuites')
testsuite = ET.SubElement(root, 'testsuite', id='1', package='grpc', name='tests')

jobset.run([cxx_job], maxjobs=3, xml_report=testsuite)

tree = ET.ElementTree(root)
tree.write('report.xml', encoding='UTF-8')

jobset.message ('Good', 'Just starting')

