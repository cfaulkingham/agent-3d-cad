# Optional macOS publisher preparation

`packaging/macos-publisher.py` is a developer tool. Builds, universal composition,
CI and the current ad-hoc packages do not invoke it. It does not install software,
create/import credentials, modify keychains, change Gatekeeper, publish a release
or register either desktop host. Python is needed only by this developer tool.

Current local evidence: Xcode supplies `notarytool`; the development machine has
**zero valid code-signing identities**. Offline tests use actual locally compiled
ARM/Intel Mach-O fixtures and real ad-hoc signing. These are not Developer ID
signing, successful Apple submissions or host acceptance evidence.

Apple requires Developer ID signatures, secure timestamps and hardened runtime
for this distribution path. Sign code items individually; do not use deep signing.
See [Apple's signing instructions](https://developer.apple.com/documentation/xcode/creating-distribution-signed-code-for-the-mac)
and [notarization requirements](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution).

## Review the immutable input

Use a verified **core** bundle from an accepted build, before making the plugin
or Claude wrappers. For universal distribution, require the same-archive native
ARM and Intel CI reports first. Supply an independently recorded provenance
SHA-256 and production source SHA-256 from that accepted artifact; inspecting
an untrusted bundle does not make its self-declared identity authoritative.

```sh
python3 packaging/macos-publisher.py inspect /absolute/path/to/core
```

Inspection checks exact inventories, the pinned SDK/license/modified-HLR records,
production source and build/cache bindings, strict native signatures, every CPU
slice and portable loader dependencies. It rejects plugin/Claude/desktop wrappers,
unlisted Mach-O code, malformed signatures and changed files. It does not execute
the native service or contact Apple's notary/timestamp services.

## Explicit signing boundary

Prerequisites are an authorized Developer ID Application certificate **with its
private key**, its exact uppercase SHA-1 fingerprint, the publisher's Team ID and
a publisher-owned reverse-DNS identifier prefix. Select an existing identity with
`security find-identity -v -p codesigning`; the tool does not create one. Do not
substitute an ad-hoc, development or unknown identity.

The following is a template, not a command already executed:

```sh
python3 packaging/macos-publisher.py sign /absolute/path/to/core /new/publisher-output \
  --input-manifest-sha256 APPROVED_PROVENANCE_SHA256 \
  --source-sha256 APPROVED_PRODUCTION_SOURCE_SHA256 \
  --identity-sha1 EXACT_CERTIFICATE_SHA1 --team-id EXACT_TEAM_ID \
  --identifier-prefix PUBLISHER_OWNED_REVERSE_DNS_PREFIX --allow-apple-network
```

The explicit network flag permits Apple's secure timestamp request. Each copied
library and executable is signed with hardened runtime and no entitlements.
Every slice must verify against the exact leaf certificate, Team ID, Apple
Developer ID chain requirement, identifier and timestamp. This workflow grants
no entitlement exemptions for unsigned libraries, debugging or executable memory.
[Apple TN3127](https://developer.apple.com/documentation/technotes/tn3127-inside-code-signing-requirements)
documents the Developer ID chain requirement.

The original input remains unchanged. The new output contains `core/`, original
`input-provenance.json`, `publisher.json` and `notary-submission.zip`. All unsigned
Mach-O bytes, sections, CPU/deployment/loader metadata, resources, permissions,
source/build/cache identities and original thin provenance must remain intact.
Only final signature/slice hashes and the final file inventory are refreshed.
The existing packagers can validate this core. Publication uses an exclusive
atomic rename; failure leaves no partial signed output and cannot replace an
existing directory. The report says `signed-not-submitted`, never notarized.

## Explicit Apple submission and collection

Create an authorized `notarytool` keychain profile separately using Apple's
documented account process. This tool accepts a profile name, not passwords,
API private keys or tokens. `submit` uploads the exact approved ZIP once, without
polling. It captures the ZIP into the new attempt directory, checks its approved
hash, makes the capture read-only and uploads that captured path. It checks the
capture again afterward and retains it with stdout/stderr and the submission ID. An interrupted/failed upload can have an unknown outcome:
review the retained evidence and Apple's submission history before any retry.

```sh
python3 packaging/macos-publisher.py submit /publisher-output /new/submission-evidence \
  --archive-sha256 APPROVED_SUBMISSION_ZIP_SHA256 \
  --keychain-profile EXISTING_AUTHORIZED_PROFILE --allow-apple-network

python3 packaging/macos-publisher.py collect /publisher-output /new/result-evidence \
  --submission /submission-evidence/submission.json \
  --archive-sha256 APPROVED_SUBMISSION_ZIP_SHA256 \
  --keychain-profile EXISTING_AUTHORIZED_PROFILE --allow-apple-network
```

Collection downloads Apple's actual `info` and full `log` responses. It writes
`accepted.json` only when both report `Accepted` for the same submission, the
log's upload SHA-256 matches the approved ZIP, no log error exists, and local
signed bytes still verify. Warnings remain in the preserved log and acceptance
record for review. There is no offline/imported-JSON acceptance command. Pure
response-classification fixtures do not establish any actual Apple acceptance.
Upload is bounded at 300 seconds; local commands and each collection request at
120 seconds. These developer-tool limits do not change native worker, SDK or CI
budgets. Collection does not wait or automatically resubmit.

## Remaining release gates

Apple can issue online tickets for standalone binaries, but neither standalone
binaries nor ZIP archives can receive a stapled ticket. See Apple's
[custom notarization workflow](https://developer.apple.com/documentation/security/customizing-the-notarization-workflow).
This tool therefore reports `stapled: false`; it does not claim offline Gatekeeper
acceptance. A staple-capable delivery container or a demonstrated host-supported
route remains a distribution decision, with separate tests on downloaded,
quarantined packages on clean Macs.

After real credentialed signing, rerun native service/worker CPU, cache, modeling,
exports and isolated lifecycle checks using the **signed bytes** on both Macs.
Recreate the plugin and Claude wrappers from `publisher-output/core`, verify their
inventories and hashes, and test their actual host installation, upgrade,
restart/reopen and save/download journeys. The current ad-hoc CI reports do not
prove hardened-runtime behavior, Apple acceptance or either host's publisher
route. See [RELEASE_1_0.md](RELEASE_1_0.md) and
[LOCAL_PACKAGE_ACCEPTANCE.md](LOCAL_PACKAGE_ACCEPTANCE.md).

Offline developer verification:

```sh
python3 tests/macos_publisher_tests.py
```

This exercises real ARM/Intel/fat Mach-O re-signing and strict validation, payload
and loader preservation, source/cache inventory rejection, exact Apple-chain
rejection of ad-hoc code, network-command gating and atomic failure/publication.
It deliberately cannot exercise successful Developer ID signing without an
authorized identity or claim successful notarization without Apple's response.
