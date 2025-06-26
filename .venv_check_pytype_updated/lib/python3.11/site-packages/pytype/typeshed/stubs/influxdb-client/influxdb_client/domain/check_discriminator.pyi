from _typeshed import Incomplete

from influxdb_client.domain.check_base import CheckBase

class CheckDiscriminator(CheckBase):
    openapi_types: Incomplete
    attribute_map: Incomplete
    discriminator: Incomplete
    def __init__(
        self,
        id: Incomplete | None = None,
        name: Incomplete | None = None,
        org_id: Incomplete | None = None,
        task_id: Incomplete | None = None,
        owner_id: Incomplete | None = None,
        created_at: Incomplete | None = None,
        updated_at: Incomplete | None = None,
        query: Incomplete | None = None,
        status: Incomplete | None = None,
        description: Incomplete | None = None,
        latest_completed: Incomplete | None = None,
        last_run_status: Incomplete | None = None,
        last_run_error: Incomplete | None = None,
        labels: Incomplete | None = None,
        links: Incomplete | None = None,
    ) -> None: ...
    def to_dict(self): ...
    def to_str(self): ...
    def __eq__(self, other): ...
    def __ne__(self, other): ...
