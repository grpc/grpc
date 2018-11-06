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
    #%s/comments' %
    #os.environ['ghprbPullId'],
    print('commenting on PR')
    req = urllib2.Request(
        #url='https://api.github.com/repos/grpc/grpc/check-runs',
        # data=json.dumps({
        #     "name": "mighty_readme",
        #     "head_sha": '%s' ,
        #     "status": "completed",
        #     "started_at": "2018-05-04T01:14:52Z",
        #     "completed_at": "2018-05-05T01:14:52Z",
        #     "conclusion": "success",
        #     "output": {
        #         "title": "Mighty Readme report",
        #         "summary": "this is summary",
        #         "text": text
        #     }
        # }),
        url='https://api.github.com/repos/grpc/grpc/statuses/%s' % os.environ['KOKORO_GIT_COMMIT'],
        data=json.dumps({
            "state": "error",
            "target_url": "https://example.com/build/status",
            "description": "sdfasfa sdfsdfas asdfsadf asdfasdfasdfasdfasdf asdf asdf asdf sdf asdfasdf asdf asdf sdf asdf asdf asdf asdf asdfasdfasdf asdf asfdThe build succeeded!",
            "context": "microbenchmarks",
            #"name": "mighty_readme",
            # "head_sha": '%s' % os.environ['KOKORO_GIT_COMMIT'],
            # "status": "completed",
            # "started_at": "2018-05-04T01:14:52Z",
            # "completed_at": "2018-05-05T01:14:52Z",
            # "conclusion": "success",
            # "output": {
            #     "title": "Mighty Readme report",
            #     "summary": "this is summary",
            #     "text": text
            # }
        }),
        headers={
            'Authorization': 'token %s' % os.environ['JENKINS_OAUTH_TOKEN'],
            'Content-Type': 'application/json',
            #'Accept': 'application/vnd.github.antiope-preview+json',
        })
    print urllib2.urlopen(req).read()
