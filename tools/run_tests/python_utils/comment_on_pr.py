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

import os
import json
import urllib2


def comment_on_pr(text):
    if 'JENKINS_OAUTH_TOKEN' not in os.environ:
        print 'Missing JENKINS_OAUTH_TOKEN env var: not commenting'
        return
    if 'ghprbPullId' not in os.environ:
        print 'Missing ghprbPullId env var: not commenting'
        return
    req = urllib2.Request(
        url='https://api.github.com/repos/grpc/grpc/issues/%s/comments' %
        os.environ['ghprbPullId'],
        data=json.dumps({
            'body': text
        }),
        headers={
            'Authorization': 'token %s' % os.environ['JENKINS_OAUTH_TOKEN'],
            'Content-Type': 'application/json',
        })
    print urllib2.urlopen(req).read()
