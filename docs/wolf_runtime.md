In general, the wolf runtime API implementation itself should *call into* other utilities or functions within the module, not the other way around. This may avoid circular include dependencies and other clutter. The exception to this is already-registered callbacks, of course, and certain other exceptions can be made if they signifigantly simplify the control flow.

The runtime <-> plugin API boundary shall be a C boundary, with all functions having explicit calling conventions and no mangling of signatures or names.
