#!/usr/bin/env python
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
"""Run interop (cross-language) tests in parallel."""

from __future__ import print_function

import argparse
import atexit
import itertools
import json
import multiprocessing
import os
import re
import subprocess
import sys
import tempfile
import time
import uuid
import six
import traceback

import python_utils.dockerjob as dockerjob
import python_utils.jobset as jobset
import python_utils.report_utils as report_utils

# Docker doesn't clean up after itself, so we do it on exit.
atexit.register(lambda: subprocess.call(['stty', 'echo']))

ROOT = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), '../..'))
os.chdir(ROOT)

_FALLBACK_SERVER_PORT = 443
_BALANCER_SERVER_PORT = 12000
_BACKEND_SERVER_PORT = 8080

_TEST_TIMEOUT = 30

_FAKE_SERVERS_SAFENAME = 'fake_servers'

# Use a name that's verified by the test certs
_SERVICE_NAME = 'server.test.google.fr'


class CXXLanguage:

    def __init__(self):
        self.client_cwd = '/var/local/git/grpc'
        self.safename = 'cxx'

    def client_cmd(self, args):
        return ['bins/opt/interop_client'] + args

    def global_env(self):
        # 1) Set c-ares as the resolver, to
        #    enable grpclb.
        # 2) Turn on verbose logging.
        # 3) Set the ROOTS_PATH env variable
        #    to the test CA in order for
        #    GoogleDefaultCredentials to be
        #    able to use the test CA.
        return {
            'GRPC_DNS_RESOLVER':
                'ares',
            'GRPC_VERBOSITY':
                'DEBUG',
            'GRPC_TRACE':
                'client_channel,glb',
            'GRPC_DEFAULT_SSL_ROOTS_FILE_PATH':
                '/var/local/git/grpc/src/core/tsi/test_creds/ca.pem',
        }

    def __str__(self):
        return 'c++'


class JavaLanguage:

    def __init__(self):
        self.client_cwd = '/var/local/git/grpc-java'
        self.safename = str(self)

    def client_cmd(self, args):
        # Take necessary steps to import our test CA into
        # the set of test CA's that the Java runtime of the
        # docker container will pick up, so that
        # Java GoogleDefaultCreds can use it.
        pem_to_der_cmd = ('openssl x509 -outform der '
                          '-in /external_mount/src/core/tsi/test_creds/ca.pem '
                          '-out /tmp/test_ca.der')
        keystore_import_cmd = (
            'keytool -import '
            '-keystore /usr/lib/jvm/java-8-oracle/jre/lib/security/cacerts '
            '-file /tmp/test_ca.der '
            '-deststorepass changeit '
            '-noprompt')
        return [
            'bash', '-c',
            ('{pem_to_der_cmd} && '
             '{keystore_import_cmd} && '
             './run-test-client.sh {java_client_args}').format(
                 pem_to_der_cmd=pem_to_der_cmd,
                 keystore_import_cmd=keystore_import_cmd,
                 java_client_args=' '.join(args))
        ]

    def global_env(self):
        # 1) Enable grpclb
        # 2) Enable verbose logging
        return {
            'JAVA_OPTS': (
                '-Dio.grpc.internal.DnsNameResolverProvider.enable_grpclb=true '
                '-Djava.util.logging.config.file=/var/local/grpc_java_logging/logconf.txt'
            )
        }

    def __str__(self):
        return 'java'


class GoLanguage:

    def __init__(self):
        self.client_cwd = '/go/src/google.golang.org/grpc/interop/client'
        self.safename = str(self)

    def client_cmd(self, args):
        # Copy the test CA file into the path that
        # the Go runtime in the docker container will use, so
        # that Go's GoogleDefaultCredentials can use it.
        # See https://golang.org/src/crypto/x509/root_linux.go.
        return [
            'bash', '-c',
            ('cp /external_mount/src/core/tsi/test_creds/ca.pem '
             '/etc/ssl/certs/ca-certificates.crt && '
             '/go/bin/client {go_client_args}').format(
                 go_client_args=' '.join(args))
        ]

    def global_env(self):
        return {
            'GRPC_GO_LOG_VERBOSITY_LEVEL': '3',
            'GRPC_GO_LOG_SEVERITY_LEVEL': 'INFO'
        }

    def __str__(self):
        return 'go'


