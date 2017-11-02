# Creates a compile_commands.json compilation database using bazel aspects

def _compilation_database_aspect_impl(target, ctx):
    sources = [f for src in ctx.rule.attr.srcs for f in src.files]
    if 'hdrs' in dir(ctx.rule.attr):
      sources.extend([h for src in ctx.rule.attr.hdrs for h in src.files])

    compilation_infos = []
    for s in sources:
      compilation_infos.append(struct(file=s.path))

    for d in ctx.rule.attr.deps:
      compilation_infos += d.compilation_infos

    return struct(compilation_infos=compilation_infos)

def _compilation_database_impl(ctx):
    all_infos = []
    for t in ctx.attr.targets:
        all_infos += t.compilation_infos

    entries = [e.to_json() for e in all_infos]
    ctx.file_action(output=ctx.outputs.filename, content="[\n " + ", \n ".join(entries) + "\n]")

compilation_database_aspect = aspect(
    implementation = _compilation_database_aspect_impl,
    attr_aspects = ["deps"],
)

compilation_database = rule(
    implementation = _compilation_database_impl,
    attrs = {
        'targets': attr.label_list(aspects = [compilation_database_aspect])
    },
    outputs = {
        'filename': 'compile_commands.json',
    }
)
