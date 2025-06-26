from _typeshed import Incomplete

from influxdb_client.domain.notification_rule_discriminator import NotificationRuleDiscriminator

class TelegramNotificationRuleBase(NotificationRuleDiscriminator):
    openapi_types: Incomplete
    attribute_map: Incomplete
    discriminator: Incomplete
    def __init__(
        self,
        type: Incomplete | None = None,
        message_template: Incomplete | None = None,
        parse_mode: Incomplete | None = None,
        disable_web_page_preview: Incomplete | None = None,
        latest_completed: Incomplete | None = None,
        last_run_status: Incomplete | None = None,
        last_run_error: Incomplete | None = None,
        id: Incomplete | None = None,
        endpoint_id: Incomplete | None = None,
        org_id: Incomplete | None = None,
        task_id: Incomplete | None = None,
        owner_id: Incomplete | None = None,
        created_at: Incomplete | None = None,
        updated_at: Incomplete | None = None,
        status: Incomplete | None = None,
        name: Incomplete | None = None,
        sleep_until: Incomplete | None = None,
        every: Incomplete | None = None,
        offset: Incomplete | None = None,
        runbook_link: Incomplete | None = None,
        limit_every: Incomplete | None = None,
        limit: Incomplete | None = None,
        tag_rules: Incomplete | None = None,
        description: Incomplete | None = None,
        status_rules: Incomplete | None = None,
        labels: Incomplete | None = None,
        links: Incomplete | None = None,
    ) -> None: ...
    @property
    def type(self): ...
    @type.setter
    def type(self, type) -> None: ...
    @property
    def message_template(self): ...
    @message_template.setter
    def message_template(self, message_template) -> None: ...
    @property
    def parse_mode(self): ...
    @parse_mode.setter
    def parse_mode(self, parse_mode) -> None: ...
    @property
    def disable_web_page_preview(self): ...
    @disable_web_page_preview.setter
    def disable_web_page_preview(self, disable_web_page_preview) -> None: ...
    def to_dict(self): ...
    def to_str(self): ...
    def __eq__(self, other): ...
    def __ne__(self, other): ...
