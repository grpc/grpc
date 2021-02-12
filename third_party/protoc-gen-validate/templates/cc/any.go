package cc

const anyTpl = `{{ $f := .Field }}{{ $r := .Rules }}
	{{ template "required" . }}

	{{ if $r.In }}
		{{ $table := lookup $f "InLookup" }}
		if ({{ hasAccessor . }} && {{ $table }}.find({{ accessor . }}.type_url()) == {{ $table }}.end()) {
			{{ err . "type URL must be in list " $r.In }}
		}
	{{ else if $r.NotIn }}
		{{ $table := lookup $f "NotInLookup" }}
		if ({{ hasAccessor . }} && {{ $table }}.find({{ accessor . }}.type_url()) != {{ $table }}.end()) {
			{{ err . "type URL must not be in list " $r.NotIn }}
		}
	{{ end }}
`
