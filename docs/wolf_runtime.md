In general, the wolf runtime API itself should *call into* other utilities or functions within the module, not the other way around. This may avoid circular include dependencies and other clutter.

The runtime <-> plugin API boundary shall be a C boundary, with all functions having explicit calling conventions and no mangling of signatures or names.
