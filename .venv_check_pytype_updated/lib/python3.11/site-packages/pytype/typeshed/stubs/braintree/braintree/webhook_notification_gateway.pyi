from typing import Any

from braintree.exceptions.invalid_challenge_error import InvalidChallengeError as InvalidChallengeError
from braintree.exceptions.invalid_signature_error import InvalidSignatureError as InvalidSignatureError
from braintree.util.crypto import Crypto as Crypto
from braintree.util.xml_util import XmlUtil as XmlUtil
from braintree.webhook_notification import WebhookNotification as WebhookNotification

text_type = str

class WebhookNotificationGateway:
    gateway: Any
    config: Any
    def __init__(self, gateway) -> None: ...
    def parse(self, signature, payload): ...
    def verify(self, challenge): ...
