from stripe.api_resources.abstract import ListableAPIResource as ListableAPIResource
from stripe.api_resources.customer import Customer as Customer

class BitcoinReceiver(ListableAPIResource):
    OBJECT_NAME: str
    def instance_url(self): ...
    @classmethod
    def class_url(cls): ...
