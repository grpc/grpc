package java

const repeatedConstTpl = `{{ renderConstants (.Elem "" "") }}`

const repeatedTpl = `{{ $f := .Field }}{{ $r := .Rules -}}
{{- if $r.GetMinItems }}
			io.envoyproxy.pgv.RepeatedValidation.minItems("{{ $f.FullyQualifiedName }}", {{ accessor . }}, {{ $r.GetMinItems }});
{{- end -}}
{{- if $r.GetMaxItems }}
			io.envoyproxy.pgv.RepeatedValidation.maxItems("{{ $f.FullyQualifiedName }}", {{ accessor . }}, {{ $r.GetMaxItems }});
{{- end -}}
{{- if $r.GetUnique }}
			io.envoyproxy.pgv.RepeatedValidation.unique("{{ $f.FullyQualifiedName }}", {{ accessor . }});
{{- end }}
			io.envoyproxy.pgv.RepeatedValidation.forEach({{ accessor . }}, item -> {
				{{ render (.Elem "item" "") }}
			});
`
