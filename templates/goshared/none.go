package goshared

const noneTpl = `// no validation rules for {{ name .Field }}
	{{- if .Index }}[{{ .Index }}]{{ end }}
	{{- if .OnKey }} (key){{ end }}`
