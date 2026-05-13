// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "admin/taperquery_admin_http_server.h"

#include <arpa/inet.h>
#include <atomic>
#include <cctype>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace bytetaper::admin {

namespace {

struct ParsedJson {
    bool ok = false;
    std::string error;

    std::string source_type;
    std::string source;
    std::string expected_base_identity;
    std::string mode;
    std::string operator_id;
    std::string request_id;
    bool include_unchanged_routes = false;
    bool include_field_level_changes = true;
    bool strict_production = true;
};

static ParsedJson parse_apply_json(const std::string& body) {
    ParsedJson parsed;
    parsed.ok = true;

    size_t pos = 0;
    pos = body.find('{');
    if (pos == std::string::npos) {
        parsed.ok = false;
        parsed.error = "Missing opening brace '{'";
        return parsed;
    }
    pos++;

    auto skip_whitespace = [&](size_t& p) {
        while (p < body.size() && (body[p] == ' ' || body[p] == '\t' || body[p] == '\r' ||
                                   body[p] == '\n' || body[p] == ',')) {
            p++;
        }
    };

    auto parse_string = [&](size_t& p, std::string& out_str) -> bool {
        if (p >= body.size() || body[p] != '"')
            return false;
        p++; // skip starting quote
        out_str.clear();
        while (p < body.size()) {
            if (body[p] == '"') {
                p++; // skip ending quote
                return true;
            }
            if (body[p] == '\\') {
                p++;
                if (p >= body.size())
                    return false;
                if (body[p] == '"')
                    out_str.push_back('"');
                else if (body[p] == '\\')
                    out_str.push_back('\\');
                else if (body[p] == 'n')
                    out_str.push_back('\n');
                else if (body[p] == 't')
                    out_str.push_back('\t');
                else if (body[p] == 'r')
                    out_str.push_back('\r');
                else
                    out_str.push_back(body[p]);
            } else {
                out_str.push_back(body[p]);
            }
            p++;
        }
        return false;
    };

    while (pos < body.size()) {
        skip_whitespace(pos);
        if (pos >= body.size() || body[pos] == '}') {
            break;
        }

        std::string key;
        if (!parse_string(pos, key)) {
            parsed.ok = false;
            parsed.error = "Malformed key in JSON";
            return parsed;
        }

        skip_whitespace(pos);
        if (pos >= body.size() || body[pos] != ':') {
            parsed.ok = false;
            parsed.error = "Missing colon after key '" + key + "'";
            return parsed;
        }
        pos++; // skip ':'

        skip_whitespace(pos);
        if (pos >= body.size()) {
            parsed.ok = false;
            parsed.error = "Missing value for key '" + key + "'";
            return parsed;
        }

        if (body[pos] == '"') {
            std::string val;
            if (!parse_string(pos, val)) {
                parsed.ok = false;
                parsed.error = "Malformed string value for key '" + key + "'";
                return parsed;
            }
            if (key == "source_type")
                parsed.source_type = val;
            else if (key == "source")
                parsed.source = val;
            else if (key == "expected_base_identity")
                parsed.expected_base_identity = val;
            else if (key == "mode")
                parsed.mode = val;
            else if (key == "operator_id")
                parsed.operator_id = val;
            else if (key == "request_id")
                parsed.request_id = val;
        } else {
            size_t start = pos;
            while (pos < body.size() && body[pos] != ' ' && body[pos] != '\t' &&
                   body[pos] != '\r' && body[pos] != '\n' && body[pos] != ',' && body[pos] != '}') {
                pos++;
            }
            std::string val = body.substr(start, pos - start);
            bool is_true = (val == "true");
            bool is_false = (val == "false");
            if (!is_true && !is_false) {
                parsed.ok = false;
                parsed.error = "Unsupported unquoted value '" + val + "' for key '" + key + "'";
                return parsed;
            }
            if (key == "include_unchanged_routes")
                parsed.include_unchanged_routes = is_true;
            else if (key == "include_field_level_changes")
                parsed.include_field_level_changes = is_true;
            else if (key == "strict_production")
                parsed.strict_production = is_true;
        }
    }

    return parsed;
}

static std::string escape_json_string(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (char c : input) {
        if (c == '"')
            out += "\\\"";
        else if (c == '\\')
            out += "\\\\";
        else if (c == '\n')
            out += "\\n";
        else if (c == '\t')
            out += "\\t";
        else if (c == '\r')
            out += "\\r";
        else if (static_cast<unsigned char>(c) < 32) {
            char hex[8];
            std::snprintf(hex, sizeof(hex), "\\u%04x", static_cast<int>(c));
            out += hex;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

static std::string status_to_string(taperquery::TqApplyStatus s) {
    switch (s) {
    case taperquery::TqApplyStatus::Applied:
        return "Applied";
    case taperquery::TqApplyStatus::DryRunReady:
        return "DryRunReady";
    case taperquery::TqApplyStatus::RejectedInvalidRequest:
        return "RejectedInvalidRequest";
    case taperquery::TqApplyStatus::RejectedParseError:
        return "RejectedParseError";
    case taperquery::TqApplyStatus::RejectedCompileError:
        return "RejectedCompileError";
    case taperquery::TqApplyStatus::RejectedValidation:
        return "RejectedValidation";
    case taperquery::TqApplyStatus::RejectedRouteAnalysis:
        return "RejectedRouteAnalysis";
    case taperquery::TqApplyStatus::RejectedCasMismatch:
        return "RejectedCasMismatch";
    case taperquery::TqApplyStatus::RejectedNoChanges:
        return "RejectedNoChanges";
    case taperquery::TqApplyStatus::RejectedSnapshotBuildFailed:
        return "RejectedSnapshotBuildFailed";
    case taperquery::TqApplyStatus::InternalError:
        return "InternalError";
    }
    return "Unknown";
}

static int status_to_http_code(taperquery::TqApplyStatus s, taperquery::TqApplyMode mode) {
    switch (s) {
    case taperquery::TqApplyStatus::Applied:
        return 200;
    case taperquery::TqApplyStatus::DryRunReady:
        return 200;
    case taperquery::TqApplyStatus::RejectedInvalidRequest:
        return 400;
    case taperquery::TqApplyStatus::RejectedParseError:
        return 400;
    case taperquery::TqApplyStatus::RejectedCompileError:
        return 400;
    case taperquery::TqApplyStatus::RejectedCasMismatch:
        return 409;
    case taperquery::TqApplyStatus::RejectedValidation:
        return 422;
    case taperquery::TqApplyStatus::RejectedRouteAnalysis:
        return 422;
    case taperquery::TqApplyStatus::RejectedNoChanges:
        return (mode == taperquery::TqApplyMode::DryRun) ? 200 : 409;
    case taperquery::TqApplyStatus::RejectedSnapshotBuildFailed:
        return 500;
    case taperquery::TqApplyStatus::InternalError:
        return 500;
    }
    return 500;
}

static std::string apply_status_to_error_code(taperquery::TqApplyStatus s) {
    switch (s) {
    case taperquery::TqApplyStatus::RejectedInvalidRequest:
        return "INVALID_REQUEST";
    case taperquery::TqApplyStatus::RejectedParseError:
        return "PARSE_ERROR";
    case taperquery::TqApplyStatus::RejectedCompileError:
        return "COMPILE_ERROR";
    case taperquery::TqApplyStatus::RejectedCasMismatch:
        return "CAS_MISMATCH";
    case taperquery::TqApplyStatus::RejectedValidation:
        return "VALIDATION_ERROR";
    case taperquery::TqApplyStatus::RejectedRouteAnalysis:
        return "ROUTE_ANALYSIS_ERROR";
    case taperquery::TqApplyStatus::RejectedNoChanges:
        return "NO_CHANGES";
    case taperquery::TqApplyStatus::RejectedSnapshotBuildFailed:
        return "SNAPSHOT_BUILD_FAILED";
    case taperquery::TqApplyStatus::InternalError:
        return "INTERNAL_ERROR";
    default:
        return "INTERNAL_ERROR";
    }
}

static std::string render_apply_result(const taperquery::TqApplyResult& res) {
    std::string out = "{";
    out += "\"ok\":" + std::string(res.ok ? "true" : "false") + ",";
    if (!res.ok) {
        out += "\"error_code\":\"" + escape_json_string(apply_status_to_error_code(res.status)) +
               "\",";
    }
    out += "\"status\":\"" + escape_json_string(status_to_string(res.status)) + "\",";
    out += "\"message\":\"" + escape_json_string(res.message) + "\",";
    out +=
        "\"current_policy_identity\":\"" + escape_json_string(res.current_policy_identity) + "\",";
    out += "\"candidate_policy_identity\":\"" + escape_json_string(res.candidate_policy_identity) +
           "\",";
    out += "\"expected_base_identity\":\"" + escape_json_string(res.expected_base_identity) + "\",";
    out +=
        "\"applied_policy_identity\":\"" + escape_json_string(res.applied_policy_identity) + "\",";
    out += "\"before_generation\":" + std::to_string(res.before_generation) + ",";
    out += "\"after_generation\":" + std::to_string(res.after_generation) + ",";

    // 1. Render plan
    out += "\"plan\":{";
    out += "\"ok\":" + std::string(res.plan.ok ? "true" : "false") + ",";
    out += "\"before_policy_identity\":\"" + escape_json_string(res.plan.before_policy_identity) +
           "\",";
    out +=
        "\"after_policy_identity\":\"" + escape_json_string(res.plan.after_policy_identity) + "\",";
    out += "\"expected_base_identity\":\"" + escape_json_string(res.plan.expected_base_identity) +
           "\",";

    // Render route_changes
    out += "\"route_changes\":[";
    for (size_t i = 0; i < res.plan.route_changes.size(); ++i) {
        if (i > 0)
            out += ",";
        const auto& rc = res.plan.route_changes[i];
        out += "{";
        out += "\"route_id\":\"" + escape_json_string(rc.route_id) + "\",";

        std::string kind_str = "Unchanged";
        if (rc.kind == taperquery::TqRouteChangeKind::Added)
            kind_str = "Added";
        else if (rc.kind == taperquery::TqRouteChangeKind::Removed)
            kind_str = "Removed";
        else if (rc.kind == taperquery::TqRouteChangeKind::Modified)
            kind_str = "Modified";
        out += "\"kind\":\"" + kind_str + "\",";
        out += "\"before_identity\":\"" + escape_json_string(rc.before_identity) + "\",";
        out += "\"after_identity\":\"" + escape_json_string(rc.after_identity) + "\",";

        // Render field_changes
        out += "\"field_changes\":[";
        for (size_t j = 0; j < rc.field_changes.size(); ++j) {
            if (j > 0)
                out += ",";
            const auto& fc = rc.field_changes[j];
            out += "{";
            out += "\"field_path\":\"" + escape_json_string(fc.field_path) + "\",";
            out += "\"before\":\"" + escape_json_string(fc.before) + "\",";
            out += "\"after\":\"" + escape_json_string(fc.after) + "\",";

            std::string impact_str = "None";
            if (fc.impact == taperquery::TqSemanticImpact::MatchBehavior)
                impact_str = "MatchBehavior";
            else if (fc.impact == taperquery::TqSemanticImpact::MutationBehavior)
                impact_str = "MutationBehavior";
            else if (fc.impact == taperquery::TqSemanticImpact::CacheBehavior)
                impact_str = "CacheBehavior";
            else if (fc.impact == taperquery::TqSemanticImpact::CacheKeyBehavior)
                impact_str = "CacheKeyBehavior";
            else if (fc.impact == taperquery::TqSemanticImpact::CacheStorageBehavior)
                impact_str = "CacheStorageBehavior";
            else if (fc.impact == taperquery::TqSemanticImpact::FieldFilteringBehavior)
                impact_str = "FieldFilteringBehavior";
            else if (fc.impact == taperquery::TqSemanticImpact::PaginationBehavior)
                impact_str = "PaginationBehavior";
            else if (fc.impact == taperquery::TqSemanticImpact::CompressionBehavior)
                impact_str = "CompressionBehavior";
            else if (fc.impact == taperquery::TqSemanticImpact::CoalescingBehavior)
                impact_str = "CoalescingBehavior";
            else if (fc.impact == taperquery::TqSemanticImpact::FailureBehavior)
                impact_str = "FailureBehavior";
            else if (fc.impact == taperquery::TqSemanticImpact::RuntimeCompatibility)
                impact_str = "RuntimeCompatibility";
            out += "\"impact\":\"" + impact_str + "\"";
            out += "}";
        }
        out += "]";
        out += "}";
    }
    out += "],";

    // Render issues
    out += "\"issues\":[";
    for (size_t i = 0; i < res.plan.issues.size(); ++i) {
        if (i > 0)
            out += ",";
        const auto& issue = res.plan.issues[i];
        out += "{";
        std::string sev_str = "Info";
        if (issue.severity == taperquery::TqPlanSeverity::Warning)
            sev_str = "Warning";
        else if (issue.severity == taperquery::TqPlanSeverity::Blocker)
            sev_str = "Blocker";
        out += "\"severity\":\"" + sev_str + "\",";
        out += "\"code\":\"" + escape_json_string(issue.code) + "\",";
        out += "\"route_id\":\"" + escape_json_string(issue.route_id) + "\",";
        out += "\"reason\":\"" + escape_json_string(issue.reason) + "\",";
        out += "\"hint\":\"" + escape_json_string(issue.hint) + "\"";
        out += "}";
    }
    out += "]";
    out += "},";

    // 2. Render route_analysis
    out += "\"route_analysis\":{";
    out += "\"ok\":" + std::string(res.route_analysis.ok ? "true" : "false") + ",";

    // Render precedence
    out += "\"precedence\":[";
    for (size_t i = 0; i < res.route_analysis.precedence.size(); ++i) {
        if (i > 0)
            out += ",";
        const auto& p = res.route_analysis.precedence[i];
        out += "{";
        out += "\"route_id\":\"" + escape_json_string(p.route_id) + "\",";
        out += "\"method\":\"" + escape_json_string(p.method) + "\",";
        out += "\"match_kind\":\"" + escape_json_string(p.match_kind) + "\",";
        out += "\"path\":\"" + escape_json_string(p.path) + "\",";
        out += "\"declaration_index\":" + std::to_string(p.declaration_index) + ",";
        out += "\"specificity_score\":" + std::to_string(p.specificity_score) + ",";
        out += "\"may_shadow_later_routes\":" +
               std::string(p.may_shadow_later_routes ? "true" : "false") + ",";
        out += "\"may_be_shadowed\":" + std::string(p.may_be_shadowed ? "true" : "false");
        out += "}";
    }
    out += "],";

    // Render findings
    out += "\"findings\":[";
    for (size_t i = 0; i < res.route_analysis.findings.size(); ++i) {
        if (i > 0)
            out += ",";
        const auto& f = res.route_analysis.findings[i];
        out += "{";
        std::string sev_str = "Warning";
        if (f.severity == taperquery::TqRouteAnalysisSeverity::Info)
            sev_str = "Info";
        else if (f.severity == taperquery::TqRouteAnalysisSeverity::Error)
            sev_str = "Error";
        out += "\"severity\":\"" + sev_str + "\",";

        std::string kind_str = "PrefixShadowsPrefix";
        if (f.kind == taperquery::TqRouteAnalysisKind::DuplicateRouteId)
            kind_str = "DuplicateRouteId";
        else if (f.kind == taperquery::TqRouteAnalysisKind::DuplicateExactMatch)
            kind_str = "DuplicateExactMatch";
        else if (f.kind == taperquery::TqRouteAnalysisKind::EquivalentPrefixMatch)
            kind_str = "EquivalentPrefixMatch";
        else if (f.kind == taperquery::TqRouteAnalysisKind::PrefixShadowsExact)
            kind_str = "PrefixShadowsExact";
        else if (f.kind == taperquery::TqRouteAnalysisKind::EarlierRouteWins)
            kind_str = "EarlierRouteWins";
        else if (f.kind == taperquery::TqRouteAnalysisKind::SharedL2CachePath)
            kind_str = "SharedL2CachePath";

        out += "\"kind\":\"" + kind_str + "\",";
        out += "\"route_id\":\"" + escape_json_string(f.route_id) + "\",";
        out += "\"related_route_id\":\"" + escape_json_string(f.related_route_id) + "\",";
        out += "\"field_path\":\"" + escape_json_string(f.field_path) + "\",";
        out += "\"reason\":\"" + escape_json_string(f.reason) + "\",";
        out += "\"hint\":\"" + escape_json_string(f.hint) + "\"";
        out += "}";
    }
    out += "]";
    out += "},";

    // 3. Render diagnostics
    out += "\"diagnostics\":[";
    for (size_t i = 0; i < res.diagnostics.size(); ++i) {
        if (i > 0)
            out += ",";
        const auto& d = res.diagnostics[i];
        out += "{";
        out += "\"severity\":\"" + escape_json_string(d.severity) + "\",";
        out += "\"code\":\"" + escape_json_string(d.code) + "\",";
        out += "\"route_id\":\"" + escape_json_string(d.route_id) + "\",";
        out += "\"field_path\":\"" + escape_json_string(d.field_path) + "\",";
        out += "\"reason\":\"" + escape_json_string(d.reason) + "\",";
        out += "\"hint\":\"" + escape_json_string(d.hint) + "\"";
        out += "}";
    }
    out += "]";

    out += "}";
    return out;
}

struct TaperQueryAdminHttpServerImpl {
    TaperQueryAdminHttpServerConfig config;
    int listen_fd = -1;
    std::atomic<bool> stop_flag{ false };
    std::thread accept_thread{};
};

static void send_response(int conn_fd, int status_code, const std::string& status_text,
                          const std::string& content_type, const std::string& body) {
    std::string response = "HTTP/1.1 " + std::to_string(status_code) + " " + status_text + "\r\n";
    response += "Content-Type: " + content_type + "\r\n";
    response += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    response += "Connection: close\r\n";
    response += "\r\n";
    response += body;

    ssize_t total_sent = 0;
    while (total_sent < (ssize_t) response.size()) {
        ssize_t n = write(conn_fd, response.data() + total_sent, response.size() - total_sent);
        if (n <= 0)
            break;
        total_sent += n;
    }
}

static void handle_connection(int conn_fd, TaperQueryAdminHttpServerImpl* impl) {
    const auto& config = impl->config;
    char chunk[1024];
    std::string request_headers;
    size_t header_end_pos = std::string::npos;

    while (true) {
        ssize_t n = read(conn_fd, chunk, sizeof(chunk));
        if (n <= 0)
            break;
        request_headers.append(chunk, n);
        header_end_pos = request_headers.find("\r\n\r\n");
        if (header_end_pos != std::string::npos)
            break;
        if (request_headers.size() > config.max_request_bytes) {
            send_response(conn_fd, 413, "Payload Too Large", "application/json",
                          "{\"ok\":false,\"error_code\":\"PAYLOAD_TOO_LARGE\",\"message\":"
                          "\"Request exceeds limit of " +
                              std::to_string(config.max_request_bytes) + " bytes.\"}");
            close(conn_fd);
            return;
        }
    }

    if (header_end_pos == std::string::npos) {
        send_response(conn_fd, 400, "Bad Request", "application/json",
                      "{\"ok\":false,\"error_code\":\"BAD_REQUEST\",\"message\":\"Incomplete HTTP "
                      "headers.\"}");
        close(conn_fd);
        return;
    }

    size_t first_line_end = request_headers.find("\r\n");
    if (first_line_end == std::string::npos) {
        send_response(conn_fd, 400, "Bad Request", "application/json",
                      "{\"ok\":false,\"error_code\":\"BAD_REQUEST\",\"message\":\"Malformed "
                      "request line.\"}");
        close(conn_fd);
        return;
    }

    std::string first_line = request_headers.substr(0, first_line_end);
    size_t space1 = first_line.find(' ');
    if (space1 == std::string::npos) {
        send_response(conn_fd, 400, "Bad Request", "application/json",
                      "{\"ok\":false,\"error_code\":\"BAD_REQUEST\",\"message\":\"Malformed "
                      "request line.\"}");
        close(conn_fd);
        return;
    }

    std::string method = first_line.substr(0, space1);
    size_t space2 = first_line.find(' ', space1 + 1);
    if (space2 == std::string::npos) {
        send_response(conn_fd, 400, "Bad Request", "application/json",
                      "{\"ok\":false,\"error_code\":\"BAD_REQUEST\",\"message\":\"Malformed "
                      "request line.\"}");
        close(conn_fd);
        return;
    }

    std::string path = first_line.substr(space1 + 1, space2 - (space1 + 1));

    size_t content_length = 0;
    bool has_content_length = false;

    size_t pos = first_line_end + 2;
    while (pos < header_end_pos) {
        size_t next_line = request_headers.find("\r\n", pos);
        if (next_line == std::string::npos || next_line > header_end_pos) {
            next_line = header_end_pos;
        }
        std::string line = request_headers.substr(pos, next_line - pos);
        pos = next_line + 2;

        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string name = line.substr(0, colon);
            while (!name.empty() && name.back() == ' ')
                name.pop_back();
            for (char& c : name)
                c = std::tolower(c);

            if (name == "content-length") {
                std::string val = line.substr(colon + 1);
                size_t val_start = 0;
                while (val_start < val.size() && val[val_start] == ' ')
                    val_start++;
                size_t val_end = val.size();
                while (val_end > val_start && val[val_end - 1] == ' ')
                    val_end--;
                val = val.substr(val_start, val_end - val_start);

                bool valid = !val.empty();
                for (char c : val) {
                    if (!std::isdigit(c)) {
                        valid = false;
                        break;
                    }
                }
                if (valid) {
                    content_length = 0;
                    for (char c : val) {
                        content_length = content_length * 10 + (c - '0');
                    }
                    has_content_length = true;
                } else {
                    send_response(conn_fd, 400, "Bad Request", "application/json",
                                  "{\"ok\":false,\"error_code\":\"BAD_REQUEST\",\"message\":"
                                  "\"Invalid Content-Length header.\"}");
                    close(conn_fd);
                    return;
                }
            }
        }
    }

    std::string body;
    size_t body_start = header_end_pos + 4;
    if (body_start < request_headers.size()) {
        body = request_headers.substr(body_start);
    }

    if (method == "POST") {
        if (!has_content_length) {
            send_response(conn_fd, 411, "Length Required", "application/json",
                          "{\"ok\":false,\"error_code\":\"LENGTH_REQUIRED\",\"message\":\"Content-"
                          "Length header is required for POST requests.\"}");
            close(conn_fd);
            return;
        }

        if (content_length > config.max_request_bytes) {
            send_response(conn_fd, 413, "Payload Too Large", "application/json",
                          "{\"ok\":false,\"error_code\":\"PAYLOAD_TOO_LARGE\",\"message\":"
                          "\"Request body exceeds limit of " +
                              std::to_string(config.max_request_bytes) + " bytes.\"}");
            close(conn_fd);
            return;
        }

        while (body.size() < content_length) {
            size_t to_read = content_length - body.size();
            if (to_read > sizeof(chunk))
                to_read = sizeof(chunk);
            ssize_t n = read(conn_fd, chunk, to_read);
            if (n <= 0)
                break;
            body.append(chunk, n);
        }

        if (body.size() < content_length) {
            send_response(conn_fd, 400, "Bad Request", "application/json",
                          "{\"ok\":false,\"error_code\":\"BAD_REQUEST\",\"message\":\"Incomplete "
                          "request body.\"}");
            close(conn_fd);
            return;
        }

        if (body.size() > content_length) {
            body.resize(content_length);
        }
    }

    if (path == "/admin/taperquery/policy/current") {
        if (method != "GET") {
            send_response(
                conn_fd, 405, "Method Not Allowed", "application/json",
                "{\"ok\":false,\"error_code\":\"METHOD_NOT_ALLOWED\",\"message\":\"Method " +
                    method + " not allowed for " + path + ".\"}");
            close(conn_fd);
            return;
        }

        if (config.policy_store == nullptr) {
            send_response(conn_fd, 503, "Service Unavailable", "application/json",
                          "{\"ok\":false,\"error_code\":\"NO_ACTIVE_POLICY\",\"message\":\"Policy "
                          "store is not available.\"}");
            close(conn_fd);
            return;
        }

        auto snapshot = config.policy_store->load();
        if (!snapshot) {
            send_response(conn_fd, 503, "Service Unavailable", "application/json",
                          "{\"ok\":false,\"error_code\":\"NO_ACTIVE_POLICY\",\"message\":\"No "
                          "active runtime policy snapshot is installed.\"}");
            close(conn_fd);
            return;
        }

        std::string res_body = "{";
        res_body += "\"ok\":true,";
        res_body +=
            "\"policy_identity\":\"" + escape_json_string(snapshot->policy_identity) + "\",";
        res_body += "\"generation\":" + std::to_string(snapshot->generation) + ",";
        res_body += "\"route_count\":" + std::to_string(snapshot->routes.size()) + ",";
        res_body += "\"source_name\":\"" + escape_json_string(snapshot->source_name) + "\",";
        res_body += "\"version\":{";
        res_body += "\"source_schema_version\":\"" +
                    escape_json_string(snapshot->policy_ir.version.source_schema_version) + "\",";
        res_body += "\"policy_ir_version\":\"" +
                    escape_json_string(snapshot->policy_ir.version.policy_ir_version) + "\",";
        res_body += "\"identity_version\":\"" +
                    escape_json_string(snapshot->policy_ir.version.identity_version) + "\",";
        res_body += "\"emitter_version\":\"" +
                    escape_json_string(snapshot->policy_ir.version.emitter_version) + "\",";
        res_body += "\"runtime_min_version\":\"" +
                    escape_json_string(snapshot->policy_ir.version.runtime_min_version) + "\",";
        res_body += "\"runtime_capability_profile\":\"" +
                    escape_json_string(snapshot->policy_ir.version.runtime_capability_profile) +
                    "\"";
        res_body += "}";
        res_body += "}";

        send_response(conn_fd, 200, "OK", "application/json", res_body);
        close(conn_fd);
        return;
    }

    if (path == "/admin/taperquery/apply") {
        if (method != "POST") {
            send_response(
                conn_fd, 405, "Method Not Allowed", "application/json",
                "{\"ok\":false,\"error_code\":\"METHOD_NOT_ALLOWED\",\"message\":\"Method " +
                    method + " not allowed for " + path + ".\"}");
            close(conn_fd);
            return;
        }

        if (!config.enable_taperquery_apply) {
            send_response(
                conn_fd, 403, "Forbidden", "application/json",
                "{\"ok\":false,\"error_code\":\"FORBIDDEN\",\"message\":\"TaperQuery apply "
                "endpoint is disabled. Enable using --admin-enable-taperquery flag.\"}");
            close(conn_fd);
            return;
        }

        if (config.apply_service == nullptr) {
            send_response(conn_fd, 503, "Service Unavailable", "application/json",
                          "{\"ok\":false,\"error_code\":\"SERVICE_UNAVAILABLE\",\"message\":"
                          "\"TaperQuery apply service is not available.\"}");
            close(conn_fd);
            return;
        }

        ParsedJson parsed = parse_apply_json(body);
        if (!parsed.ok) {
            send_response(conn_fd, 400, "Bad Request", "application/json",
                          "{\"ok\":false,\"error_code\":\"INVALID_REQUEST\",\"message\":\"" +
                              escape_json_string(parsed.error) + "\"}");
            close(conn_fd);
            return;
        }

        if (parsed.source_type.empty() || parsed.source.empty() || parsed.mode.empty() ||
            parsed.expected_base_identity.empty()) {
            send_response(conn_fd, 400, "Bad Request", "application/json",
                          "{\"ok\":false,\"error_code\":\"INVALID_REQUEST\",\"message\":\"Required "
                          "fields: 'source_type', 'source', 'expected_base_identity', 'mode'.\"}");
            close(conn_fd);
            return;
        }

        if (parsed.source_type != "taperquery") {
            send_response(conn_fd, 400, "Bad Request", "application/json",
                          "{\"ok\":false,\"error_code\":\"INVALID_REQUEST\",\"message\":"
                          "\"Unsupported source_type. Supported values: taperquery.\"}");
            close(conn_fd);
            return;
        }

        taperquery::TqApplyMode app_mode;
        if (parsed.mode == "dry_run") {
            app_mode = taperquery::TqApplyMode::DryRun;
        } else if (parsed.mode == "apply") {
            app_mode = taperquery::TqApplyMode::Apply;
        } else {
            send_response(conn_fd, 400, "Bad Request", "application/json",
                          "{\"ok\":false,\"error_code\":\"INVALID_REQUEST\",\"message\":"
                          "\"Unsupported mode. Supported values: dry_run, apply.\"}");
            close(conn_fd);
            return;
        }

        taperquery::TqApplyRequest apply_req;
        apply_req.source_type = taperquery::TqApplySourceType::TaperQuery;
        apply_req.mode = app_mode;
        apply_req.source = parsed.source;
        apply_req.expected_base_identity = parsed.expected_base_identity;
        apply_req.operator_id = parsed.operator_id;
        apply_req.request_id = parsed.request_id;
        apply_req.include_unchanged_routes = parsed.include_unchanged_routes;
        apply_req.include_field_level_changes = parsed.include_field_level_changes;
        apply_req.strict_production = parsed.strict_production;

        auto result = config.apply_service->execute(apply_req);

        std::string res_body = render_apply_result(result);
        int http_status = status_to_http_code(result.status, app_mode);
        std::string http_status_text = "OK";
        if (http_status == 400)
            http_status_text = "Bad Request";
        else if (http_status == 409)
            http_status_text = "Conflict";
        else if (http_status == 422)
            http_status_text = "Unprocessable Entity";
        else if (http_status == 500)
            http_status_text = "Internal Server Error";

        send_response(conn_fd, http_status, http_status_text, "application/json", res_body);
        close(conn_fd);
        return;
    }

    send_response(
        conn_fd, 404, "Not Found", "application/json",
        "{\"ok\":false,\"error_code\":\"NOT_FOUND\",\"message\":\"Endpoint not found.\"}");
    close(conn_fd);
}

static void accept_loop(TaperQueryAdminHttpServerImpl* impl) {
    while (!impl->stop_flag.load(std::memory_order_relaxed)) {
        int conn_fd = accept(impl->listen_fd, nullptr, nullptr);
        if (conn_fd < 0) {
            if (impl->stop_flag.load(std::memory_order_relaxed))
                break;
            continue;
        }
        handle_connection(conn_fd, impl);
    }
}

} // namespace

bool start_taperquery_admin_http_server(const TaperQueryAdminHttpServerConfig& config,
                                        TaperQueryAdminHttpServerHandle* handle) {
    if (handle == nullptr) {
        return false;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return false;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config.port);
    inet_pton(AF_INET, config.listen_address, &addr.sin_addr);

    if (bind(fd, (struct sockaddr*) &addr, sizeof(addr)) < 0) {
        close(fd);
        return false;
    }

    if (listen(fd, 4) < 0) {
        close(fd);
        return false;
    }

    socklen_t len = sizeof(addr);
    if (getsockname(fd, (struct sockaddr*) &addr, &len) == 0) {
        handle->bound_port = ntohs(addr.sin_port);
    }

    auto* impl = new TaperQueryAdminHttpServerImpl();
    impl->config = config;
    impl->listen_fd = fd;
    impl->accept_thread = std::thread(accept_loop, impl);

    handle->impl = impl;
    return true;
}

void stop_taperquery_admin_http_server(TaperQueryAdminHttpServerHandle* handle) {
    if (handle == nullptr || handle->impl == nullptr) {
        return;
    }

    auto* impl = static_cast<TaperQueryAdminHttpServerImpl*>(handle->impl);
    impl->stop_flag.store(true, std::memory_order_relaxed);

    shutdown(impl->listen_fd, SHUT_RDWR);
    close(impl->listen_fd);

    if (impl->accept_thread.joinable()) {
        impl->accept_thread.join();
    }

    delete impl;
    handle->impl = nullptr;
    handle->bound_port = 0;
}

} // namespace bytetaper::admin
