# TaperQuery Policy IR Snapshots

This directory contains hand-reviewed canonical reference snapshots representing the **expected semantic ground truth** for each YAML configuration under `examples/policy/`. 

Snapshots do not merely record current loader output; they represent the exact targets our platform integration is contractually bound to deliver.

---

## Testing Workflows

### 1. Normal Compare Mode (Default)
By default, running tests validates that loaded structures perfectly match these snapshots. On any mismatch, the test suite prints precise field-level diagnostics detailing why and where the policy has lost semantic data.
To run comparison validation:
```bash
ctest --test-dir build -R taperquery_policy_parity --output-on-failure
```

### 2. Snapshot Update Mode (Opt-In)
If you intentionally modify policy semantics in `examples/policy/` or fix a loader parity gap, you can automatically regenerate the reference snapshots using the `BYTETAPER_UPDATE_TQ_SNAPSHOTS` environment variable:
```bash
BYTETAPER_UPDATE_TQ_SNAPSHOTS=1 ctest --test-dir build -R taperquery_policy_parity
```

> [!IMPORTANT]
> Snapshot updates should **only** be executed after intentionally updating policy specifications or successfully enhancing loader translation capability. Do not run in update mode to mask accidental loader regressions or bypass validation requirements.
