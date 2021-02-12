package module

import (
	"github.com/envoyproxy/protoc-gen-validate/templates"
	"github.com/envoyproxy/protoc-gen-validate/templates/java"
	pgs "github.com/lyft/protoc-gen-star"
	pgsgo "github.com/lyft/protoc-gen-star/lang/go"
)

const (
	validatorName = "validator"
	langParam     = "lang"
)

type Module struct {
	*pgs.ModuleBase
	ctx pgsgo.Context
}

func Validator() pgs.Module { return &Module{ModuleBase: &pgs.ModuleBase{}} }

func (m *Module) InitContext(ctx pgs.BuildContext) {
	m.ModuleBase.InitContext(ctx)
	m.ctx = pgsgo.InitContext(ctx.Parameters())
}

func (m *Module) Name() string { return validatorName }

func (m *Module) Execute(targets map[string]pgs.File, pkgs map[string]pgs.Package) []pgs.Artifact {
	lang := m.Parameters().Str(langParam)
	m.Assert(lang != "", "`lang` parameter must be set")

	// Process file-level templates
	tpls := templates.Template(m.Parameters())[lang]
	m.Assert(tpls != nil, "could not find templates for `lang`: ", lang)

	for _, f := range targets {
		m.Push(f.Name().String())

		for _, msg := range f.AllMessages() {
			m.CheckRules(msg)
		}

		for _, tpl := range tpls {
			out := templates.FilePathFor(tpl)(f, m.ctx, tpl)
			// A nil path means no output should be generated for this file - as controlled by
			// implementation-specific FilePathFor implementations.
			// Ex: Don't generate Java validators for files that don't reference PGV.
			if out != nil {
				if opts := f.Descriptor().GetOptions(); opts != nil && opts.GetJavaMultipleFiles() && lang == "java" {
					// TODO: Only Java supports multiple file generation. If more languages add multiple file generation
					// support, the implementation should be made more inderect.
					for _, msg := range f.Messages() {
						m.AddGeneratorTemplateFile(java.JavaMultiFilePath(f, msg).String(), tpl, msg)
					}
				} else {
					m.AddGeneratorTemplateFile(out.String(), tpl, f)
				}
			}
		}

		m.Pop()
	}

	return m.Artifacts()
}

var _ pgs.Module = (*Module)(nil)
