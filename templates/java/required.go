package java

const requiredTpl = `{{ $f := .Field }}
	{{- if .Rules.GetRequired }}
		if ({{ hasAccessor . }}) {
			io.envoyproxy.pgv.RequiredValidation.required("{{ $f.FullyQualifiedName }}", {{ accessor . }});
		} else {
			io.envoyproxy.pgv.RequiredValidation.required("{{ $f.FullyQualifiedName }}", null);
		};
	{{- end -}}
`
