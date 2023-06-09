# Copyright 2019 gRPC authors.
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
"""Generate draft and release notes in Markdown from Github PRs.

You'll need a github API token to avoid being rate-limited. See
https://help.github.com/articles/creating-a-personal-access-token-for-the-command-line/

This script collects PRs using "git log X..Y" from local repo where X and Y are
tags or release branch names of previous and current releases respectively.
Typically, notes are generated before the release branch is labelled so Y is
almost always the name of the release branch. X is the previous release branch
if this is not a patch release. Otherwise, it is the previous release tag.
For example, for release v1.17.0, X will be origin/v1.16.x and for release v1.17.3,
X will be v1.17.2. In both cases Y will be origin/v1.17.x.

"""

from collections import defaultdict
import json
import logging

import urllib3

logging.basicConfig(level=logging.WARNING)

content_header = """Draft Release Notes For {version}
--
Final release notes will be generated from the PR titles that have *"release notes:yes"* label. If you have any additional notes please add them below. These will be appended to auto generated release notes. Previous release notes are [here](https://github.com/grpc/grpc/releases).

**Also, look at the PRs listed below against your name.** Please apply the missing labels and make necessary corrections (like fixing the title) to the PR in Github. Final release notes will be generated just before the release on {date}.

Add additional notes not in PRs
--

Core
-


C++
-


C#
-


Objective-C
-


PHP
-


Python
-


Ruby
-


"""

rl_header = """This is release {version} ([{name}](https://github.com/grpc/grpc/blob/master/doc/g_stands_for.md)) of gRPC Core.

For gRPC documentation, see [grpc.io](https://grpc.io/). For previous releases, see [Releases](https://github.com/grpc/grpc/releases).

This release contains refinements, improvements, and bug fixes, with highlights listed below.


"""

HTML_URL = "https://github.com/grpc/grpc/pull/"
API_URL = "https://api.github.com/repos/grpc/grpc/pulls/"


def get_commit_log(prevRelLabel, relBranch):
    """Return the output of 'git log prevRelLabel..relBranch'"""

    import subprocess

    glg_command = [
        "git",
        "log",
        "--pretty=oneline",
        "--committer=GitHub",
        "%s..%s" % (prevRelLabel, relBranch),
    ]
    print(("Running ", " ".join(glg_command)))
    return subprocess.check_output(glg_command).decode("utf-8", "ignore")


def get_pr_data(pr_num):
    """Get the PR data from github. Return 'error' on exception"""
    http = urllib3.PoolManager(
        retries=urllib3.Retry(total=7, backoff_factor=1), timeout=4.0
    )
    url = API_URL + pr_num
    try:
        response = http.request(
            "GET", url, headers={"Authorization": "token %s" % TOKEN}
        )
    except urllib3.exceptions.HTTPError as e:
        print("Request error:", e.reason)
        return "error"
    return json.loads(response.data.decode("utf-8"))


def get_pr_titles(gitLogs):
    import re

    error_count = 0
    # PRs with merge commits
    match_merge_pr = "Merge pull request #(\d+)"
    prlist_merge_pr = re.findall(match_merge_pr, gitLogs, re.MULTILINE)
    print("\nPRs matching 'Merge pull request #<num>':")
    print(prlist_merge_pr)
    print("\n")
    # PRs using Github's squash & merge feature
    match_sq = "\(#(\d+)\)$"
    prlist_sq = re.findall(match_sq, gitLogs, re.MULTILINE)
    print("\nPRs matching '[PR Description](#<num>)$'")
    print(prlist_sq)
    print("\n")
    prlist = prlist_merge_pr + prlist_sq
    langs_pr = defaultdict(list)
    for pr_num in prlist:
        pr_num = str(pr_num)
        print(("---------- getting data for PR " + pr_num))
        pr = get_pr_data(pr_num)
        if pr == "error":
            print(
                ("\n***ERROR*** Error in getting data for PR " + pr_num + "\n")
            )
            error_count += 1
            continue
        rl_no_found = False
        rl_yes_found = False
        lang_found = False
        for label in pr["labels"]:
            if label["name"] == "release notes: yes":
                rl_yes_found = True
            elif label["name"] == "release notes: no":
                rl_no_found = True
            elif label["name"].startswith("lang/"):
                lang_found = True
                lang = label["name"].split("/")[1].lower()
                # lang = lang[0].upper() + lang[1:]
        body = pr["title"]
        if not body.endswith("."):
            body = body + "."
        if not pr["merged_by"]:
            print(("\n***ERROR***: No merge_by found for PR " + pr_num + "\n"))
            error_count += 1
            continue

        prline = (
            "-  " + body + " ([#" + pr_num + "](" + HTML_URL + pr_num + "))"
        )
        detail = "- " + pr["merged_by"]["login"] + "@ " + prline
        print(detail)
        # if no RL label
        if not rl_no_found and not rl_yes_found:
            print(("Release notes label missing for " + pr_num))
            langs_pr["nolabel"].append(detail)
        elif rl_yes_found and not lang_found:
            print(("Lang label missing for " + pr_num))
            langs_pr["nolang"].append(detail)
        elif rl_no_found:
            print(("'Release notes:no' found for " + pr_num))
            langs_pr["notinrel"].append(detail)
        elif rl_yes_found:
            print(
                (
                    "'Release notes:yes' found for "
                    + pr_num
                    + " with lang "
                    + lang
                )
            )
            langs_pr["inrel"].append(detail)
            langs_pr[lang].append(prline)

    return langs_pr, error_count


