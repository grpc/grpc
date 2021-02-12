package java

const timestampConstTpl = `{{ $f := .Field }}{{ $r := .Rules -}}
{{- if $r.Const }}
		private final com.google.protobuf.Timestamp {{ constantName . "Const" }} = {{ tsLit $r.GetConst }};
{{- end -}}
{{- if $r.Lt }}
		private final com.google.protobuf.Timestamp {{ constantName . "Lt" }} = {{ tsLit $r.GetLt }};
{{- end -}}
{{- if $r.Lte }}
		private final com.google.protobuf.Timestamp {{ constantName . "Lte" }} = {{ tsLit $r.Lte }};
{{- end -}}
{{- if $r.Gt }}
		private final com.google.protobuf.Timestamp {{ constantName . "Gt" }} = {{ tsLit $r.GetGt }};
{{- end -}}
{{- if $r.Gte }}
		private final com.google.protobuf.Timestamp {{ constantName . "Gte" }} = {{ tsLit $r.GetGte }};
{{- end -}}
{{- if $r.Within }}
		private final com.google.protobuf.Duration {{ constantName . "Within" }} = {{ durLit $r.GetWithin }};
{{- end -}}`

const timestampTpl = `{{ $f := .Field }}{{ $r := .Rules -}}
{{- template "required" . -}}

{{- if $r.Const }}
			if ({{ hasAccessor . }}) io.envoyproxy.pgv.ConstantValidation.constant("{{ $f.FullyQualifiedName }}", {{ accessor . }}, {{ constantName . "Const" }});
{{- end -}}
{{- if and (or $r.Lt $r.Lte) (or $r.Gt $r.Gte)}}
			if ({{ hasAccessor . }}) io.envoyproxy.pgv.ComparativeValidation.range("{{ $f.FullyQualifiedName }}", {{ accessor . }}, {{ if $r.Lt }}{{ constantName . "Lt" }}{{ else }}null{{ end }}, {{ if $r.Lte }}{{ constantName . "Lte" }}{{ else }}null{{ end }}, {{ if $r.Gt }}{{ constantName . "Gt" }}{{ else }}null{{ end }}, {{ if $r.Gte }}{{ constantName . "Gte" }}{{ else }}null{{ end }}, com.google.protobuf.util.Timestamps.comparator());
{{- else -}}
{{- if $r.Lt }}
			if ({{ hasAccessor . }}) io.envoyproxy.pgv.ComparativeValidation.lessThan("{{ $f.FullyQualifiedName }}", {{ accessor . }}, {{ constantName . "Lt" }}, com.google.protobuf.util.Timestamps.comparator());
{{- end -}}
{{- if $r.Lte }}
			if ({{ hasAccessor . }}) io.envoyproxy.pgv.ComparativeValidation.lessThanOrEqual("{{ $f.FullyQualifiedName }}", {{ accessor . }}, {{ constantName . "Lte" }}, com.google.protobuf.util.Timestamps.comparator());
{{- end -}}
{{- if $r.Gt }}
			if ({{ hasAccessor . }}) io.envoyproxy.pgv.ComparativeValidation.greaterThan("{{ $f.FullyQualifiedName }}", {{ accessor . }}, {{ constantName . "Gt" }}, com.google.protobuf.util.Timestamps.comparator());
{{- end -}}
{{- if $r.Gte }}
			if ({{ hasAccessor . }}) io.envoyproxy.pgv.ComparativeValidation.greaterThanOrEqual("{{ $f.FullyQualifiedName }}", {{ accessor . }}, {{ constantName . "Gte" }}, com.google.protobuf.util.Timestamps.comparator());
{{- end -}}
{{- end -}}
{{- if $r.LtNow }}
			if ({{ hasAccessor . }}) io.envoyproxy.pgv.ComparativeValidation.lessThan("{{ $f.FullyQualifiedName }}", {{ accessor . }}, io.envoyproxy.pgv.TimestampValidation.currentTimestamp(), com.google.protobuf.util.Timestamps.comparator());
{{- end -}}
{{- if $r.GtNow }}
			if ({{ hasAccessor . }}) io.envoyproxy.pgv.ComparativeValidation.greaterThan("{{ $f.FullyQualifiedName }}", {{ accessor . }}, io.envoyproxy.pgv.TimestampValidation.currentTimestamp(), com.google.protobuf.util.Timestamps.comparator());
{{- end -}}
{{- if $r.Within }}
			if ({{ hasAccessor . }}) io.envoyproxy.pgv.TimestampValidation.within("{{ $f.FullyQualifiedName }}", {{ accessor . }}, {{ constantName . "Within" }}, io.envoyproxy.pgv.TimestampValidation.currentTimestamp());
{{- end -}}
`
