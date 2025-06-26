from boto.connection import AWSAuthConnection
from boto.regioninfo import RegionInfo

from .connection import S3Connection

class S3RegionInfo(RegionInfo):
    def connect(
        self,
        name: str | None = ...,
        endpoint: str | None = ...,
        connection_cls: type[AWSAuthConnection] | None = ...,
        **kw_params,
    ) -> S3Connection: ...

def regions() -> list[S3RegionInfo]: ...
def connect_to_region(region_name: str, **kw_params): ...
