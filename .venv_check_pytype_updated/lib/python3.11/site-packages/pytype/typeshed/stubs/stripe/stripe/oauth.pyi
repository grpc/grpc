from _typeshed import Incomplete

from stripe import api_requestor as api_requestor, connect_api_base as connect_api_base, error as error

class OAuth:
    @staticmethod
    def authorize_url(express: bool = False, **params): ...
    @staticmethod
    def token(api_key: Incomplete | None = None, **params): ...
    @staticmethod
    def deauthorize(api_key: Incomplete | None = None, **params): ...
