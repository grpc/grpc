# Channelz

This directory contains the implementation of gRPC's Channelz, which is a system for inspecting the state of gRPC channels.

## Overarching Purpose

Channelz provides a way to get detailed information about the state of gRPC channels, including things like the number of calls, the amount of data sent and received, and the status of the underlying transport. This information can be used for debugging, monitoring, and performance tuning.

## Files and Subdirectories

- **`channelz.h` / `channelz.cc`**: Defines the `Channelz` class, which is the main class for Channelz.
- **`channel_trace.h` / `channel_trace.cc`**: Defines the `ChannelTrace` class, which is used to trace events on a channel.
- **`channelz_registry.h` / `channelz_registry.cc`**: Defines the `ChannelzRegistry` class, which is a registry for all Channelz entities.
- **`property_list.h` / `property_list.cc`**: Defines a list of properties that can be attached to a Channelz entity.
- **`ztrace_collector.h`**: A collector for Channelz trace events.
- **`v2tov1/`**: Code for converting between Channelz v2 and v1 formats.
- **`zviz/`**: A web-based viewer for Channelz data.

## Notes

- Channelz is a powerful tool for understanding the behavior of gRPC channels.
- It can be used to debug a wide variety of problems, from simple connectivity issues to complex performance problems.
- The Channelz data can be accessed through a variety of tools, including the `grpc_cli` command-line tool and the web-based viewer in the `zviz` directory.

## DataSource Lifetime

`DataSource` objects are not owned by the `BaseNode` they are attached to.
They are expected to be owned by some other transport-level object.
`DataSource` implementations call `SourceConstructed()` in their constructor to
register with a `BaseNode` and `SourceDestructing()` in their destructor to
unregister.

Because `BaseNode` does not manage `DataSource` lifetime, it is unsafe for
`BaseNode` to call `DataSource::AddData` without holding `BaseNode::data_sources_mu_`,
as the `DataSource` could be destroyed by another thread concurrently.
This means `data_sources_mu_` must be held during any call to `AddData`.
Consequently, `AddData` implementations must not call back into any code that
acquires `data_sources_mu_`, such as `SourceConstructed`, `SourceDestructing`,
or other channelz rendering paths like `SerializeEntity` or `AdditionalInfo`,
as this will cause a deadlock.

If this cannot be guaranteed (for example, in `Party` we might execute arbitrary
other promises during a `Spawn()` call, in chttp2 we need to enter the combiner
lock which has similar properties) a good technique is to use `EventEngine`
to spawn a background task to collect the data outside of the BaseNode lock.