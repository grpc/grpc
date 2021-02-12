## Overview
This file describes an API for a Pub/Sub (Publish/Subscribe) system. This system
provides a reliable many-to-many communication mechanism between independently
written publishers and subscribers where the publisher publishes messages to
*topics* and each subscriber creates a *subscription* and consumes *messages*
from it.

1.  The Pub/Sub system maintains bindings between topics and subscriptions.
2.  A publisher publishes messages into a topic.
3.  The Pub/Sub system delivers messages from topics into attached
    subscriptions.
4.  A subscriber receives pending messages from its subscription and
    acknowledges each one to the Pub/Sub system.
5.  The Pub/Sub system removes acknowledged messages from that subscription.

## Data Model
The data model consists of the following:

*   **Topic**: A topic is a resource to which messages are published by
    publishers. Topics are named, and the name of the topic is unique within the
    Pub/Sub system.

*   **Subscription**: A subscription records the subscriber's interest in a
    topic. The Pub/Sub system maintains those messages which still need
    to be delivered and acknowledged so that they can retried as needed.
    The set of messages that have not been acknowledged is called the
    subscription backlog.

*   **Message**: A message is a unit of data that flows in the system. It
    contains opaque data from the publisher along with its *attributes*.

*   **Message Attributes** (optional): A set of opaque key-value pairs assigned
    by the publisher to a message. Attributes are delivered unmodified to
    subscribers together with the message data, if there's any.

## Publisher Flow
A publisher publishes messages to the topic using the `Publish` call:

```data
PubsubMessage message;
message.set_data("....");
message.attributes.put("key1", "value1");
PublishRequest request;
request.set_topic("topicName");
request.add_message(message);
Publisher.Publish(request);
```

## Subscriber Flow
The subscriber part of the API is richer than the publisher part and has a
number of concepts for subscription creation and use:

1.  A subscriber (user or process) creates a subscription using the
    `CreateSubscription` call.

2.  A subscriber receives messages in one of two ways: via pull or push.

  *   To receive messages via pull, a subscriber calls the `Pull` method on the
      `Subscriber` to get messages from the subscription. For each individual
      message, the subscriber may use the `ack_id` received in the
      `PullResponse` to `Acknowledge` the message, or modify the *ack deadline*
      with `ModifyAckDeadline`. See the `Subscription.ack_deadline_seconds`
      field documentation for details on the ack deadline behavior. Messages
      must be acknowledged or they will be redelivered in a future `Pull` call.

      **Note:** Messages may be consumed in parallel by multiple processes
      making `Pull` calls to the same subscription; this will result in the set
      of messages from the subscription being split among the processes, each
      process receiving a subset of the messages.

  *   To receive messages via push, the `PushConfig` field must be specified in
      the `Subscription` parameter when creating a subscription, or set with
      `ModifyPushConfig`. The PushConfig specifies an endpoint at which the
      subscriber exposes the `PushEndpointService` or some other handler,
      depending on the endpoint. Messages are received via the
      `ProcessPushMessage` method. The push subscriber responds to the method
      with a result code that indicates one of three things: `Acknowledge` (the
      message has been successfully processed and the Pub/Sub system may delete
      it), `Nack` (the message has been rejected and the Pub/Sub system should
      resend it at a later time).

      **Note:** The endpoint may be a load balancer for better scalability, so
      that multiple processes may handle the message processing load.

Subscription creation:

```data
Subscription subscription;
subscription.set_topic("topicName");
subscription.set_name("subscriptionName");
subscription.push_config().set_push_endpoint("machinename:8888");
Subscriber.CreateSubscription(subscription);
```

Consuming messages via pull:

```data
// The subscription must be created without setting the push_config field.

PullRequest pull_request;
pull_request.set_subscription("subscriptionName");
pull_request.set_return_immediately(false);
pull_request.set_max_messages(10);
while (true) {
  PullResponse pull_response;
  AcknowledgeRequest ack_request;
  ackRequest.set_subscription("subscriptionName");
  if (Subscriber.Pull(pull_request, pull_response) == OK) {
    for (ReceivedMessage received in pull_response.received_messages()) {
      Process(received.message().data());
      ackRequest.add_ack_id(received.ack_id());
    }
  }
  if (ackRequest.ack_ids().size() > 0) {
    Subscriber.Acknowledge(ack_request);
  }
}
```

## Reliability Semantics
When a subscriber successfully creates a subscription using
`Subscriber.CreateSubscription`, it establishes a "subscription point" for
that subscription, no later than the time that `Subscriber.CreateSubscription`
returns. The subscriber is guaranteed to receive any message published after
this subscription point. Note that messages published before the subscription
point may or may not be delivered.

Messages are not delivered in any particular order by the Pub/Sub system.
Furthermore, the system guarantees *at-least-once* delivery of each message
until acknowledged.

## Deletion
Both topics and subscriptions may be deleted.

When a subscription is deleted, all messages are immediately dropped. If it
is a pull subscriber, future pull requests will return NOT_FOUND.
