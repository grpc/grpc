from stripe.api_resources.abstract import ListableAPIResource as ListableAPIResource

class ScheduledQueryRun(ListableAPIResource):
    OBJECT_NAME: str
    @classmethod
    def class_url(cls): ...
