from _typeshed import Incomplete

from stripe.stripe_object import StripeObject as StripeObject

class ErrorObject(StripeObject):
    def refresh_from(
        self,
        values,
        api_key: Incomplete | None = None,
        partial: bool = False,
        stripe_version: Incomplete | None = None,
        stripe_account: Incomplete | None = None,
        last_response: Incomplete | None = None,
    ): ...

class OAuthErrorObject(StripeObject):
    def refresh_from(
        self,
        values,
        api_key: Incomplete | None = None,
        partial: bool = False,
        stripe_version: Incomplete | None = None,
        stripe_account: Incomplete | None = None,
        last_response: Incomplete | None = None,
    ): ...
