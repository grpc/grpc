package cc

const constTpl = `{{ $r := .Rules }}
	{{ if $r.Const }}
		if ({{ accessor . }} != {{ lit $r.GetConst }}) {
			{{ err . "value must equal " $r.GetConst }}
		}
	{{ end }}
`
