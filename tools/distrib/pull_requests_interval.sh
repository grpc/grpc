#!/bin/bash
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

if [ "x$1" = "x" ] ; then
  echo "Usage: $0 <first ref> [second ref]"
  exit 1
else
  first=$1
fi

if [ -n $2 ] ; then
  second=HEAD
fi

if [ -e ~/github-credentials.vars ] ; then
  . ~/github-credentials.vars
fi

if [ "x$github_client_id" = "x" ] || [ "x$github_client_secret" = "x" ] ; then
  echo "Warning: you don't have github credentials set."
  echo
  echo "You may end up exceeding guest quota quickly."
  echo "You can create an application for yourself,"
  echo "and get its credentials. Go to"
  echo
  echo "  https://github.com/settings/developers"
  echo
  echo "and click 'Register a new application'."
  echo
  echo "From the application's information, copy/paste"
  echo "its Client ID and Client Secret, into the file"
  echo
  echo "  ~/github-credentials.vars"
  echo
  echo "with the following format:"
  echo
  echo "github_client_id=0123456789abcdef0123"
  echo "github_client_secret=0123456789abcdef0123456789abcdef"
  echo
  echo
  addendum=""
else
  addendum="?client_id=$github_client_id&client_secret=$github_client_secret"
fi

unset notfirst
echo "["
git log --pretty=oneline $1..$2 |
  grep '[^ ]\+ Merge pull request #[0-9]\{4,6\} ' |
  cut -f 2 -d# |
  cut -f 1 -d\  |
  sort -u |
  while read id ; do
    if [ "x$notfirst" = "x" ] ; then
      notfirst=true
    else
      echo ","
    fi
    echo -n "  {\"url\": \"https://github.com/grpc/grpc/pull/$id\","
    out=`mktemp`
    curl -s "https://api.github.com/repos/grpc/grpc/pulls/$id$addendum" > $out
    echo -n " "`grep '"title"' $out`
    echo -n " "`grep '"login"' $out | head -1`
    echo -n "  \"pr\": $id }"
    rm $out
  done
echo
echo "]"
