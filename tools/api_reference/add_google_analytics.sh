#!/bin/bash
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

# This script finds all html files in the current directory, and adds the
# GA tracking snippet to them.

read -r -d '' SNIPPET << EOF
<script type="text/javascript">
    (function(i,s,o,g,r,a,m){i['GoogleAnalyticsObject']=r;i[r]=i[r]||function(){
    (i[r].q=i[r].q||[]).push(arguments)},i[r].l=1*new Date();a=s.createElement(o),
    m=s.getElementsByTagName(o)[0];a.async=1;a.src=g;m.parentNode.insertBefore(a,m)
    })(window,document,'script','//www.google-analytics.com/analytics.js','ga');

    ga('create', 'UA-60127042-1', 'auto');
    ga('send', 'pageview');
</script>
EOF

S=$(echo -n "$SNIPPET" | tr '\n' ' ')

while IFS= read -r -d '' M
do
  if grep -q "i,s,o,g,r,a,m" "$M"; then
    :
  else
    sed -i "s_</head>_${S}</head>_" "$M"
  fi
done < <(find . -name \*.html -print0)
