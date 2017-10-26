# Copyright 2017 gRPC authors.
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


import atexit
import os
import subprocess
import sys
import time
import unittest
import tempfile


processes = []

# Make sure we attempt to clean up any
# processes we may have left running
def cleanup_processes():
    for process in processes:
        try:
            process.kill()
        except Exception:
            pass


atexit.register(cleanup_processes)

DIR = os.path.abspath(os.path.dirname(__file__))

def _get_abs_path(path):
    return DIR + '/' + path


def run_client(cmd):
    # run the command, wait for it to exit, and return its stdout and
    # stderr. if there's any error in running the command, then None
    # is returned for both
    p = None
    try:
        p = subprocess.Popen(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            env={'GRPC_VERBOSITY': 'ERROR'})
        return p.communicate()
    except:
        pass
    return None, None


# stdout and stderr when the client successfully performs the rpc
def assert_client_success(stdout, stderr):
    assert stdout == 'Greeter client received: Hello, you\n', 'unexpected stdout: [{}]'.format(stdout)
    assert stderr == '', 'unexpected stderr: [{}]'.format(stderr)
    return

def assert_client_failure(stdout, stderr, client_rejects_server):
    # "client_rejects_server": true if the failure is because client
    # rejects server, false if because server rejects client
    # print 'checking stdout...'
    assert stdout == '', 'unexpected stdout: {}'.format(stdout)
    if client_rejects_server:
        # print 'checking for "handshake failed" msg...'
        # if server rejects client, then client won't see this msg
        assert 'Handshake failed with fatal error' in stderr
    assert not 'Greeter client received' in stderr
    return

def check_background_client_success(stdout, stderr,
                                    before_switch_timestamp, after_switch_timestamp):
    assert stderr == '', stderr
    lines = stdout.splitlines()
    assert len(lines) == 2
    assert lines[0].startswith('Greeter client received: Hello, '), lines[0]
    assert int(lines[0].split()[4]) < before_switch_timestamp
    assert lines[1].startswith('Greeter client received: Hello, '), lines[1]
    assert int(lines[1].split()[4]) > after_switch_timestamp



