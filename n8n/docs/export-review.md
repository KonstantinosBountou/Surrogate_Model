# Export review

The supplied n8n JSON was read and a separate sharing copy was prepared. The original downloaded export and running n8n workflow were not modified.

## Changes in the sharing copy

- Removed SSH credential references (IDs and names) from five SSH nodes. No SSH password was present in the supplied export.
- Removed installation metadata, workflow ID and version ID.
- Removed the `availableInMCP` setting from the exported copy.
- Replaced the personal absolute working directory in four SSH nodes with `/ABSOLUTE/PATH/TO/ns-3.47`.
- Kept node IDs, node names, connections, commands and expressions, except for those working-directory substitutions.
- Kept the workflow inactive and pinData empty.

## Checks and limits

JSON parsing, node-reference integrity, three generated distances, one-item loop batch default, and the merge node's Execute Once setting were checked. The sharing copy was scanned for the removed user path, original credential ID, instance ID and obvious embedded secret fields.

The scenario and baseline JSON were copied from the current WSL files. They are snapshots at packaging time, not a claim that the binary used in earlier batches was rebuilt from these exact bytes. The existing root scenario was retained separately.

The imported sharing copy was not executed in a fresh n8n installation, and the simulator was not rebuilt during this packaging task. Pilot results are reported from the user-provided execution records. No credentials or raw execution exports are included.
