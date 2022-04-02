import functions_framework
from google.cloud import pubsub_v1

ps_client = pubsub_v1.PublisherClient()
_PROJECT_ID = "grpc-testing"
_PUBSUB_TOPIC = 'gcf-distribtest-topic'


@functions_framework.http
def test_publish(request):
  topic_path = ps_client.topic_path(_PROJECT_ID, _PUBSUB_TOPIC)
  message = '{"function": "TEST"}'
  message_bytes = message.encode('utf-8')

  for _ in range(100):
    future = ps_client.publish(topic_path, data=message_bytes)

  return "ok", 200