_LANGUAGES = {
    'c++': CXXLanguage(),
    'go': GoLanguage(),
    'java': JavaLanguage(),
}


def docker_run_cmdline(cmdline, image, docker_args, cwd, environ=None):
    """Wraps given cmdline array to create 'docker run' cmdline from it."""
    # turn environ into -e docker args
    docker_cmdline = 'docker run -i --rm=true'.split()
    if environ:
        for k, v in environ.items():
            docker_cmdline += ['-e', '%s=%s' % (k, v)]
    return docker_cmdline + ['-w', cwd] + docker_args + [image] + cmdline


def _job_kill_handler(job):
    assert job._spec.container_name
    dockerjob.docker_kill(job._spec.container_name)


def transport_security_to_args(transport_security):
    args = []
    if transport_security == 'tls':
        args += ['--use_tls=true']
    elif transport_security == 'alts':
        args += ['--use_tls=false', '--use_alts=true']
    elif transport_security == 'insecure':
        args += ['--use_tls=false']
    elif transport_security == 'google_default_credentials':
        args += ['--custom_credentials_type=google_default_credentials']
    else:
        print('Invalid transport security option.')
        sys.exit(1)
    return args


def lb_client_interop_jobspec(language,
                              dns_server_ip,
                              docker_image,
                              transport_security='tls'):
    """Runs a gRPC client under test in a docker container"""
    interop_only_options = [
        '--server_host=%s' % _SERVICE_NAME,
        '--server_port=%d' % _FALLBACK_SERVER_PORT
    ] + transport_security_to_args(transport_security)
    # Don't set the server host override in any client;
    # Go and Java default to no override.
    # We're using a DNS server so there's no need.
    if language.safename == 'c++':
        interop_only_options += ['--server_host_override=""']
    # Don't set --use_test_ca; we're configuring
    # clients to use test CA's via alternate means.
    interop_only_options += ['--use_test_ca=false']
    client_args = language.client_cmd(interop_only_options)
    container_name = dockerjob.random_name('lb_interop_client_%s' %
                                           language.safename)
    docker_cmdline = docker_run_cmdline(
        client_args,
        environ=language.global_env(),
        image=docker_image,
        cwd=language.client_cwd,
        docker_args=[
            '--dns=%s' % dns_server_ip,
            '--net=host',
            '--name=%s' % container_name,
            '-v',
            '{grpc_grpc_root_dir}:/external_mount:ro'.format(
                grpc_grpc_root_dir=ROOT),
        ])
    jobset.message('IDLE',
                   'docker_cmdline:\b|%s|' % ' '.join(docker_cmdline),
                   do_newline=True)
    test_job = jobset.JobSpec(cmdline=docker_cmdline,
                              shortname=('lb_interop_client:%s' % language),
                              timeout_seconds=_TEST_TIMEOUT,
                              kill_handler=_job_kill_handler)
    test_job.container_name = container_name
    return test_job


def fallback_server_jobspec(transport_security, shortname):
    """Create jobspec for running a fallback server"""
    cmdline = [
        'bin/server',
        '--port=%d' % _FALLBACK_SERVER_PORT,
    ] + transport_security_to_args(transport_security)
    return grpc_server_in_docker_jobspec(server_cmdline=cmdline,
                                         shortname=shortname)


def backend_server_jobspec(transport_security, shortname):
    """Create jobspec for running a backend server"""
    cmdline = [
        'bin/server',
        '--port=%d' % _BACKEND_SERVER_PORT,
    ] + transport_security_to_args(transport_security)
    return grpc_server_in_docker_jobspec(server_cmdline=cmdline,
                                         shortname=shortname)


def grpclb_jobspec(transport_security, short_stream, backend_addrs, shortname):
    """Create jobspec for running a balancer server"""
    cmdline = [
        'bin/fake_grpclb',
        '--backend_addrs=%s' % ','.join(backend_addrs),
        '--port=%d' % _BALANCER_SERVER_PORT,
        '--short_stream=%s' % short_stream,
        '--service_name=%s' % _SERVICE_NAME,
    ] + transport_security_to_args(transport_security)
    return grpc_server_in_docker_jobspec(server_cmdline=cmdline,
                                         shortname=shortname)


