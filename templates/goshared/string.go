package goshared

const strTpl = `
	{{ $f := .Field }}{{ $r := .Rules }}
	{{ template "const" . }}
	{{ template "in" . }}

	{{ if or $r.Len (and $r.MinLen $r.MaxLen (eq $r.GetMinLen $r.GetMaxLen)) }}
		{{ if $r.Len }}
			if utf8.RuneCountInString({{ accessor . }}) != {{ $r.GetLen }} {
			return {{ err . "value length must be " $r.GetLen " runes" }}
		{{ else }}
		if utf8.RuneCountInString({{ accessor . }}) != {{ $r.GetMinLen }} {
			return {{ err . "value length must be " $r.GetMinLen " runes" }}
		{{ end }}
	}
	{{ else if $r.MinLen }}
		{{ if $r.MaxLen }}
			if l := utf8.RuneCountInString({{ accessor . }}); l < {{ $r.GetMinLen }} || l > {{ $r.GetMaxLen }} {
				return {{ err . "value length must be between " $r.GetMinLen " and " $r.GetMaxLen " runes, inclusive" }}
			}
		{{ else }}
			if utf8.RuneCountInString({{ accessor . }}) < {{ $r.GetMinLen }} {
				return {{ err . "value length must be at least " $r.GetMinLen " runes" }}
			}
		{{ end }}
	{{ else if $r.MaxLen }}
		if utf8.RuneCountInString({{ accessor . }}) > {{ $r.GetMaxLen }} {
			return {{ err . "value length must be at most " $r.GetMaxLen " runes" }}
		}
	{{ end }}

	{{ if or $r.LenBytes (and $r.MinBytes $r.MaxBytes (eq $r.GetMinBytes $r.GetMaxBytes)) }}
		{{ if $r.LenBytes }}
			if len({{ accessor . }}) != {{ $r.GetLenBytes }} {
				return {{ err . "value length must be " $r.GetLenBytes " bytes" }}
			}
		{{ else }}
			if len({{ accessor . }}) != {{ $r.GetMinBytes }} {
				return {{ err . "value length must be " $r.GetMinBytes " bytes" }}
			}
		{{ end }}
	{{ else if $r.MinBytes }}
		{{ if $r.MaxBytes }}
			if l := len({{ accessor . }}); l < {{ $r.GetMinBytes }} || l > {{ $r.GetMaxBytes }} {
					return {{ err . "value length must be between " $r.GetMinBytes " and " $r.GetMaxBytes " bytes, inclusive" }}
			}
		{{ else }}
			if len({{ accessor . }}) < {{ $r.GetMinBytes }} {
				return {{ err . "value length must be at least " $r.GetMinBytes " bytes" }}
			}
		{{ end }}
	{{ else if $r.MaxBytes }}
		if len({{ accessor . }}) > {{ $r.GetMaxBytes }} {
			return {{ err . "value length must be at most " $r.GetMaxBytes " bytes" }}
		}
	{{ end }}

	{{ if $r.Prefix }}
		if !strings.HasPrefix({{ accessor . }}, {{ lit $r.GetPrefix }}) {
			return {{ err . "value does not have prefix " (lit $r.GetPrefix) }}
		}
	{{ end }}

	{{ if $r.Suffix }}
		if !strings.HasSuffix({{ accessor . }}, {{ lit $r.GetSuffix }}) {
			return {{ err . "value does not have suffix " (lit $r.GetSuffix) }}
		}
	{{ end }}

	{{ if $r.Contains }}
		if !strings.Contains({{ accessor . }}, {{ lit $r.GetContains }}) {
			return {{ err . "value does not contain substring " (lit $r.GetContains) }}
		}
	{{ end }}

	{{ if $r.NotContains }}
		if strings.Contains({{ accessor . }}, {{ lit $r.GetNotContains }}) {
			return {{ err . "value contains substring " (lit $r.GetNotContains) }}
		}
	{{ end }}

	{{ if $r.GetIp }}
		if ip := net.ParseIP({{ accessor . }}); ip == nil {
			return {{ err . "value must be a valid IP address" }}
		}
	{{ else if $r.GetIpv4 }}
		if ip := net.ParseIP({{ accessor . }}); ip == nil || ip.To4() == nil {
			return {{ err . "value must be a valid IPv4 address" }}
		}
	{{ else if $r.GetIpv6 }}
		if ip := net.ParseIP({{ accessor . }}); ip == nil || ip.To4() != nil {
			return {{ err . "value must be a valid IPv6 address" }}
		}
	{{ else if $r.GetEmail }}
		if err := m._validateEmail({{ accessor . }}); err != nil {
			return {{ errCause . "err" "value must be a valid email address" }}
		}
	{{ else if $r.GetHostname }}
		if err := m._validateHostname({{ accessor . }}); err != nil {
			return {{ errCause . "err" "value must be a valid hostname" }}
		}
	{{ else if $r.GetAddress }}
		if err := m._validateHostname({{ accessor . }}); err != nil {
			if ip := net.ParseIP({{ accessor . }}); ip == nil {
				return {{ err . "value must be a valid hostname, or ip address" }}
			}
		}
	{{ else if $r.GetUri }}
		if uri, err := url.Parse({{ accessor . }}); err != nil {
			return {{ errCause . "err" "value must be a valid URI" }}
		} else if !uri.IsAbs() {
			return {{ err . "value must be absolute" }}
		}
	{{ else if $r.GetUriRef }}
		if _, err := url.Parse({{ accessor . }}); err != nil {
			return {{ errCause . "err" "value must be a valid URI" }}
		}
	{{ else if $r.GetUuid }}
		if err := m._validateUuid({{ accessor . }}); err != nil {
			return {{ errCause . "err" "value must be a valid UUID" }}
		}
	{{ end }}

	{{ if $r.Pattern }}
	if !{{ lookup $f "Pattern" }}.MatchString({{ accessor . }}) {
		return {{ err . "value does not match regex pattern " (lit $r.GetPattern) }}
	}
{{ end }}
`
