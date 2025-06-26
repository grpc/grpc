from stripe.api_resources.abstract import CreateableAPIResource as CreateableAPIResource

class ThreeDSecure(CreateableAPIResource):
    OBJECT_NAME: str
    @classmethod
    def class_url(cls): ...