def grpc_server_in_docker_jobspec(server_cmdline, shortname):
    container_name = dockerjob.random_name(shortname)
    environ = {
        'GRPC_GO_LOG_VERBOSITY_LEVEL': '3',
        'GRPC_GO_LOG_SEVERITY_LEVEL': 'INFO ',
    }
    docker_cmdline = docker_run_cmdline(
        server_cmdline,
        cwd='/go',
        image=docker_images.get(_FAKE_SERVERS_SAFENAME),
        environ=environ,
        docker_args=['--name=%s' % container_name])
    jobset.message('IDLE',
                   'docker_cmdline:\b|%s|' % ' '.join(docker_cmdline),
                   do_newline=True)
    server_job = jobset.JobSpec(cmdline=docker_cmdline,
                                shortname=shortname,
                                timeout_seconds=30 * 60)
    server_job.container_name = container_name
    return server_job


def dns_server_in_docker_jobspec(grpclb_ips, fallback_ips, shortname,
                                 cause_no_error_no_data_for_balancer_a_record):
    container_name = dockerjob.random_name(shortname)
    run_dns_server_cmdline = [
        'python',
        'test/cpp/naming/utils/run_dns_server_for_lb_interop_tests.py',
        '--grpclb_ips=%s' % ','.join(grpclb_ips),
        '--fallback_ips=%s' % ','.join(fallback_ips),
    ]
    if cause_no_error_no_data_for_balancer_a_record:
        run_dns_server_cmdline.append(
            '--cause_no_error_no_data_for_balancer_a_record')
    docker_cmdline = docker_run_cmdline(
        run_dns_server_cmdline,
        cwd='/var/local/git/grpc',
        image=docker_images.get(_FAKE_SERVERS_SAFENAME),
        docker_args=['--name=%s' % container_name])
    jobset.message('IDLE',
                   'docker_cmdline:\b|%s|' % ' '.join(docker_cmdline),
                   do_newline=True)
    server_job = jobset.JobSpec(cmdline=docker_cmdline,
                                shortname=shortname,
                                timeout_seconds=30 * 60)
    server_job.container_name = container_name
    return server_job


def build_interop_image_jobspec(lang_safename, basename_prefix='grpc_interop'):
    """Creates jobspec for building interop docker image for a language"""
    tag = '%s_%s:%s' % (basename_prefix, lang_safename, uuid.uuid4())
    env = {
        'INTEROP_IMAGE': tag,
        'BASE_NAME': '%s_%s' % (basename_prefix, lang_safename),
    }
    build_job = jobset.JobSpec(
        cmdline=['tools/run_tests/dockerize/build_interop_image.sh'],
        environ=env,
        shortname='build_docker_%s' % lang_safename,
        timeout_seconds=30 * 60)
    build_job.tag = tag
    return build_job


argp = argparse.ArgumentParser(description='Run interop tests.')
argp.add_argument('-l',
                  '--language',
                  choices=['all'] + sorted(_LANGUAGES),
                  nargs='+',
                  default=['all'],
                  help='Clients to run.')
argp.add_argument('-j', '--jobs', default=multiprocessing.cpu_count(), type=int)
argp.add_argument('-s',
                  '--scenarios_file',
                  default=None,
                  type=str,
                  help='File containing test scenarios as JSON configs.')
argp.add_argument(
    '-n',
    '--scenario_name',
    default=None,
    type=str,
    help=(
        'Useful for manual runs: specify the name of '
        'the scenario to run from scenarios_file. Run all scenarios if unset.'))
argp.add_argument('--cxx_image_tag',
                  default=None,
                  type=str,
                  help=('Setting this skips the clients docker image '
                        'build step and runs the client from the named '
                        'image. Only supports running a one client language.'))
argp.add_argument('--go_image_tag',
                  default=None,
                  type=str,
                  help=('Setting this skips the clients docker image build '
                        'step and runs the client from the named image. Only '
                        'supports running a one client language.'))
argp.add_argument('--java_image_tag',
                  default=None,
                  type=str,
                  help=('Setting this skips the clients docker image build '
                        'step and runs the client from the named image. Only '
                        'supports running a one client language.'))
argp.add_argument(
    '--servers_image_tag',
    default=None,
    type=str,
    help=('Setting this skips the fake servers docker image '
          'build step and runs the servers from the named image.'))
argp.add_argument('--no_skips',
                  default=False,
                  type=bool,
                  nargs='?',
                  const=True,
                  help=('Useful for manual runs. Setting this overrides test '
                        '"skips" configured in test scenarios.'))
