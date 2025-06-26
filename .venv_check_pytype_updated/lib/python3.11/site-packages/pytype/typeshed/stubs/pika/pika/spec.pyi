import builtins
from _typeshed import Incomplete
from typing import ClassVar, Literal
from typing_extensions import Self, TypeAlias

from .amqp_object import Class, Method, Properties

# Ouch. Since str = bytes at runtime, we need a type alias for "str".
_str: TypeAlias = builtins.str  # noqa: Y042
str = builtins.bytes

PROTOCOL_VERSION: Incomplete
PORT: int
ACCESS_REFUSED: int
CHANNEL_ERROR: int
COMMAND_INVALID: int
CONNECTION_FORCED: int
CONTENT_TOO_LARGE: int
FRAME_BODY: int
FRAME_END: int
FRAME_END_SIZE: int
FRAME_ERROR: int
FRAME_HEADER: int
FRAME_HEADER_SIZE: int
FRAME_HEARTBEAT: int
FRAME_MAX_SIZE: int
FRAME_METHOD: int
FRAME_MIN_SIZE: int
INTERNAL_ERROR: int
INVALID_PATH: int
NOT_ALLOWED: int
NOT_FOUND: int
NOT_IMPLEMENTED: int
NO_CONSUMERS: int
NO_ROUTE: int
PERSISTENT_DELIVERY_MODE: int
PRECONDITION_FAILED: int
REPLY_SUCCESS: int
RESOURCE_ERROR: int
RESOURCE_LOCKED: int
SYNTAX_ERROR: int
TRANSIENT_DELIVERY_MODE: int
UNEXPECTED_FRAME: int

class Connection(Class):
    INDEX: ClassVar[int]

    class Start(Method):
        INDEX: ClassVar[int]
        version_major: Incomplete
        version_minor: Incomplete
        server_properties: Incomplete
        mechanisms: Incomplete
        locales: Incomplete
        def __init__(
            self,
            version_major: int = 0,
            version_minor: int = 9,
            server_properties: Incomplete | None = None,
            mechanisms: _str = "PLAIN",
            locales: _str = "en_US",
        ) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class StartOk(Method):
        INDEX: ClassVar[int]
        client_properties: Incomplete
        mechanism: Incomplete
        response: Incomplete
        locale: Incomplete
        def __init__(
            self,
            client_properties: Incomplete | None = None,
            mechanism: _str = "PLAIN",
            response: Incomplete | None = None,
            locale: _str = "en_US",
        ) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class Secure(Method):
        INDEX: ClassVar[int]
        challenge: Incomplete
        def __init__(self, challenge: Incomplete | None = None) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class SecureOk(Method):
        INDEX: ClassVar[int]
        response: Incomplete
        def __init__(self, response: Incomplete | None = None) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class Tune(Method):
        INDEX: ClassVar[int]
        channel_max: Incomplete
        frame_max: Incomplete
        heartbeat: Incomplete
        def __init__(self, channel_max: int = 0, frame_max: int = 0, heartbeat: int = 0) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class TuneOk(Method):
        INDEX: ClassVar[int]
        channel_max: Incomplete
        frame_max: Incomplete
        heartbeat: Incomplete
        def __init__(self, channel_max: int = 0, frame_max: int = 0, heartbeat: int = 0) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class Open(Method):
        INDEX: ClassVar[int]
        virtual_host: Incomplete
        capabilities: Incomplete
        insist: Incomplete
        def __init__(self, virtual_host: _str = "/", capabilities: _str = "", insist: bool = False) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class OpenOk(Method):
        INDEX: ClassVar[int]
        known_hosts: Incomplete
        def __init__(self, known_hosts: _str = "") -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class Close(Method):
        INDEX: ClassVar[int]
        reply_code: Incomplete
        reply_text: Incomplete
        class_id: Incomplete
        method_id: Incomplete
        def __init__(
            self,
            reply_code: Incomplete | None = None,
            reply_text: _str = "",
            class_id: Incomplete | None = None,
            method_id: Incomplete | None = None,
        ) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class CloseOk(Method):
        INDEX: ClassVar[int]
        def __init__(self) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class Blocked(Method):
        INDEX: ClassVar[int]
        reason: Incomplete
        def __init__(self, reason: _str = "") -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class Unblocked(Method):
        INDEX: ClassVar[int]
        def __init__(self) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class UpdateSecret(Method):
        INDEX: ClassVar[int]
        new_secret: Incomplete
        reason: Incomplete
        def __init__(self, new_secret, reason) -> None: ...
        @property
        def synchronous(self): ...
        mechanisms: Incomplete
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class UpdateSecretOk(Method):
        INDEX: ClassVar[int]
        def __init__(self) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

