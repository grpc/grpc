package java

const durationConstTpl = `{{ $f := .Field }}{{ $r := .Rules -}}
{{- if $r.Const }}
		private final com.google.protobuf.Duration {{ constantName . "Const" }} = {{ durLit $r.GetConst }};
{{- end -}}
{{- if $r.Lt }}
		private final com.google.protobuf.Duration {{ constantName . "Lt" }} = {{ durLit $r.GetLt }};
{{- end -}}
{{- if $r.Lte }}
		private final com.google.protobuf.Duration {{ constantName . "Lte" }} = {{ durLit $r.GetLte }};
{{- end -}}
{{- if $r.Gt }}
		private final com.google.protobuf.Duration {{ constantName . "Gt" }} = {{ durLit $r.GetGt }};
{{- end -}}
{{- if $r.Gte }}
		private final com.google.protobuf.Duration {{ constantName . "Gte" }} = {{ durLit $r.GetGte }};
{{- end -}}
{{- if $r.In }}
		private final com.google.protobuf.Duration[] {{ constantName . "In" }} = new com.google.protobuf.Duration[]{
			{{- range $r.In }}
			{{ durLit . }},
			{{- end }}
		};
{{- end -}}
{{- if $r.NotIn }}
		private final com.google.protobuf.Duration[] {{ constantName . "NotIn" }} = new com.google.protobuf.Duration[]{
			{{- range $r.NotIn }}
			{{ durLit . }},
			{{- end }}
		};
{{- end -}}`

const durationTpl = `{{ $f := .Field }}{{ $r := .Rules -}}
{{- template "required" . -}}

{{- if $r.Const }}
			if ({{ hasAccessor . }}) io.envoyproxy.pgv.ConstantValidation.constant("{{ $f.FullyQualifiedName }}", {{ accessor . }}, {{ constantName . "Const" }});
{{- end -}}
{{- if and (or $r.Lt $r.Lte) (or $r.Gt $r.Gte)}}
			if ({{ hasAccessor . }}) io.envoyproxy.pgv.ComparativeValidation.range("{{ $f.FullyQualifiedName }}", {{ accessor . }}, {{ if $r.Lt }}{{ constantName . "Lt" }}{{ else }}null{{ end }}, {{ if $r.Lte }}{{ constantName . "Lte" }}{{ else }}null{{ end }}, {{ if $r.Gt }}{{ constantName . "Gt" }}{{ else }}null{{ end }}, {{ if $r.Gte }}{{ constantName . "Gte" }}{{ else }}null{{ end }}, com.google.protobuf.util.Durations.comparator());
{{- else -}}
{{- if $r.Lt }}
			if ({{ hasAccessor . }}) io.envoyproxy.pgv.ComparativeValidation.lessThan("{{ $f.FullyQualifiedName }}", {{ accessor . }}, {{ constantName . "Lt" }}, com.google.protobuf.util.Durations.comparator());
{{- end -}}
{{- if $r.Lte }}
			if ({{ hasAccessor . }}) io.envoyproxy.pgv.ComparativeValidation.lessThanOrEqual("{{ $f.FullyQualifiedName }}", {{ accessor . }}, {{ constantName . "Lte" }}, com.google.protobuf.util.Durations.comparator());
{{- end -}}
{{- if $r.Gt }}
			if ({{ hasAccessor . }}) io.envoyproxy.pgv.ComparativeValidation.greaterThan("{{ $f.FullyQualifiedName }}", {{ accessor . }}, {{ constantName . "Gt" }}, com.google.protobuf.util.Durations.comparator());
{{- end -}}
{{- if $r.Gte }}
			if ({{ hasAccessor . }}) io.envoyproxy.pgv.ComparativeValidation.greaterThanOrEqual("{{ $f.FullyQualifiedName }}", {{ accessor . }}, {{ constantName . "Gte" }}, com.google.protobuf.util.Durations.comparator());
{{- end -}}
{{- end -}}
{{- if $r.In }}
			if ({{ hasAccessor . }}) io.envoyproxy.pgv.CollectiveValidation.in("{{ $f.FullyQualifiedName }}", {{ accessor . }}, {{ constantName . "In" }});
{{- end -}}
{{- if $r.NotIn }}
			if ({{ hasAccessor . }}) io.envoyproxy.pgv.CollectiveValidation.notIn("{{ $f.FullyQualifiedName }}", {{ accessor . }}, {{ constantName . "NotIn" }});
{{- end -}}
`