argp.add_argument('--verbose',
                  default=False,
                  type=bool,
                  nargs='?',
                  const=True,
                  help='Increase logging.')
args = argp.parse_args()

docker_images = {}

build_jobs = []
if len(args.language) and args.language[0] == 'all':
    languages = _LANGUAGES.keys()
else:
    languages = args.language
for lang_name in languages:
    l = _LANGUAGES[lang_name]
    # First check if a pre-built image was supplied, and avoid
    # rebuilding the particular docker image if so.
    if lang_name == 'c++' and args.cxx_image_tag:
        docker_images[str(l.safename)] = args.cxx_image_tag
    elif lang_name == 'go' and args.go_image_tag:
        docker_images[str(l.safename)] = args.go_image_tag
    elif lang_name == 'java' and args.java_image_tag:
        docker_images[str(l.safename)] = args.java_image_tag
    else:
        # Build the test client in docker and save the fully
        # built image.
        job = build_interop_image_jobspec(l.safename)
        build_jobs.append(job)
        docker_images[str(l.safename)] = job.tag

# First check if a pre-built image was supplied.
if args.servers_image_tag:
    docker_images[_FAKE_SERVERS_SAFENAME] = args.servers_image_tag
else:
    # Build the test servers in docker and save the fully
    # built image.
    job = build_interop_image_jobspec(_FAKE_SERVERS_SAFENAME,
                                      basename_prefix='lb_interop')
    build_jobs.append(job)
    docker_images[_FAKE_SERVERS_SAFENAME] = job.tag

if build_jobs:
    jobset.message('START', 'Building interop docker images.', do_newline=True)
    print('Jobs to run: \n%s\n' % '\n'.join(str(j) for j in build_jobs))
    num_failures, _ = jobset.run(build_jobs,
                                 newline_on_success=True,
                                 maxjobs=args.jobs)
    if num_failures == 0:
        jobset.message('SUCCESS',
                       'All docker images built successfully.',
                       do_newline=True)
    else:
        jobset.message('FAILED',
                       'Failed to build interop docker images.',
                       do_newline=True)
        sys.exit(1)


def wait_until_dns_server_is_up(dns_server_ip):
    """Probes the DNS server until it's running and safe for tests."""
    for i in range(0, 30):
        print('Health check: attempt to connect to DNS server over TCP.')
        tcp_connect_subprocess = subprocess.Popen([
            os.path.join(os.getcwd(), 'test/cpp/naming/utils/tcp_connect.py'),
            '--server_host', dns_server_ip, '--server_port',
            str(53), '--timeout',
            str(1)
        ])
        tcp_connect_subprocess.communicate()
        if tcp_connect_subprocess.returncode == 0:
            print(('Health check: attempt to make an A-record '
                   'query to DNS server.'))
            dns_resolver_subprocess = subprocess.Popen([
                os.path.join(os.getcwd(),
                             'test/cpp/naming/utils/dns_resolver.py'),
                '--qname',
                ('health-check-local-dns-server-is-alive.'
                 'resolver-tests.grpctestingexp'), '--server_host',
                dns_server_ip, '--server_port',
                str(53)
            ],
                                                       stdout=subprocess.PIPE)
            dns_resolver_stdout, _ = dns_resolver_subprocess.communicate()
            if dns_resolver_subprocess.returncode == 0:
                if '123.123.123.123' in dns_resolver_stdout:
                    print(('DNS server is up! '
                           'Successfully reached it over UDP and TCP.'))
                    return
        time.sleep(0.1)
    raise Exception(('Failed to reach DNS server over TCP and/or UDP. '
                     'Exitting without running tests.'))


def shortname(shortname_prefix, shortname, index):
    return '%s_%s_%d' % (shortname_prefix, shortname, index)