class Channel(Class):
    INDEX: ClassVar[int]

    class Open(Method):
        INDEX: ClassVar[int]
        out_of_band: Incomplete
        def __init__(self, out_of_band: _str = "") -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class OpenOk(Method):
        INDEX: ClassVar[int]
        channel_id: Incomplete
        def __init__(self, channel_id: _str = "") -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class Flow(Method):
        INDEX: ClassVar[int]
        active: Incomplete
        def __init__(self, active: Incomplete | None = None) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class FlowOk(Method):
        INDEX: ClassVar[int]
        active: Incomplete
        def __init__(self, active: Incomplete | None = None) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class Close(Method):
        INDEX: ClassVar[int]
        reply_code: Incomplete
        reply_text: Incomplete
        class_id: Incomplete
        method_id: Incomplete
        def __init__(
            self,
            reply_code: Incomplete | None = None,
            reply_text: _str = "",
            class_id: Incomplete | None = None,
            method_id: Incomplete | None = None,
        ) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class CloseOk(Method):
        INDEX: ClassVar[int]
        def __init__(self) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

class Access(Class):
    INDEX: ClassVar[int]

    class Request(Method):
        INDEX: ClassVar[int]
        realm: Incomplete
        exclusive: Incomplete
        passive: Incomplete
        active: Incomplete
        write: Incomplete
        read: Incomplete
        def __init__(
            self,
            realm: _str = "/data",
            exclusive: bool = False,
            passive: bool = True,
            active: bool = True,
            write: bool = True,
            read: bool = True,
        ) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class RequestOk(Method):
        INDEX: ClassVar[int]
        ticket: Incomplete
        def __init__(self, ticket: int = 1) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

class Exchange(Class):
    INDEX: ClassVar[int]

    class Declare(Method):
        INDEX: ClassVar[int]
        ticket: Incomplete
        exchange: Incomplete
        type: Incomplete
        passive: Incomplete
        durable: Incomplete
        auto_delete: Incomplete
        internal: Incomplete
        nowait: Incomplete
        arguments: Incomplete
        def __init__(
            self,
            ticket: int = 0,
            exchange: Incomplete | None = None,
            type=...,
            passive: bool = False,
            durable: bool = False,
            auto_delete: bool = False,
            internal: bool = False,
            nowait: bool = False,
            arguments: Incomplete | None = None,
        ) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class DeclareOk(Method):
        INDEX: ClassVar[int]
        def __init__(self) -> None: ...
        @property
        def synchronous(self) -> Literal[False]: ...
        def decode(self, encoded: bytes, offset: int = 0) -> Self: ...
        def encode(self) -> list[bytes]: ...

    class Delete(Method):
        INDEX: ClassVar[int]
        ticket: Incomplete
        exchange: Incomplete
        if_unused: Incomplete
        nowait: Incomplete
        def __init__(
            self, ticket: int = 0, exchange: Incomplete | None = None, if_unused: bool = False, nowait: bool = False
        ) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class DeleteOk(Method):
        INDEX: ClassVar[int]
        def __init__(self) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class Bind(Method):
        INDEX: ClassVar[int]
        ticket: int
        destination: Incomplete | None
        source: Incomplete | None
        routing_key: _str
        nowait: bool
        arguments: Incomplete | None
        def __init__(
            self,
            ticket: int = 0,
            destination: Incomplete | None = None,
            source: Incomplete | None = None,
            routing_key: _str = "",
            nowait: bool = False,
            arguments: Incomplete | None = None,
        ) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class BindOk(Method):
        INDEX: ClassVar[int]
        def __init__(self) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class Unbind(Method):
        INDEX: ClassVar[int]
        ticket: Incomplete
        destination: Incomplete
        source: Incomplete
        routing_key: Incomplete
        nowait: Incomplete
        arguments: Incomplete
        def __init__(
            self,
            ticket: int = 0,
            destination: Incomplete | None = None,
            source: Incomplete | None = None,
            routing_key: _str = "",
            nowait: bool = False,
            arguments: Incomplete | None = None,
        ) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class UnbindOk(Method):
        INDEX: ClassVar[int]
        def __init__(self) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