class ServerSSLCertReloadTest(unittest.TestCase):

    def test_server_ssl_cert_reload_basic(self):
        switch_cert_on_client_num = 9

        # use tempfiles to contain the client's cert plus intermediate
        # cert from the two cert hierarchies

        client_key_1_fpath = _get_abs_path('cert_hier_1/intermediate/private/client.key.pem')
        client_chain_1_fp = tempfile.NamedTemporaryFile(suffix='.pem')
        client_chain_1_fp.write('''{}
{}
'''.format(open(_get_abs_path('cert_hier_1/intermediate/certs/client.cert.pem')).read(),
           open(_get_abs_path('cert_hier_1/intermediate/certs/intermediate.cert.pem')).read(),
           ))
        client_chain_1_fp.flush()

        client_key_2_fpath = _get_abs_path('cert_hier_2/intermediate/private/client.key.pem')
        client_chain_2_fp = tempfile.NamedTemporaryFile(suffix='.pem')
        client_chain_2_fp.write('''{}
{}
'''.format(open(_get_abs_path('cert_hier_2/intermediate/certs/client.cert.pem')).read(),
           open(_get_abs_path('cert_hier_2/intermediate/certs/intermediate.cert.pem')).read(),
           ))
        client_chain_2_fp.flush()

        server_ca_1_fpath = _get_abs_path('cert_hier_1/certs/ca.cert.pem')
        server_ca_2_fpath = _get_abs_path('cert_hier_2/certs/ca.cert.pem')

        greeter_client_path = _get_abs_path('greeter_client.py')

        require_client_auth = True

        devnull_fp = open(os.devnull)
        server_process = subprocess.Popen(
            ['python', _get_abs_path('greeter_server.py'),
             str(switch_cert_on_client_num), str(require_client_auth)],
            stdout=devnull_fp, stderr=devnull_fp)
        processes.append(server_process)
        # give server a second to initialize
        import time
        time.sleep(1)

        print 'things should work...',
        stdout, stderr = run_client(
            ['python', greeter_client_path,
             server_ca_1_fpath, client_key_2_fpath, client_chain_2_fp.name])
        assert_client_success(stdout, stderr)
        print 'nice!'

        print 'client should reject server...',
        # fails because client trusts ca1 and so will reject server
        stdout, stderr = run_client(
            ['python', greeter_client_path,
             server_ca_2_fpath, client_key_2_fpath, client_chain_2_fp.name])
        assert_client_failure(stdout, stderr, True)
        # this assert is fragile, i.e., depends on how the core lib logs the error
        assert 'SSL_ERROR_SSL: error:1000007d:SSL routines:OPENSSL_internal:CERTIFICATE_VERIFY_FAILED' in stderr
        print 'good'

        print 'should work again...',
        stdout, stderr = run_client(
            ['python', greeter_client_path,
             server_ca_1_fpath, client_key_2_fpath, client_chain_2_fp.name])
        assert_client_success(stdout, stderr)
        print 'good'

        print 'client should be rejected by server...',
        # fails because client uses key/cert1, but server trusts ca2,
        # so server will reject
        stdout, stderr = run_client(
            ['python', greeter_client_path,
             server_ca_1_fpath, client_key_1_fpath, client_chain_1_fp.name])
        assert_client_failure(stdout, stderr, False)
        print 'good'

        print 'one more time, client should be rejected by server...',
        stdout, stderr = run_client(
            ['python', greeter_client_path,
             server_ca_1_fpath, client_key_1_fpath, client_chain_1_fp.name])
        assert_client_failure(stdout, stderr, False)
        print 'good'

        print 'should work again...',
        stdout, stderr = run_client(
            ['python', greeter_client_path,
             server_ca_1_fpath, client_key_2_fpath, client_chain_2_fp.name])
        assert_client_success(stdout, stderr)
        print 'good'

        print 'should work again...',
        stdout, stderr = run_client(
            ['python', greeter_client_path,
             server_ca_1_fpath, client_key_2_fpath, client_chain_2_fp.name])
        assert_client_success(stdout, stderr)
        print 'good'

        background_client_process = subprocess.Popen(
            ['python', _get_abs_path('greeter_client_2.py'), server_ca_1_fpath,
             client_key_2_fpath, client_chain_2_fp.name, "4"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            env={'GRPC_VERBOSITY': 'ERROR'})
        processes.append(background_client_process)

        time.sleep(1)

        before_switch_timestamp = time.time()

        print
        print 'moment of truth!! client should reject server because the server switch cert...',
        stdout, stderr = run_client(
            ['python', greeter_client_path,
             server_ca_1_fpath, client_key_2_fpath, client_chain_2_fp.name])
        assert_client_failure(stdout, stderr, True)
        print 'NICE!!'
        print

        after_switch_timestamp = time.time()

        print 'now should work again...',
        stdout, stderr = run_client(
            ['python', greeter_client_path,
             server_ca_2_fpath, client_key_1_fpath, client_chain_1_fp.name])
        assert_client_success(stdout, stderr)
        print 'good'

        print 'and again...',
        stdout, stderr = run_client(
            ['python', greeter_client_path,
             server_ca_2_fpath, client_key_1_fpath, client_chain_1_fp.name])
        assert_client_success(stdout, stderr)
        print 'good'

        print 'client should be rejected by server...',
        stdout, stderr = run_client(
            ['python', greeter_client_path,
             server_ca_2_fpath, client_key_2_fpath, client_chain_2_fp.name])
        assert_client_failure(stdout, stderr, False)
        print 'good'

        print 'here client should reject server...',
        stdout, stderr = run_client(
            ['python', greeter_client_path,
             server_ca_1_fpath, client_key_2_fpath, client_chain_2_fp.name])
        assert_client_failure(stdout, stderr, True)
        print 'good'

        print 'check background client...',
        stdout, stderr = background_client_process.communicate()
        check_background_client_success(stdout, stderr,
                                        before_switch_timestamp, after_switch_timestamp)
        print 'good'

        print 'one last check... server process has not exited yet...',
        assert server_process.poll() is None
        print 'good'

        print 'now kill server process'
        server_process.terminate()


    def test_server_ssl_cert_reload_no_client_auth(self):
        switch_cert_on_client_num = 9

        # use tempfiles to contain the client's cert plus intermediate
        # cert from the two cert hierarchies

        client_key_1_fpath = _get_abs_path('cert_hier_1/intermediate/private/client.key.pem')
        client_chain_1_fp = tempfile.NamedTemporaryFile(suffix='.pem')
        client_chain_1_fp.write('''{}
{}
'''.format(open(_get_abs_path('cert_hier_1/intermediate/certs/client.cert.pem')).read(),
           open(_get_abs_path('cert_hier_1/intermediate/certs/intermediate.cert.pem')).read(),
           ))
        client_chain_1_fp.flush()

        client_key_2_fpath = _get_abs_path('cert_hier_2/intermediate/private/client.key.pem')
        client_chain_2_fp = tempfile.NamedTemporaryFile(suffix='.pem')
        client_chain_2_fp.write('''{}
{}
'''.format(open(_get_abs_path('cert_hier_2/intermediate/certs/client.cert.pem')).read(),
           open(_get_abs_path('cert_hier_2/intermediate/certs/intermediate.cert.pem')).read(),
           ))
        client_chain_2_fp.flush()

        server_ca_1_fpath = _get_abs_path('cert_hier_1/certs/ca.cert.pem')
        server_ca_2_fpath = _get_abs_path('cert_hier_2/certs/ca.cert.pem')

        greeter_client_path = _get_abs_path('greeter_client.py')

        require_client_auth = False

        devnull_fp = open(os.devnull)
        server_process = subprocess.Popen(
            ['python', _get_abs_path('greeter_server.py'),
             str(switch_cert_on_client_num), str(require_client_auth)],
            stdout=devnull_fp, stderr=devnull_fp)
        processes.append(server_process)
        # give server a second to initialize
        import time
        time.sleep(1)

        print 'things should work...',
        stdout, stderr = run_client(
            ['python', greeter_client_path,
             server_ca_1_fpath, client_key_2_fpath, client_chain_2_fp.name])
        assert_client_success(stdout, stderr)
        print 'nice!'

        print 'client should reject server...',
        # fails because client trusts ca1 and so will reject server
        stdout, stderr = run_client(
            ['python', greeter_client_path,
             server_ca_2_fpath, client_key_2_fpath, client_chain_2_fp.name])
        assert_client_failure(stdout, stderr, True)
        # this assert is fragile, i.e., depends on how the core lib logs the error
        assert 'SSL_ERROR_SSL: error:1000007d:SSL routines:OPENSSL_internal:CERTIFICATE_VERIFY_FAILED' in stderr
        print 'good'

        print 'should work again...',
        stdout, stderr = run_client(
            ['python', greeter_client_path,
             server_ca_1_fpath, client_key_2_fpath, client_chain_2_fp.name])
        assert_client_success(stdout, stderr)
        print 'good'

        print 'client should still work even though using key/cert1, because server is told not do do client auth...',
        # fails because client uses key/cert1, but server trusts ca2,
        # so server will reject
        stdout, stderr = run_client(
            ['python', greeter_client_path,
             server_ca_1_fpath, client_key_1_fpath, client_chain_1_fp.name])
        assert_client_success(stdout, stderr)
        print 'good'

        print 'one more time, client should still workd...',
        stdout, stderr = run_client(
            ['python', greeter_client_path,
             server_ca_1_fpath, client_key_1_fpath, client_chain_1_fp.name])
        assert_client_success(stdout, stderr)
        print 'good'

        print 'should work again...',
        stdout, stderr = run_client(
            ['python', greeter_client_path,
             server_ca_1_fpath, client_key_2_fpath, client_chain_2_fp.name])
        assert_client_success(stdout, stderr)
        print 'good'

        print 'should work again...',
        stdout, stderr = run_client(
            ['python', greeter_client_path,
             server_ca_1_fpath, client_key_2_fpath, client_chain_2_fp.name])
        assert_client_success(stdout, stderr)
        print 'good'

        background_client_process = subprocess.Popen(
            ['python', _get_abs_path('greeter_client_2.py'), server_ca_1_fpath,
             client_key_2_fpath, client_chain_2_fp.name, "4"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            env={'GRPC_VERBOSITY': 'ERROR'})
        processes.append(background_client_process)

        time.sleep(1)

        before_switch_timestamp = time.time()

        print
        print 'moment of truth!! client should reject server because the server switch cert...',
        stdout, stderr = run_client(
            ['python', greeter_client_path,
             server_ca_1_fpath, client_key_2_fpath, client_chain_2_fp.name])
        assert_client_failure(stdout, stderr, True)
        print 'NICE!!'
        print

        after_switch_timestamp = time.time()

        print 'now should work again...',
        stdout, stderr = run_client(
            ['python', greeter_client_path,
             server_ca_2_fpath, client_key_1_fpath, client_chain_1_fp.name])
        assert_client_success(stdout, stderr)
        print 'good'

        print 'and again...',
        stdout, stderr = run_client(
            ['python', greeter_client_path,
             server_ca_2_fpath, client_key_1_fpath, client_chain_1_fp.name])
        assert_client_success(stdout, stderr)
        print 'good'

        print 'client should still work... once again, server is not doing client auth...',
        stdout, stderr = run_client(
            ['python', greeter_client_path,
             server_ca_2_fpath, client_key_2_fpath, client_chain_2_fp.name])
        assert_client_success(stdout, stderr)
        print 'good'

        print 'here client should reject server...',
        stdout, stderr = run_client(
            ['python', greeter_client_path,
             server_ca_1_fpath, client_key_2_fpath, client_chain_2_fp.name])
        assert_client_failure(stdout, stderr, True)
        print 'good'

        print 'check background client...',
        stdout, stderr = background_client_process.communicate()
        check_background_client_success(stdout, stderr,
                                        before_switch_timestamp, after_switch_timestamp)
        print 'good'

        print 'one last check... server process has not exited yet...',
        assert server_process.poll() is None
        print 'good'

        print 'now kill server process'
        server_process.terminate()


if __name__ == '__main__':
    unittest.main(verbosity=2)
