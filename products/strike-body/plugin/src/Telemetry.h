// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "sb/engine.h"

#include <hba/host/telemetry_channel.h>

namespace sb {

using TelemetryChannel = hba::host::TelemetryChannel<TelemetrySnapshot>;

} // namespace sb
