package golang

const durationTpl = `{{ $f := .Field }}{{ $r := .Rules }}
	{{ template "required" . }}

	{{ if or $r.In $r.NotIn $r.Lt $r.Lte $r.Gt $r.Gte $r.Const }}
		if d := {{ accessor . }}; d != nil {
			dur, err := ptypes.Duration(d)
			if err != nil { return {{ errCause . "err" "value is not a valid duration" }} }

			{{ template "durationcmp" . }}
		}
	{{ end }}
`