class Queue(Class):
    INDEX: ClassVar[int]

    class Declare(Method):
        INDEX: ClassVar[int]
        ticket: Incomplete
        queue: Incomplete
        passive: Incomplete
        durable: Incomplete
        exclusive: Incomplete
        auto_delete: Incomplete
        nowait: Incomplete
        arguments: Incomplete
        def __init__(
            self,
            ticket: int = 0,
            queue: _str = "",
            passive: bool = False,
            durable: bool = False,
            exclusive: bool = False,
            auto_delete: bool = False,
            nowait: bool = False,
            arguments: Incomplete | None = None,
        ) -> None: ...
        @property
        def synchronous(self) -> Literal[True]: ...
        def decode(self, encoded: bytes, offset: int = 0) -> Self: ...
        def encode(self) -> list[bytes]: ...

    class DeclareOk(Method):
        INDEX: ClassVar[int]
        queue: _str
        message_count: int
        consumer_count: int
        def __init__(self, queue: _str, message_count: int, consumer_count: int) -> None: ...
        @property
        def synchronous(self) -> Literal[False]: ...
        def decode(self, encoded: bytes, offset: int = 0) -> Self: ...
        def encode(self) -> list[bytes]: ...

    class Bind(Method):
        INDEX: ClassVar[int]
        ticket: Incomplete
        queue: Incomplete
        exchange: Incomplete
        routing_key: Incomplete
        nowait: Incomplete
        arguments: Incomplete
        def __init__(
            self,
            ticket: int = 0,
            queue: _str = "",
            exchange: Incomplete | None = None,
            routing_key: _str = "",
            nowait: bool = False,
            arguments: Incomplete | None = None,
        ) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class BindOk(Method):
        INDEX: ClassVar[int]
        def __init__(self) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class Purge(Method):
        INDEX: ClassVar[int]
        ticket: Incomplete
        queue: Incomplete
        nowait: Incomplete
        def __init__(self, ticket: int = 0, queue: _str = "", nowait: bool = False) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class PurgeOk(Method):
        INDEX: ClassVar[int]
        message_count: Incomplete
        def __init__(self, message_count: Incomplete | None = None) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class Delete(Method):
        INDEX: ClassVar[int]
        ticket: Incomplete
        queue: Incomplete
        if_unused: Incomplete
        if_empty: Incomplete
        nowait: Incomplete
        def __init__(
            self, ticket: int = 0, queue: _str = "", if_unused: bool = False, if_empty: bool = False, nowait: bool = False
        ) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class DeleteOk(Method):
        INDEX: ClassVar[int]
        message_count: Incomplete
        def __init__(self, message_count: Incomplete | None = None) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class Unbind(Method):
        INDEX: ClassVar[int]
        ticket: Incomplete
        queue: Incomplete
        exchange: Incomplete
        routing_key: Incomplete
        arguments: Incomplete
        def __init__(
            self,
            ticket: int = 0,
            queue: _str = "",
            exchange: Incomplete | None = None,
            routing_key: _str = "",
            arguments: Incomplete | None = None,
        ) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class UnbindOk(Method):
        INDEX: ClassVar[int]
        def __init__(self) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

