from _typeshed import Incomplete

from influxdb_client.domain.check_discriminator import CheckDiscriminator

class ThresholdCheck(CheckDiscriminator):
    openapi_types: Incomplete
    attribute_map: Incomplete
    discriminator: Incomplete
    def __init__(
        self,
        type: str = "threshold",
        thresholds: Incomplete | None = None,
        every: Incomplete | None = None,
        offset: Incomplete | None = None,
        tags: Incomplete | None = None,
        status_message_template: Incomplete | None = None,
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
    @property
    def type(self): ...
    @type.setter
    def type(self, type) -> None: ...
    @property
    def thresholds(self): ...
    @thresholds.setter
    def thresholds(self, thresholds) -> None: ...
    @property
    def every(self): ...
    @every.setter
    def every(self, every) -> None: ...
    @property
    def offset(self): ...
    @offset.setter
    def offset(self, offset) -> None: ...
    @property
    def tags(self): ...
    @tags.setter
    def tags(self, tags) -> None: ...
    @property
    def status_message_template(self): ...
    @status_message_template.setter
    def status_message_template(self, status_message_template) -> None: ...
    def to_dict(self): ...
    def to_str(self): ...
    def __eq__(self, other): ...
    def __ne__(self, other): ...
