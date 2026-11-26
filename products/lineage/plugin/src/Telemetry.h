// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "ln/engine.h"

#include <hba/host/telemetry_channel.h>

namespace ln {

using TelemetryChannel = hba::host::TelemetryChannel<TelemetrySnapshot>;

} // namespace ln
