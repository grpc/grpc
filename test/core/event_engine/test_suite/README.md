A reusable test suite for EventEngine implementations.

To exercise a custom EventEngine, simply link against `:event_engine_test_suite`
and provide a definition of `EventEnigneTest::NewEventEventEngine`. For a class
called `MyCustomEventEngine`, it will look something like:

```
#include "src/core/event_engine/test_suite/event_engine_test.h"

std::unique_ptr<EventEngine> EventEngineTest::NewEventEngine() {
  return absl::make_unique<MyCustomEventEngine>();
}
```
