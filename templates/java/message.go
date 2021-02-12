package java

const messageTpl = `{{ $f := .Field }}{{ $r := .Rules }}
	{{- if .MessageRules.GetSkip }}
			// skipping validation for {{ $f.Name }}
	{{- else -}}
		{{- template "required" . }}
		{{- if (isOfMessageType $f) }}
			// Validate {{ $f.Name }}
			if ({{ hasAccessor . }}) index.validatorFor({{ accessor . }}).assertValid({{ accessor . }});
		{{- end -}}
	{{- end -}}
`
