#pragma once
// The ACTS umbrella header (the fork's src/core/acts/includes.hpp) trimmed to what reading Replay 1.20 zones uses:
// no PS4 debugger, mods or asmjit. Found before the fork's copy: iw8r/ is first on iw8r-acts' include path.
// ACTS: GPL-3 or MIT (src/tools/acts/LICENSE.md), used here under MIT.

#define _WINSOCKAPI_
#include <includes_shared.hpp>
#include <zlib.h>

#include "hashutils.hpp"
#include "tools/tools.hpp"