def run_one_scenario(scenario_config):
    jobset.message('START', 'Run scenario: %s' % scenario_config['name'])
    server_jobs = {}
    server_addresses = {}
    suppress_server_logs = True
    try:
        backend_addrs = []
        fallback_ips = []
        grpclb_ips = []
        shortname_prefix = scenario_config['name']
        # Start backends
        for i in xrange(len(scenario_config['backend_configs'])):
            backend_config = scenario_config['backend_configs'][i]
            backend_shortname = shortname(shortname_prefix, 'backend_server', i)
            backend_spec = backend_server_jobspec(
                backend_config['transport_sec'], backend_shortname)
            backend_job = dockerjob.DockerJob(backend_spec)
            server_jobs[backend_shortname] = backend_job
            backend_addrs.append(
                '%s:%d' % (backend_job.ip_address(), _BACKEND_SERVER_PORT))
        # Start fallbacks
        for i in xrange(len(scenario_config['fallback_configs'])):
            fallback_config = scenario_config['fallback_configs'][i]
            fallback_shortname = shortname(shortname_prefix, 'fallback_server',
                                           i)
            fallback_spec = fallback_server_jobspec(
                fallback_config['transport_sec'], fallback_shortname)
            fallback_job = dockerjob.DockerJob(fallback_spec)
            server_jobs[fallback_shortname] = fallback_job
            fallback_ips.append(fallback_job.ip_address())
        # Start balancers
        for i in xrange(len(scenario_config['balancer_configs'])):
            balancer_config = scenario_config['balancer_configs'][i]
            grpclb_shortname = shortname(shortname_prefix, 'grpclb_server', i)
            grpclb_spec = grpclb_jobspec(balancer_config['transport_sec'],
                                         balancer_config['short_stream'],
                                         backend_addrs, grpclb_shortname)
            grpclb_job = dockerjob.DockerJob(grpclb_spec)
            server_jobs[grpclb_shortname] = grpclb_job
            grpclb_ips.append(grpclb_job.ip_address())
        # Start DNS server
        dns_server_shortname = shortname(shortname_prefix, 'dns_server', 0)
        dns_server_spec = dns_server_in_docker_jobspec(
            grpclb_ips, fallback_ips, dns_server_shortname,
            scenario_config['cause_no_error_no_data_for_balancer_a_record'])
        dns_server_job = dockerjob.DockerJob(dns_server_spec)
        server_jobs[dns_server_shortname] = dns_server_job
        # Get the IP address of the docker container running the DNS server.
        # The DNS server is running on port 53 of that IP address. Note we will
        # point the DNS resolvers of grpc clients under test to our controlled
        # DNS server by effectively modifying the /etc/resolve.conf "nameserver"
        # lists of their docker containers.
        dns_server_ip = dns_server_job.ip_address()
        wait_until_dns_server_is_up(dns_server_ip)
        # Run clients
        jobs = []
        for lang_name in languages:
            # Skip languages that are known to not currently
            # work for this test.
            if not args.no_skips and lang_name in scenario_config.get(
                    'skip_langs', []):
                jobset.message(
                    'IDLE', 'Skipping scenario: %s for language: %s\n' %
                    (scenario_config['name'], lang_name))
                continue
            lang = _LANGUAGES[lang_name]
            test_job = lb_client_interop_jobspec(
                lang,
                dns_server_ip,
                docker_image=docker_images.get(lang.safename),
                transport_security=scenario_config['transport_sec'])
            jobs.append(test_job)
        jobset.message(
            'IDLE', 'Jobs to run: \n%s\n' % '\n'.join(str(job) for job in jobs))
        num_failures, resultset = jobset.run(jobs,
                                             newline_on_success=True,
                                             maxjobs=args.jobs)
        report_utils.render_junit_xml_report(resultset, 'sponge_log.xml')
        if num_failures:
            suppress_server_logs = False
            jobset.message('FAILED',
                           'Scenario: %s. Some tests failed' %
                           scenario_config['name'],
                           do_newline=True)
        else:
            jobset.message('SUCCESS',
                           'Scenario: %s. All tests passed' %
                           scenario_config['name'],
                           do_newline=True)
        return num_failures
    finally:
        # Check if servers are still running.
        for server, job in server_jobs.items():
            if not job.is_running():
                print('Server "%s" has exited prematurely.' % server)
        suppress_failure = suppress_server_logs and not args.verbose
        dockerjob.finish_jobs([j for j in six.itervalues(server_jobs)],
                              suppress_failure=suppress_failure)


num_failures = 0
with open(args.scenarios_file, 'r') as scenarios_input:
    all_scenarios = json.loads(scenarios_input.read())
    for scenario in all_scenarios:
        if args.scenario_name:
            if args.scenario_name != scenario['name']:
                jobset.message('IDLE',
                               'Skipping scenario: %s' % scenario['name'])
                continue
        num_failures += run_one_scenario(scenario)
if num_failures == 0:
    sys.exit(0)
else:
    sys.exit(1)
