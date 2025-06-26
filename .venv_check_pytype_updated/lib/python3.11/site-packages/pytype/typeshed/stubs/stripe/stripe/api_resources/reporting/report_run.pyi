from stripe.api_resources.abstract import (
    CreateableAPIResource as CreateableAPIResource,
    ListableAPIResource as ListableAPIResource,
)

class ReportRun(CreateableAPIResource, ListableAPIResource):
    OBJECT_NAME: str
