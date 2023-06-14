# Copyright 2022 gRPC authors.
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

import functions_framework
from google.cloud import pubsub_v1

ps_client = pubsub_v1.PublisherClient()
_PROJECT_ID = "grpc-testing"
_PUBSUB_TOPIC = "gcf-distribtest-topic"


@functions_framework.http
def test_publish(request):
    topic_path = ps_client.topic_path(_PROJECT_ID, _PUBSUB_TOPIC)
    message = '{"function": "TEST"}'
    message_bytes = message.encode("utf-8")

    for _ in range(100):
        future = ps_client.publish(topic_path, data=message_bytes)

    return "ok", 200