class Basic(Class):
    INDEX: ClassVar[int]

    class Qos(Method):
        INDEX: ClassVar[int]
        prefetch_size: Incomplete
        prefetch_count: Incomplete
        global_qos: Incomplete
        def __init__(self, prefetch_size: int = 0, prefetch_count: int = 0, global_qos: bool = False) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class QosOk(Method):
        INDEX: ClassVar[int]
        def __init__(self) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class Consume(Method):
        INDEX: ClassVar[int]
        ticket: Incomplete
        queue: Incomplete
        consumer_tag: Incomplete
        no_local: Incomplete
        no_ack: Incomplete
        exclusive: Incomplete
        nowait: Incomplete
        arguments: Incomplete
        def __init__(
            self,
            ticket: int = 0,
            queue: _str = "",
            consumer_tag: _str = "",
            no_local: bool = False,
            no_ack: bool = False,
            exclusive: bool = False,
            nowait: bool = False,
            arguments: Incomplete | None = None,
        ) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class ConsumeOk(Method):
        INDEX: ClassVar[int]
        consumer_tag: Incomplete
        def __init__(self, consumer_tag: Incomplete | None = None) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class Cancel(Method):
        INDEX: ClassVar[int]
        consumer_tag: Incomplete
        nowait: Incomplete
        def __init__(self, consumer_tag: Incomplete | None = None, nowait: bool = False) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class CancelOk(Method):
        INDEX: ClassVar[int]
        consumer_tag: Incomplete
        def __init__(self, consumer_tag: Incomplete | None = None) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class Publish(Method):
        INDEX: ClassVar[int]
        ticket: Incomplete
        exchange: Incomplete
        routing_key: Incomplete
        mandatory: Incomplete
        immediate: Incomplete
        def __init__(
            self, ticket: int = 0, exchange: _str = "", routing_key: _str = "", mandatory: bool = False, immediate: bool = False
        ) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class Return(Method):
        INDEX: ClassVar[int]
        reply_code: Incomplete
        reply_text: Incomplete
        exchange: Incomplete
        routing_key: Incomplete
        def __init__(
            self,
            reply_code: Incomplete | None = None,
            reply_text: _str = "",
            exchange: Incomplete | None = None,
            routing_key: Incomplete | None = None,
        ) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class Deliver(Method):
        INDEX: ClassVar[int]
        consumer_tag: Incomplete
        delivery_tag: Incomplete
        redelivered: Incomplete
        exchange: Incomplete
        routing_key: Incomplete
        def __init__(
            self,
            consumer_tag: Incomplete | None = None,
            delivery_tag: Incomplete | None = None,
            redelivered: bool = False,
            exchange: Incomplete | None = None,
            routing_key: Incomplete | None = None,
        ) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class Get(Method):
        INDEX: ClassVar[int]
        ticket: Incomplete
        queue: Incomplete
        no_ack: Incomplete
        def __init__(self, ticket: int = 0, queue: _str = "", no_ack: bool = False) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class GetOk(Method):
        INDEX: ClassVar[int]
        delivery_tag: Incomplete
        redelivered: Incomplete
        exchange: Incomplete
        routing_key: Incomplete
        message_count: Incomplete
        def __init__(
            self,
            delivery_tag: Incomplete | None = None,
            redelivered: bool = False,
            exchange: Incomplete | None = None,
            routing_key: Incomplete | None = None,
            message_count: Incomplete | None = None,
        ) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class GetEmpty(Method):
        INDEX: ClassVar[int]
        cluster_id: Incomplete
        def __init__(self, cluster_id: _str = "") -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class Ack(Method):
        INDEX: ClassVar[int]
        delivery_tag: Incomplete
        multiple: Incomplete
        def __init__(self, delivery_tag: int = 0, multiple: bool = False) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class Reject(Method):
        INDEX: ClassVar[int]
        delivery_tag: Incomplete
        requeue: Incomplete
        def __init__(self, delivery_tag: Incomplete | None = None, requeue: bool = True) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class RecoverAsync(Method):
        INDEX: ClassVar[int]
        requeue: Incomplete
        def __init__(self, requeue: bool = False) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class Recover(Method):
        INDEX: ClassVar[int]
        requeue: Incomplete
        def __init__(self, requeue: bool = False) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class RecoverOk(Method):
        INDEX: ClassVar[int]
        def __init__(self) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class Nack(Method):
        INDEX: ClassVar[int]
        delivery_tag: Incomplete
        multiple: Incomplete
        requeue: Incomplete
        def __init__(self, delivery_tag: int = 0, multiple: bool = False, requeue: bool = True) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

