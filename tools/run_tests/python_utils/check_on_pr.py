# Copyright 2018 The gRPC Authors
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

from __future__ import print_function

import datetime
import json
import os
import sys
import time
import traceback

import jwt
import requests

_GITHUB_API_PREFIX = "https://api.github.com"
_GITHUB_REPO = "grpc/grpc"
_GITHUB_APP_ID = 22338
_INSTALLATION_ID = 519109

_ACCESS_TOKEN_CACHE = None
_ACCESS_TOKEN_FETCH_RETRIES = 6
_ACCESS_TOKEN_FETCH_RETRIES_INTERVAL_S = 15

_CHANGE_LABELS = {
    -1: "improvement",
    0: "none",
    1: "low",
    2: "medium",
    3: "high",
}

_INCREASE_DECREASE = {
    -1: "decrease",
    0: "neutral",
    1: "increase",
}


def _jwt_token():
    github_app_key = open(
        os.path.join(
            os.environ["KOKORO_KEYSTORE_DIR"], "73836_grpc_checks_private_key"
        ),
        "rb",
    ).read()
    return jwt.encode(
        {
            "iat": int(time.time()),
            "exp": int(time.time() + 60 * 10),  # expire in 10 minutes
            "iss": _GITHUB_APP_ID,
        },
        github_app_key,
        algorithm="RS256",
    )


def _access_token():
    global _ACCESS_TOKEN_CACHE
    if _ACCESS_TOKEN_CACHE == None or _ACCESS_TOKEN_CACHE["exp"] < time.time():
        for i in range(_ACCESS_TOKEN_FETCH_RETRIES):
            resp = requests.post(
                url="https://api.github.com/app/installations/%s/access_tokens"
                % _INSTALLATION_ID,
                headers={
                    "Authorization": "Bearer %s" % _jwt_token(),
                    "Accept": "application/vnd.github.machine-man-preview+json",
                },
            )

            try:
                _ACCESS_TOKEN_CACHE = {
                    "token": resp.json()["token"],
                    "exp": time.time() + 60,
                }
                break
            except (KeyError, ValueError):
                traceback.print_exc()
                print("HTTP Status %d %s" % (resp.status_code, resp.reason))
                print("Fetch access token from Github API failed:")
                print(resp.text)
                if i != _ACCESS_TOKEN_FETCH_RETRIES - 1:
                    print(
                        "Retrying after %.2f second."
                        % _ACCESS_TOKEN_FETCH_RETRIES_INTERVAL_S
                    )
                    time.sleep(_ACCESS_TOKEN_FETCH_RETRIES_INTERVAL_S)
        else:
            print("error: Unable to fetch access token, exiting...")
            sys.exit(0)

    return _ACCESS_TOKEN_CACHE["token"]


def _call(url, method="GET", json=None):
    if not url.startswith("https://"):
        url = _GITHUB_API_PREFIX + url
    headers = {
        "Authorization": "Bearer %s" % _access_token(),
        "Accept": "application/vnd.github.antiope-preview+json",
    }
    return requests.request(method=method, url=url, headers=headers, json=json)


def _latest_commit():
    resp = _call(
        "/repos/%s/pulls/%s/commits"
        % (_GITHUB_REPO, os.environ["KOKORO_GITHUB_PULL_REQUEST_NUMBER"])
    )
    return resp.json()[-1]


def check_on_pr(name, summary, success=True):
    """Create/Update a check on current pull request.

    The check runs are aggregated by their name, so newer check will update the
    older check with the same name.

    Requires environment variable 'KOKORO_GITHUB_PULL_REQUEST_NUMBER' to indicate which pull request
    should be updated.

    Args:
      name: The name of the check.
      summary: A str in Markdown to be used as the detail information of the check.
      success: A bool indicates whether the check is succeed or not.
    """
    if "KOKORO_GIT_COMMIT" not in os.environ:
        print("Missing KOKORO_GIT_COMMIT env var: not checking")
        return
    if "KOKORO_KEYSTORE_DIR" not in os.environ:
        print("Missing KOKORO_KEYSTORE_DIR env var: not checking")
        return
    if "KOKORO_GITHUB_PULL_REQUEST_NUMBER" not in os.environ:
        print("Missing KOKORO_GITHUB_PULL_REQUEST_NUMBER env var: not checking")
        return
    MAX_SUMMARY_LEN = 65400
    if len(summary) > MAX_SUMMARY_LEN:
        # Drop some hints to the log should someone come looking for what really happened!
        print("Clipping too long summary")
        print(summary)
        summary = summary[:MAX_SUMMARY_LEN] + "\n\n\n... CLIPPED (too long)"
    completion_time = (
        str(datetime.datetime.utcnow().replace(microsecond=0).isoformat()) + "Z"
    )
    resp = _call(
        "/repos/%s/check-runs" % _GITHUB_REPO,
        method="POST",
        json={
            "name": name,
            "head_sha": os.environ["KOKORO_GIT_COMMIT"],
            "status": "completed",
            "completed_at": completion_time,
            "conclusion": "success" if success else "failure",
            "output": {
                "title": name,
                "summary": summary,
            },
        },
    )
    print(
        "Result of Creating/Updating Check on PR:",
        json.dumps(resp.json(), indent=2),
    )


def label_significance_on_pr(name, change, labels=_CHANGE_LABELS):
    """Add a label to the PR indicating the significance of the check.

    Requires environment variable 'KOKORO_GITHUB_PULL_REQUEST_NUMBER' to indicate which pull request
    should be updated.

    Args:
      name: The name of the label.
      value: A str in Markdown to be used as the detail information of the label.
    """
    if change < min(list(labels.keys())):
        change = min(list(labels.keys()))
    if change > max(list(labels.keys())):
        change = max(list(labels.keys()))
    value = labels[change]
    if "KOKORO_GIT_COMMIT" not in os.environ:
        print("Missing KOKORO_GIT_COMMIT env var: not checking")
        return
    if "KOKORO_KEYSTORE_DIR" not in os.environ:
        print("Missing KOKORO_KEYSTORE_DIR env var: not checking")
        return
    if "KOKORO_GITHUB_PULL_REQUEST_NUMBER" not in os.environ:
        print("Missing KOKORO_GITHUB_PULL_REQUEST_NUMBER env var: not checking")
        return
    existing = _call(
        "/repos/%s/issues/%s/labels"
        % (_GITHUB_REPO, os.environ["KOKORO_GITHUB_PULL_REQUEST_NUMBER"]),
        method="GET",
    ).json()
    print("Result of fetching labels on PR:", existing)
    new = [x["name"] for x in existing if not x["name"].startswith(name + "/")]
    new.append(name + "/" + value)
    resp = _call(
        "/repos/%s/issues/%s/labels"
        % (_GITHUB_REPO, os.environ["KOKORO_GITHUB_PULL_REQUEST_NUMBER"]),
        method="PUT",
        json=new,
    )
    print("Result of setting labels on PR:", resp.text)


def label_increase_decrease_on_pr(name, change, significant):
    if change <= -significant:
        label_significance_on_pr(name, -1, _INCREASE_DECREASE)
    elif change >= significant:
        label_significance_on_pr(name, 1, _INCREASE_DECREASE)
    else:
        label_significance_on_pr(name, 0, _INCREASE_DECREASE)