def write_draft(langs_pr, file, version, date):
    file.write(content_header.format(version=version, date=date))
    file.write("PRs with missing release notes label - please fix in Github\n")
    file.write("---\n")
    file.write("\n")
    if langs_pr["nolabel"]:
        langs_pr["nolabel"].sort()
        file.write("\n".join(langs_pr["nolabel"]))
    else:
        file.write("- None")
    file.write("\n")
    file.write("\n")
    file.write("PRs with missing lang label - please fix in Github\n")
    file.write("---\n")
    file.write("\n")
    if langs_pr["nolang"]:
        langs_pr["nolang"].sort()
        file.write("\n".join(langs_pr["nolang"]))
    else:
        file.write("- None")
    file.write("\n")
    file.write("\n")
    file.write(
        "PRs going into release notes - please check title and fix in Github."
        " Do not edit here.\n"
    )
    file.write("---\n")
    file.write("\n")
    if langs_pr["inrel"]:
        langs_pr["inrel"].sort()
        file.write("\n".join(langs_pr["inrel"]))
    else:
        file.write("- None")
    file.write("\n")
    file.write("\n")
    file.write("PRs not going into release notes\n")
    file.write("---\n")
    file.write("\n")
    if langs_pr["notinrel"]:
        langs_pr["notinrel"].sort()
        file.write("\n".join(langs_pr["notinrel"]))
    else:
        file.write("- None")
    file.write("\n")
    file.write("\n")


def write_rel_notes(langs_pr, file, version, name):
    file.write(rl_header.format(version=version, name=name))
    if langs_pr["core"]:
        file.write("Core\n---\n\n")
        file.write("\n".join(langs_pr["core"]))
        file.write("\n")
        file.write("\n")
    if langs_pr["c++"]:
        file.write("C++\n---\n\n")
        file.write("\n".join(langs_pr["c++"]))
        file.write("\n")
        file.write("\n")
    if langs_pr["c#"]:
        file.write("C#\n---\n\n")
        file.write("\n".join(langs_pr["c#"]))
        file.write("\n")
        file.write("\n")
    if langs_pr["go"]:
        file.write("Go\n---\n\n")
        file.write("\n".join(langs_pr["go"]))
        file.write("\n")
        file.write("\n")
    if langs_pr["Java"]:
        file.write("Java\n---\n\n")
        file.write("\n".join(langs_pr["Java"]))
        file.write("\n")
        file.write("\n")
    if langs_pr["node"]:
        file.write("Node\n---\n\n")
        file.write("\n".join(langs_pr["node"]))
        file.write("\n")
        file.write("\n")
    if langs_pr["objc"]:
        file.write("Objective-C\n---\n\n")
        file.write("\n".join(langs_pr["objc"]))
        file.write("\n")
        file.write("\n")
    if langs_pr["php"]:
        file.write("PHP\n---\n\n")
        file.write("\n".join(langs_pr["php"]))
        file.write("\n")
        file.write("\n")
    if langs_pr["python"]:
        file.write("Python\n---\n\n")
        file.write("\n".join(langs_pr["python"]))
        file.write("\n")
        file.write("\n")
    if langs_pr["ruby"]:
        file.write("Ruby\n---\n\n")
        file.write("\n".join(langs_pr["ruby"]))
        file.write("\n")
        file.write("\n")
    if langs_pr["other"]:
        file.write("Other\n---\n\n")
        file.write("\n".join(langs_pr["other"]))
        file.write("\n")
        file.write("\n")


def build_args_parser():
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument(
        "release_version", type=str, help="New release version e.g. 1.14.0"
    )
    parser.add_argument(
        "release_name", type=str, help="New release name e.g. gladiolus"
    )
    parser.add_argument(
        "release_date", type=str, help="Release date e.g. 7/30/18"
    )
    parser.add_argument(
        "previous_release_label",
        type=str,
        help="Previous release branch/tag e.g. v1.13.x",
    )
    parser.add_argument(
        "release_branch",
        type=str,
        help="Current release branch e.g. origin/v1.14.x",
    )
    parser.add_argument(
        "draft_filename", type=str, help="Name of the draft file e.g. draft.md"
    )
    parser.add_argument(
        "release_notes_filename",
        type=str,
        help="Name of the release notes file e.g. relnotes.md",
    )
    parser.add_argument(
        "--token",
        type=str,
        default="",
        help="GitHub API token to avoid being rate limited",
    )
    return parser


def main():
    import os

    global TOKEN

    parser = build_args_parser()
    args = parser.parse_args()
    version, name, date = (
        args.release_version,
        args.release_name,
        args.release_date,
    )
    start, end = args.previous_release_label, args.release_branch

    TOKEN = args.token
    if TOKEN == "":
        try:
            TOKEN = os.environ["GITHUB_TOKEN"]
        except:
            pass
    if TOKEN == "":
        print(
            "Error: Github API token required. Either include param"
            " --token=<your github token> or set environment variable"
            " GITHUB_TOKEN to your github token"
        )
        return

    langs_pr, error_count = get_pr_titles(get_commit_log(start, end))

    draft_file, rel_file = args.draft_filename, args.release_notes_filename
    filename = os.path.abspath(draft_file)
    if os.path.exists(filename):
        file = open(filename, "r+")
    else:
        file = open(filename, "w")

    file.seek(0)
    write_draft(langs_pr, file, version, date)
    file.truncate()
    file.close()
    print(("\nDraft notes written to " + filename))

    filename = os.path.abspath(rel_file)
    if os.path.exists(filename):
        file = open(filename, "r+")
    else:
        file = open(filename, "w")

    file.seek(0)
    write_rel_notes(langs_pr, file, version, name)
    file.truncate()
    file.close()
    print(("\nRelease notes written to " + filename))
    if error_count > 0:
        print("\n\n*** Errors were encountered. See log. *********\n")


if __name__ == "__main__":
    main()
