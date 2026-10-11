# Local MCP distribution preparation

Checked against official OpenAI documentation on 2026-10-11. This is a
reviewable, unsent request. It does not establish directory access, provider
approval, host acceptance or release readiness.

## Current documented boundary

OpenAI's [plugin packaging guide](https://developers.openai.com/plugins/build/plugins)
and [Claude plugin submission guide](https://developers.openai.com/plugins/guides/submit-claude-plugin)
direct publishers of local MCP servers to an OpenAI contact when a public HTTPS
deployment is unsuitable. The latter also calls for partner consultation when
local execution, computer files or offline operation are central to the product.
The [connection/testing guide](https://developers.openai.com/plugins/deploy/connect-chatgpt)
describes local testing connections but does not establish public submission
eligibility for this native package. These are distinct acceptance requirements.

Agent CAD's 1.0 decision requires a packaged local native service and an embedded
viewer in ChatGPT desktop and Claude Desktop. End users do not install a language
runtime, compiler or tunnel, or edit configuration to create a model. See
[RELEASE_1_0.md](RELEASE_1_0.md). An alternative hosted service would change that
product decision and is not an implementation of this distribution requirement.

## Unsent request

Subject: Supported local MCP distribution route for Agent CAD

We are developing Agent CAD, a native parametric CAD plugin whose editable
documents, geometry service and exports remain on the user's computer. The
public source repository is
[cfaulkingham/agent-3d-cad](https://github.com/cfaulkingham/agent-3d-cad).
The service uses packaged C++20/OpenCascade 8.0.1 dependencies and MCP over stdio;
users do not install Python, Node, Rust or a compiler. Its MCP App viewer is
embedded in chat, with revision-qualified face/edge selection and transactional
model edits. The default project folder is the user's Documents/Agent CAD.

The development package uses Agent Plugins 1.0 with package-relative native
server configuration, setup/modeling skills and a local marketplace catalog.
Claude has a separate native desktop extension. Development version
0.1.0-preview.2 exposes 31 tools and provides bounded, source-qualified export
resources for STEP, STL and drawings. Current thin ARM Mac packages are ad-hoc
signed development artifacts, not publisher-signed releases. Actual host
install/restart/save/update journeys are being tested. Universal Mac runtime,
publisher signing/notarization and public distribution acceptance remain open.

The original project code is MIT-licensed; dependencies preserve their notices.
The service does not execute code supplied by model documents. Geometry work is
isolated in bounded serial native workers, and failed edits preserve the
committed revision. Tool contracts and release acceptance requirements are
available in the repository's docs/PROTOCOL.md and docs/RELEASE_1_0.md.

Your current plugin guidance directs local MCP publishers to an OpenAI contact.
Please identify the supported review and distribution route for this local
native package, including:

- Whether ChatGPT desktop can install and update this bundled stdio MCP service
  through an approved directory route, with the saved project folder preserved.
- The required package format, publisher identity/signing requirements and Mac
  universal/Windows CPU support expectations for that route.
- Whether the embedded MCP App and its supported save/download actions can be
  reviewed together with the native service, and how to obtain a test profile.
- The submission contact/process and required review materials for local file
  access and offline CAD execution.

We can supply exact package/source hashes, native and SDK test evidence and a
host journey recording as those acceptance checks complete. We will describe
each artifact's current signing and runtime verification status explicitly.

## Evidence and next action

The tested native source is `3cc4617fc2a3137324bd8654e2cc720d793d5322`;
[CI53](https://github.com/cfaulkingham/agent-3d-cad/actions/runs/38126324625)
is the corresponding five-platform/universal acceptance run. Current outcomes
and limitations belong in [HANDOFF.md](HANDOFF.md), not an assumed approval in
this draft. Exact preview.2 local archive hashes are recorded there.

Before sending, confirm the intended OpenAI contact and the user's authorization
to send this concrete request. Attach only reviewed artifacts, and refresh the
development/evidence paragraph if CI or host/signing results change. No contact,
portal upload, directory submission, publication or host installation was made
while preparing this document.
