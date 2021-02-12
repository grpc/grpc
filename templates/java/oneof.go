package java

const oneOfConstTpl = `
{{ range .Fields }}{{ renderConstants (context .) }}{{ end }}
`

const oneOfTpl = `
			switch (proto.get{{camelCase .Name }}Case()) {
				{{ range .Fields -}}
				case {{ oneof . }}:
					{{ render (context .) }}
					break;
				{{ end -}}
				{{- if required . }}
				default: 
					io.envoyproxy.pgv.RequiredValidation.required("{{ .FullyQualifiedName }}", null);
				{{- end }}
			}
`
