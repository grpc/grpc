#!/usr/bin/env python

# run server process, and client process fails/succeeds depending on
# where we are


import traceback
import subprocess
import tempfile
import sys

# tempfiles to contain the client's cert plus intermediate cert from
# the two cert hierarchies

client_key_1_fpath = 'cert_hier_1/intermediate/private/client.key.pem'
client_chain_1_fp = tempfile.NamedTemporaryFile(suffix='.pem')
client_chain_1_fp.write('''{}
{}
'''.format(open('cert_hier_1/intermediate/certs/client.cert.pem').read(),
           open('cert_hier_1/intermediate/certs/intermediate.cert.pem').read(),
           ))
client_chain_1_fp.flush()

client_key_2_fpath = 'cert_hier_2/intermediate/private/client.key.pem'
client_chain_2_fp = tempfile.NamedTemporaryFile(suffix='.pem')
client_chain_2_fp.write('''{}
{}
'''.format(open('cert_hier_2/intermediate/certs/client.cert.pem').read(),
           open('cert_hier_2/intermediate/certs/intermediate.cert.pem').read(),
           ))
client_chain_2_fp.flush()

import pdb
import os

switch_creds_on_client_num = 7

devnull_fp = open(os.devnull)
server_process = subprocess.Popen(
    ['python', 'greeter_server_with_server_creds_reload.py',
     str(switch_creds_on_client_num)],
    stdout=devnull_fp, stderr=devnull_fp)
# give server a second to initialize
import time
time.sleep(1)

server_ca_1_fpath = 'cert_hier_1/certs/ca.cert.pem'
server_ca_2_fpath = 'cert_hier_2/certs/ca.cert.pem'


def run_client(cmd):
    # run the command, wait for it to exit, and return its stdout and
    # stderr. if there's any error in running the command, then None
    # is returned for both
    try:
        p = subprocess.Popen(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        return p.communicate()
    except:
        pass
    return None, None


# stdout and stderr when the client successfully performs the rpc
def assert_client_success(stdout, stderr):
    assert stdout == 'Greeter client received: Hello, you!\n', 'unexpected stdout: [{}]'.format(stdout)
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

# for stderr when the client fails we'll have to do some string search

cmd_for_cert_hier_1 = ['python', 'greeter_client.py',
                       server_ca_1_fpath, client_key_1_fpath,
                       client_chain_1_fp.name]

failed = True

try:
    print 'things should work...',
    stdout, stderr = run_client(cmd_for_cert_hier_1)
    assert_client_success(stdout, stderr)
    print 'nice!'

    print 'client should reject server...',
    # fails because client uses ca2 and so will reject server
    stdout, stderr = run_client(
        ['python', 'greeter_client.py',
         server_ca_2_fpath, client_key_1_fpath, client_chain_1_fp.name])
    assert_client_failure(stdout, stderr, True)
    assert 'SSL_ERROR_SSL: error:1000007d:SSL routines:OPENSSL_internal:CERTIFICATE_VERIFY_FAILED' in stderr
    print 'good'

    print 'should work again...',
    stdout, stderr = run_client(cmd_for_cert_hier_1)
    assert_client_success(stdout, stderr)
    print 'good'

    print 'client should be rejected by server...',
    # fails because client key/cert2, so server will reject
    stdout, stderr = run_client(
        ['python', 'greeter_client.py',
         server_ca_1_fpath, client_key_2_fpath, client_chain_2_fp.name])
    assert_client_failure(stdout, stderr, False)
    print 'good'

    print 'should work again...',
    stdout, stderr = run_client(cmd_for_cert_hier_1)
    assert_client_success(stdout, stderr)
    print 'good'

    print 'should work again...',
    stdout, stderr = run_client(cmd_for_cert_hier_1)
    assert_client_success(stdout, stderr)
    print 'good'

    print
    print 'moment of truth!! client should reject server because the server switch creds...',
    stdout, stderr = run_client(cmd_for_cert_hier_1)
    assert_client_failure(stdout, stderr, True)
    print 'NICE!!'
    print

    print 'now should work again...',
    stdout, stderr = run_client(
        ['python', 'greeter_client.py',
         server_ca_2_fpath, client_key_1_fpath, client_chain_1_fp.name])
    assert_client_success(stdout, stderr)
    print 'good'

    print 'and again...',
    stdout, stderr = run_client(
        ['python', 'greeter_client.py',
         server_ca_2_fpath, client_key_1_fpath, client_chain_1_fp.name])
    assert_client_success(stdout, stderr)
    print 'good'

    print 'client should be rejected by server...',
    stdout, stderr = run_client(
        ['python', 'greeter_client.py',
         server_ca_2_fpath, client_key_2_fpath, client_chain_2_fp.name])
    assert_client_failure(stdout, stderr, False)
    print 'good'

    print 'another check... client should fail...',
    stdout, stderr = run_client(cmd_for_cert_hier_1)
    assert_client_failure(stdout, stderr, True)
    print 'good'

    failed = False
except Exception as exc:
    print
    print 'some exception occured:'
    traceback.print_exc()
finally:
    print 'one last check... server process has not exited yet...',
    assert server_process.poll() is None
    print 'good'
    server_process.terminate()

if failed:
    print 'FAILED'
    sys.exit(1)
else:
    print 'PASSED'
    sys.exit(0)