class Tx(Class):
    INDEX: ClassVar[int]

    class Select(Method):
        INDEX: ClassVar[int]
        def __init__(self) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class SelectOk(Method):
        INDEX: ClassVar[int]
        def __init__(self) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class Commit(Method):
        INDEX: ClassVar[int]
        def __init__(self) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class CommitOk(Method):
        INDEX: ClassVar[int]
        def __init__(self) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class Rollback(Method):
        INDEX: ClassVar[int]
        def __init__(self) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class RollbackOk(Method):
        INDEX: ClassVar[int]
        def __init__(self) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

class Confirm(Class):
    INDEX: ClassVar[int]

    class Select(Method):
        INDEX: ClassVar[int]
        nowait: Incomplete
        def __init__(self, nowait: bool = False) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

    class SelectOk(Method):
        INDEX: ClassVar[int]
        def __init__(self) -> None: ...
        @property
        def synchronous(self): ...
        def decode(self, encoded, offset: int = 0): ...
        def encode(self): ...

class BasicProperties(Properties):
    CLASS: Incomplete
    INDEX: ClassVar[int]
    FLAG_CONTENT_TYPE: Incomplete
    FLAG_CONTENT_ENCODING: Incomplete
    FLAG_HEADERS: Incomplete
    FLAG_DELIVERY_MODE: Incomplete
    FLAG_PRIORITY: Incomplete
    FLAG_CORRELATION_ID: Incomplete
    FLAG_REPLY_TO: Incomplete
    FLAG_EXPIRATION: Incomplete
    FLAG_MESSAGE_ID: Incomplete
    FLAG_TIMESTAMP: Incomplete
    FLAG_TYPE: Incomplete
    FLAG_USER_ID: Incomplete
    FLAG_APP_ID: Incomplete
    FLAG_CLUSTER_ID: Incomplete
    content_type: Incomplete
    content_encoding: Incomplete
    headers: Incomplete
    delivery_mode: Incomplete
    priority: Incomplete
    correlation_id: Incomplete
    reply_to: Incomplete
    expiration: Incomplete
    message_id: Incomplete
    timestamp: Incomplete
    type: Incomplete
    user_id: Incomplete
    app_id: Incomplete
    cluster_id: Incomplete
    def __init__(
        self,
        content_type: Incomplete | None = None,
        content_encoding: Incomplete | None = None,
        headers: Incomplete | None = None,
        delivery_mode: Incomplete | None = None,
        priority: Incomplete | None = None,
        correlation_id: Incomplete | None = None,
        reply_to: Incomplete | None = None,
        expiration: Incomplete | None = None,
        message_id: Incomplete | None = None,
        timestamp: Incomplete | None = None,
        type: Incomplete | None = None,
        user_id: Incomplete | None = None,
        app_id: Incomplete | None = None,
        cluster_id: Incomplete | None = None,
    ) -> None: ...
    def decode(self, encoded, offset: int = 0): ...
    def encode(self): ...

methods: Incomplete
props: Incomplete

def has_content(methodNumber): ...
