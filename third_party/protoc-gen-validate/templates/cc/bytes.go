package cc

const bytesTpl = `
	{{ $f := .Field }}{{ $r := .Rules }}
	{{ template "const" . }}
	{{ template "in" . }}

	{{ if or $r.Len (and $r.MinLen $r.MaxLen (eq $r.GetMinLen $r.GetMaxLen)) }}
	{
		const auto length = {{ accessor . }}.size();
		{{ if $r.Len }}
			if (length != {{ $r.GetLen }}) {
				{{ err . "value length must be " $r.GetLen " bytes" }}
			}
		{{ else }}
			if (length != {{ $r.GetMinLen }}) {
				{{ err . "value length must be " $r.GetMinLen " bytes" }}
			}
		{{ end }}
	}
	{{ else if $r.MinLen }}
	{
		const auto length = {{ accessor . }}.size();
		{{ if $r.MaxLen }}
			if (length < {{ $r.GetMinLen }} || length > {{ $r.GetMaxLen }}) {
				{{ err . "value length must be between " $r.GetMinLen " and " $r.GetMaxLen " bytes, inclusive" }}
			}
		{{ else }}
			if (length < {{ $r.GetMinLen }}) {
				{{ err . "value length must be at least " $r.GetMinLen " bytes" }}
			}
		{{ end }}
	}
	{{ else if $r.MaxLen }}
		if ({{ accessor . }}.size() > {{ $r.GetMaxLen }}) {
			{{ err . "value length must be at most " $r.GetMaxLen " bytes" }}
		}
	{{ end }}

	{{ if $r.Prefix }}
	{
		const std::string prefix = {{ lit $r.GetPrefix }};
		if (!pgv::IsPrefix(prefix, {{ accessor . }})) {
			{{ err . "value does not have prefix " (lit $r.GetPrefix) }}
		}
	}
	{{ end }}

	{{ if $r.Suffix }}
	{
		const std::string suffix = {{ lit $r.GetSuffix }};
		if (!pgv::IsSuffix(suffix, {{ accessor .}})) {
			{{ err . "value does not have suffix " (lit $r.GetSuffix) }}
		}
	}
	{{ end }}

	{{ if $r.Contains }}
	{
		if (!pgv::Contains({{ accessor . }}, {{ lit $r.GetContains }})) {
			{{ err . "value does not contain substring " (lit $r.GetContains) }}
		}
	}
	{{ end }}

        {{ if $r.Pattern }}
        {
                if (!RE2::FullMatch(re2::StringPiece({{ accessor . }}.c_str(), {{ accessor . }}.size()),
                                    {{ lookup $f "Pattern" }})) {
		        {{ err . "value does not match regex pattern " (lit $r.GetPattern) }}
	        }
        }
	{{ end }}

	{{ if $r.GetIp }}
		{{ unimplemented "C++ ip address validation is not implemented" }}
		{{/* TODO(akonradi) implement all of this
		if ip := net.IP({{ accessor . }}); ip.To16() == nil {
			return {{ err . "value must be a valid IP address" }}
		}
		*/}}
	{{ else if $r.GetIpv4 }}
		{{ unimplemented "C++ ip address validation is not implemented" }}
		{{/* TODO(akonradi) implement all of this
		if ip := net.IP({{ accessor . }}); ip.To4() == nil {
			return {{ err . "value must be a valid IPv4 address" }}
		}
		*/}}
	{{ else if $r.GetIpv6 }}
		{{ unimplemented "C++ ip address validation is not implemented" }}
		{{/* TODO(akonradi) implement all of this
		if ip := net.IP({{ accessor . }}); ip.To16() == nil || ip.To4() != nil {
			return {{ err . "value must be a valid IPv6 address" }}
		}
		*/}}
	{{ end }}
`
