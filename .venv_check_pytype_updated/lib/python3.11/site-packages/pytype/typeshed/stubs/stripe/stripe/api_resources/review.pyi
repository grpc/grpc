from stripe.api_resources.abstract import ListableAPIResource as ListableAPIResource, custom_method as custom_method

class Review(ListableAPIResource):
    OBJECT_NAME: str
    def approve(self, idempotency_key: str | None = None, **params): ...
