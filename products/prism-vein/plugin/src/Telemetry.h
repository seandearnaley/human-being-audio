// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "pv/engine.h"

#include <hba/host/telemetry_channel.h>

namespace pv {

using TelemetryChannel = hba::host::TelemetryChannel<TelemetrySnapshot>;

} // namespace pv
