// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "coalescing/coalescing_decision.h"

#include "metrics/coalescing_metrics.h"
#include "observability/trace.h"

namespace bytetaper::coalescing {

std::string_view get_decision_reason_string(CoalescingDecisionReason reason) {
    switch (reason) {
    case CoalescingDecisionReason::PolicyDisabled:
        return "policy_disabled";
    case CoalescingDecisionReason::MethodNotGet:
        return "method_not_get";
    case CoalescingDecisionReason::AuthenticatedRequest:
        return "authenticated_request";
    case CoalescingDecisionReason::MissingKey:
        return "missing_key";
    case CoalescingDecisionReason::LeaderCreated:
        return "leader_created";
    case CoalescingDecisionReason::FollowerJoined:
        return "follower_joined";
    case CoalescingDecisionReason::TooManyWaiters:
        return "too_many_waiters";
    case CoalescingDecisionReason::WaitWindowExpired:
        return "wait_window_expired";
    }
    return "unknown";
}

CoalescingDecision evaluate_coalescing_decision(InFlightRegistry* registry,
                                                const CoalescingDecisionContext& context) {
    CoalescingDecision decision;

    // 1. Policy Check
    if (context.policy == nullptr || !context.policy->enabled) {
        decision.action = CoalescingAction::Bypass;
        decision.reason = CoalescingDecisionReason::PolicyDisabled;
        decision.upstream_reason = metrics::UpstreamCallReason::CoalescingDisabled;
        record_coalescing_event(context.metrics, metrics::CoalescingMetricEvent::Bypass);
        return decision;
    }

    // 2. Eligibility Check (Method)
    CoalescingEligibility eligibility = evaluate_coalescing_eligibility(context.method);
    if (!eligibility.is_eligible) {
        decision.action = CoalescingAction::Bypass;
        decision.reason = CoalescingDecisionReason::MethodNotGet;
        decision.upstream_reason = metrics::UpstreamCallReason::Bypass;
        record_coalescing_event(context.metrics, metrics::CoalescingMetricEvent::Bypass);
        return decision;
    }

    // 3. Safety Check (Authentication)
    CoalescingEligibility safety = evaluate_coalescing_safety(context.safety_input);
    if (!safety.is_eligible) {
        decision.action = CoalescingAction::Bypass;
        decision.reason = CoalescingDecisionReason::AuthenticatedRequest;
        decision.upstream_reason = metrics::UpstreamCallReason::Bypass;
        record_coalescing_event(context.metrics, metrics::CoalescingMetricEvent::Bypass);
        return decision;
    }

    // 4. Key Construction
    observability::TraceSpanScope key_span{};
    if (context.trace != nullptr) {
        key_span = observability::trace_start_span(
            context.trace, observability::kSpanCoalescingKeyBuild,
            observability::TraceLatencyClass::ActiveProcessingDetail, &context.trace->root_span_id);
    }
    bool built = build_coalescing_key(context.key_input, decision.key, sizeof(decision.key));
    key_span.end();

    if (!built) {
        decision.action = CoalescingAction::Bypass;
        decision.reason = CoalescingDecisionReason::MissingKey;
        decision.upstream_reason = metrics::UpstreamCallReason::Bypass;
        record_coalescing_event(context.metrics, metrics::CoalescingMetricEvent::Bypass);
        return decision;
    }

    // 5. Registry Registration
    observability::TraceSpanScope lookup_span{};
    if (context.trace != nullptr) {
        lookup_span = observability::trace_start_span(
            context.trace, observability::kSpanCoalescingInflightLookup,
            observability::TraceLatencyClass::ActiveProcessingDetail, &context.trace->root_span_id);
    }
    RegistryRegistrationResult reg_res =
        registry_register(registry, decision.key, context.now_ms, context.policy->wait_window_ms,
                          context.policy->max_waiters_per_key, context.request_id);
    lookup_span.end();

    // Propagate all rich telemetry fields to decision
    decision.attach_failure_reason = reg_res.attach_failure_reason;
    decision.state_before = reg_res.state_before;
    decision.state_after = reg_res.state_after;
    decision.key_hash = reg_res.key_hash;
    decision.group_id = reg_res.group_id;
    decision.lifecycle_generation = reg_res.lifecycle_generation;
    decision.leader_request_id = reg_res.leader_request_id;
    decision.terminal_result_join_flag = reg_res.terminal_result_join_flag;

    // 6. Mapping Registry Result and Recording Events
    observability::TraceSpanScope decide_span{};
    if (context.trace != nullptr) {
        decide_span = observability::trace_start_span(
            context.trace, observability::kSpanCoalescingRoleDecide,
            observability::TraceLatencyClass::ActiveProcessingDetail, &context.trace->root_span_id);
    }

    switch (reg_res.role) {
    case InFlightRole::Leader:
        decision.action = CoalescingAction::Leader;
        decision.reason = CoalescingDecisionReason::LeaderCreated;
        if (is_terminal(reg_res.state_before)) {
            decision.upstream_reason = metrics::UpstreamCallReason::EntryAlreadyTerminal;
        } else {
            decision.upstream_reason = metrics::UpstreamCallReason::NoInflightEntry;
        }
        record_coalescing_event(context.metrics, metrics::CoalescingMetricEvent::Leader);
        record_coalescing_event(context.metrics, metrics::CoalescingMetricEvent::AttachSuccess);
        break;
    case InFlightRole::Follower:
        decision.action = CoalescingAction::Follower;
        decision.reason = CoalescingDecisionReason::FollowerJoined;
        decision.upstream_reason =
            metrics::UpstreamCallReason::Unknown; // only goes upstream if it times out/falls back
        record_coalescing_event(context.metrics, metrics::CoalescingMetricEvent::Follower);
        record_coalescing_event(context.metrics, metrics::CoalescingMetricEvent::AttachSuccess);
        if (reg_res.terminal_result_join_flag) {
            record_coalescing_event(context.metrics,
                                    metrics::CoalescingMetricEvent::ResultReadyRetention);
        }
        break;
    case InFlightRole::Reject:
        decision.action = CoalescingAction::Reject;
        decision.reason = CoalescingDecisionReason::TooManyWaiters;
        decision.upstream_reason = metrics::UpstreamCallReason::Unknown;
        record_coalescing_event(context.metrics, metrics::CoalescingMetricEvent::TooManyWaiters);
        if (reg_res.attach_failure_reason == AttachFailureReason::ShardFull) {
            record_coalescing_event(context.metrics,
                                    metrics::CoalescingMetricEvent::AttachFailureShardFull);
        } else if (reg_res.attach_failure_reason == AttachFailureReason::MaxWaitersEnforced) {
            record_coalescing_event(context.metrics,
                                    metrics::CoalescingMetricEvent::AttachFailureMaxWaiters);
        } else if (reg_res.attach_failure_reason == AttachFailureReason::StateMismatch) {
            record_coalescing_event(context.metrics,
                                    metrics::CoalescingMetricEvent::AttachFailureStateMismatch);
        }
        break;
    }

    decide_span.end();
    return decision;
}

} // namespace bytetaper::coalescing
