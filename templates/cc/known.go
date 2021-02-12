package cc

const hostTpl = `
	func (m {{ .TypeName.Pointer }}) _validateHostname(host string) error {
		s := strings.TrimSuffix(host, ".")

		if len(host) > 253 {
			return errors.New("hostname cannot exceed 253 characters")
		}

		for _, part := range strings.Split(s, ".") {
			if l := len(part); l == 0 || l > 63 {
				return errors.New("hostname part must be non-empty and cannot exceed 63 characters")
			}

			if s[0] == '-' {
				return errors.New("hostname parts cannot begin with hyphens")
			}

			if s[len(s)-1] == '-' {
				return errors.New("hostname parts cannot end with hyphens")
			}

			for _, r := range s {
				if (r < 'A' || r > 'Z') && (r < 'a' || r > 'z') && (r < '0' || r > '9') && r != '-' {
					return errors.New("hostname parts can only contain alphanumeric characters or hyphens")
				}
			}
		}

		return nil
	}
`

const emailTpl = `
	func (m {{ .TypeName.Pointer }}) _validateEmail(addr string) error {
		if len(addr) > 254 {
			return errors.New("email addresses cannot exceed 254 characters")
		}

		a, err := mail.ParseAddress(addr)
		if err != nil {
			return err
		}
		addr = a.Address

		parts := strings.SplitN(addr, "@", 2)

		if len(parts[0]) > 64 {
			return errors.New("email address local phrase cannot exceed 64 characters")
		}

		return m._validateHostname(parts[1])
	}
`

const uuidTpl = `
	func (m {{ .TypeName.Pointer }}) _validateUuid(uuid string) error {
                if matched := _{{ .File.InputPath.BaseName }}_uuidPattern.MatchString(uuid); !matched {
                        return errors.New("invalid uuid format")
                }

                return nil
        }
`
