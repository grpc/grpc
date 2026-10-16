Latent-see
----------

This is a simple latency profiling tool.

We record various timestamps throughout program execution, and then at exit format json
to a file `latent_see.json` in the chrome event trace format. This format can be
consumed by various tools (eg ui.perfetto.dev).

Recording macros are documented in latent_see.h.
