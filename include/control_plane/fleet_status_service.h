// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_FLEET_STATUS_SERVICE_H
#define BYTETAPER_CONTROL_PLANE_FLEET_STATUS_SERVICE_H

#include "control_plane/fleet_status.h"
#include "control_plane/fleet_status_config.h"
#include "control_plane/policy_state_key.h"
#include "control_plane/policy_state_store.h"
#include "control_plane/runtime_status_report.h"

#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace bytetaper::control_plane {

class FleetStatusService {
public:
    FleetStatusService(FleetStatusConfig config, PolicyStateStore* policy_state_store);

    RuntimeStatusReportResult ingest(const RuntimeStatusReport& report,
                                     std::int64_t now_unix_epoch_ms);

    FleetStatusResult get_fleet_status(const PolicyResourceKey& resource_key,
                                       std::int64_t now_unix_epoch_ms);

    bool has_runtime(const PolicyResourceKey& resource_key, const std::string& runtime_id);

    std::optional<RuntimeStatusReport> find_runtime_report(const PolicyResourceKey& resource_key,
                                                           const std::string& runtime_id);

    void prune_expired(std::int64_t now_unix_epoch_ms);

private:
    FleetStatusConfig config_;
    PolicyStateStore* policy_state_store_;
    std::mutex mu_;
    std::map<std::string, std::map<std::string, RuntimeStatusReport>> runtimes_by_resource_;
};

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_FLEET_STATUS_SERVICE_H
