package cc

const noneTpl = `// no validation rules for {{ .Field.Name }}
	{{- if .Index }}[{{ .Index }}]{{ end }}
	{{- if .OnKey }} (key){{ end }}`
