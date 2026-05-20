// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_POLICY_APPLY_STATUS_H
#define BYTETAPER_CONTROL_PLANE_POLICY_APPLY_STATUS_H

namespace bytetaper::control_plane {

enum class PolicyApplyStatus {
    DryRunReady,
    DiffReady,
    Accepted,
    Applied,
    PolicyCommittedNotActivated,
    PolicyActivationInProgress,
    PolicyActivationFailed,
    PolicyActiveCleanupPending,
    PolicyActive,
    RejectedInvalidRequest,
    RejectedParseError,
    RejectedCompileError,
    RejectedValidation,
    RejectedCasMismatch,
    RejectedNoChanges,
    RejectedPolicyInactive,
    RejectedQueueFull,
    RejectedStorageUnavailable,
    RejectedConfirmationRequired,
    RollbackPlanReady,
    RollbackAccepted,
    RepairLocalAccepted,
    RepairLocalPlanReady,
    AdoptLocalAccepted,
    AdoptLocalPlanReady,
    InternalError,
};

const char* to_string(PolicyApplyStatus status);

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_POLICY_APPLY_STATUS_H
