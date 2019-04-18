Ordering Status and Reads in the gRPC API
-----------------------------------------

Rules for implementors:
1. Reads and Writes Must not succeed after Status has been delivered.
2. OK Status is only delivered after all buffered messages are read.
3. Reads May continue to succeed after a failing write.
   However, once a write fails, all subsequent writes Must fail,
   and similarly, once a read fails, all subsequent reads Must fail.
4. When an error status is known to the library, if the user asks for status,
   the library Should discard messages received in the library but not delivered
   to the user and then deliver the status. If the user does not ask for status
   but continues reading, the library Should deliver buffered messages before
   delivering status. The library MAY choose to implement the stricter version
   where errors cause all buffered messages to be dropped, but this is not a
   requirement.
