// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "tg/engine.h"

#include <hba/host/telemetry_channel.h>

namespace tg {

using TelemetryChannel = hba::host::TelemetryChannel<TelemetrySnapshot>;

} // namespace tg
