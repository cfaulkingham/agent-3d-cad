---
name: setup
description: Check the Agent CAD plugin connection and open the user's saved project library after installation.
---

Use the connected Agent CAD MCP tools. Call `cad_list` to check the connection
and discover saved documents, then `cad_open` with a distinct view ID for this
chat. Open an empty library when there are no projects; do not create a sample
without the user's request. If the user names an existing project, open that
document. Retain the returned view ID for subsequent `cad_show` and `cad_context`.

The plugin creates a persistent project folder in Documents/Agent CAD. Users
do not need a terminal, language runtime or standalone viewer application.
Models appear inside the host's MCP App. If the host cannot render it, report
that limitation instead of claiming the viewer opened or installing another app.

Explain that a picked face or edge can be referenced in the connected chat and
that exports are independent of the editable source. Offer a concrete first
request, such as an 80 × 50 × 6 mm mounting plate. Do not repeat setup in later
modeling requests. Use the packaged native-cad skill for modeling and edits.

If tools are unavailable, identify the missing connection and direct the user
to the plugin's connection controls. Do not create a second MCP entry or change
host settings silently. Never report success before `cad_list` and `cad_open`
return successfully.
