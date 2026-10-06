#pragma once

namespace agentcad {
// Locate resources relative to the installed executable. Existing explicit
// resource settings are preserved for developer SDKs.
void configure_runtime();
}
