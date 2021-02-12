package cc

const inTpl = `{{ $f := .Field -}}{{ $r := .Rules -}}
	{{- if $r.In }}
		if ({{ lookup $f "InLookup" }}.find(static_cast<decltype({{ lookup $f "InLookup" }})::key_type>({{ accessor . }})) == {{ lookup $f "InLookup" }}.end()) {
			{{ err . "value must be in list " $r.In }}
		}
	{{- else if $r.NotIn }}
		if ({{ lookup $f "NotInLookup" }}.find(static_cast<decltype({{ lookup $f "NotInLookup" }})::key_type>({{ accessor . }})) != {{ lookup $f "NotInLookup" }}.end()) {
			{{ err . "value must not be in list " $r.NotIn }}
		}
	{{- end }}
`
