from typing import Any

from google.cloud.ndb import model

class BaseStatistic(model.Model):
    STORED_KIND_NAME: str
    bytes: Any
    count: Any
    timestamp: Any

class BaseKindStatistic(BaseStatistic):
    STORED_KIND_NAME: str
    kind_name: Any
    entity_bytes: Any

class GlobalStat(BaseStatistic):
    STORED_KIND_NAME: str
    entity_bytes: Any
    builtin_index_bytes: Any
    builtin_index_count: Any
    composite_index_bytes: Any
    composite_index_count: Any

class NamespaceStat(BaseStatistic):
    STORED_KIND_NAME: str
    subject_namespace: Any
    entity_bytes: Any
    builtin_index_bytes: Any
    builtin_index_count: Any
    composite_index_bytes: Any
    composite_index_count: Any

class KindStat(BaseKindStatistic):
    STORED_KIND_NAME: str
    builtin_index_bytes: Any
    builtin_index_count: Any
    composite_index_bytes: Any
    composite_index_count: Any

class KindRootEntityStat(BaseKindStatistic):
    STORED_KIND_NAME: str

class KindNonRootEntityStat(BaseKindStatistic):
    STORED_KIND_NAME: str

class PropertyTypeStat(BaseStatistic):
    STORED_KIND_NAME: str
    property_type: Any
    entity_bytes: Any
    builtin_index_bytes: Any
    builtin_index_count: Any

class KindPropertyTypeStat(BaseKindStatistic):
    STORED_KIND_NAME: str
    property_type: Any
    builtin_index_bytes: Any
    builtin_index_count: Any

class KindPropertyNameStat(BaseKindStatistic):
    STORED_KIND_NAME: str
    property_name: Any
    builtin_index_bytes: Any
    builtin_index_count: Any

class KindPropertyNamePropertyTypeStat(BaseKindStatistic):
    STORED_KIND_NAME: str
    property_type: Any
    property_name: Any
    builtin_index_bytes: Any
    builtin_index_count: Any

class KindCompositeIndexStat(BaseStatistic):
    STORED_KIND_NAME: str
    index_id: Any
    kind_name: Any

class NamespaceGlobalStat(GlobalStat):
    STORED_KIND_NAME: str

class NamespaceKindStat(KindStat):
    STORED_KIND_NAME: str

class NamespaceKindRootEntityStat(KindRootEntityStat):
    STORED_KIND_NAME: str

class NamespaceKindNonRootEntityStat(KindNonRootEntityStat):
    STORED_KIND_NAME: str

class NamespacePropertyTypeStat(PropertyTypeStat):
    STORED_KIND_NAME: str

class NamespaceKindPropertyTypeStat(KindPropertyTypeStat):
    STORED_KIND_NAME: str

class NamespaceKindPropertyNameStat(KindPropertyNameStat):
    STORED_KIND_NAME: str

class NamespaceKindPropertyNamePropertyTypeStat(KindPropertyNamePropertyTypeStat):
    STORED_KIND_NAME: str

class NamespaceKindCompositeIndexStat(KindCompositeIndexStat):
    STORED_KIND_NAME: str
