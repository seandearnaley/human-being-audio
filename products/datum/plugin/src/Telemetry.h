// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dt/engine.h"

#include <hba/host/telemetry_channel.h>

namespace dt {

using TelemetryChannel = hba::host::TelemetryChannel<TelemetrySnapshot>;

} // namespace dt
