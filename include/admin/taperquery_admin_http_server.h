// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_TAPERQUERY_ADMIN_HTTP_SERVER_H
#define BYTETAPER_TAPERQUERY_ADMIN_HTTP_SERVER_H

#include "runtime/policy_snapshot.h"
#include "taperquery/tq_apply_service.h"

#include <cstddef>
#include <cstdint>

namespace bytetaper::taperquery {
class TqApplyAuditStore;
}

namespace bytetaper::admin {

struct TaperQueryAdminHttpServerConfig {
    const char* listen_address = "127.0.0.1";
    std::uint16_t port = 0;

    runtime::RuntimePolicyStore* policy_store = nullptr;
    taperquery::TqApplyService* apply_service = nullptr;
    taperquery::TqApplyAuditStore* audit_store = nullptr;

    bool enable_taperquery_apply = false;

    std::size_t max_request_bytes = 1024 * 1024;
};

struct TaperQueryAdminHttpServerHandle {
    void* impl = nullptr;
    std::uint16_t bound_port = 0;
};

bool start_taperquery_admin_http_server(const TaperQueryAdminHttpServerConfig& config,
                                        TaperQueryAdminHttpServerHandle* handle);

void stop_taperquery_admin_http_server(TaperQueryAdminHttpServerHandle* handle);

} // namespace bytetaper::admin

#endif // BYTETAPER_TAPERQUERY_ADMIN_HTTP_SERVER_H
