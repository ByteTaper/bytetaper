# Cache Invalidation Contract

ByteTaper supports a declarative cache invalidation contract that allows mutation routes (e.g. `PATCH`, `PUT`, `DELETE`) to invalidate cached data on specific cache-store routes.

**NOTE: The runtime execution of this invalidation policy is not yet implemented. This document describes the declarative contract limits and syntax that is validated and verified by the compiler and CI pipelines.**

## Constraints

- Cache invalidation can only be defined on **mutation routes** (`PATCH`, `PUT`, `DELETE`).
- Target routes must be explicitly **GET** routes.
- Target routes must be configured with `cache.enabled: true` and `cache.behavior: store`.
- A mutation route may target up to 8 `store` routes for invalidation.
- The default and only supported `timing` is `after_successful_upstream_response`.
- Valid status bounds are `min >= 100` and `max <= 599`.

## Examples

### YAML Configuration

```yaml
routes:
  - id: "get_user"
    match: { kind: "exact", prefix: "/user" }
    method: "get"
    cache:
      enabled: true
      behavior: "store"
      ttl_seconds: 300

  - id: "update_user_patch"
    match: { kind: "exact", prefix: "/user" }
    method: "patch"
    cache:
      invalidation:
        enabled: true
        on_methods: ["PATCH"]
        targets:
          - route_id: "get_user"
            strategy: "route_epoch"

  - id: "update_user_put"
    match: { kind: "exact", prefix: "/user" }
    method: "put"
    cache:
      invalidation:
        enabled: true
        on_methods: ["PUT"]
        targets:
          - route_id: "get_user"
            strategy: "route_epoch"

  - id: "delete_user"
    match: { kind: "exact", prefix: "/user" }
    method: "delete"
    cache:
      invalidation:
        enabled: true
        on_methods: ["DELETE"]
        targets:
          - route_id: "get_user"
            strategy: "route_epoch"
```

### TaperQuery DSL Configuration

```taperquery
route "get_user" when method GET and path exact "/user" {
    cache store ttl 300s;
}

route "update_user_patch" when method PATCH and path exact "/user" {
    cache invalidation {
        enabled true
        on_methods ["PATCH"]
        target "get_user" { strategy "route_epoch" }
    }
}

route "update_user_put" when method PUT and path exact "/user" {
    cache invalidation {
        enabled true
        on_methods ["PUT"]
        target "get_user" { strategy "route_epoch" }
    }
}

route "delete_user" when method DELETE and path exact "/user" {
    cache invalidation {
        enabled true
        on_methods ["DELETE"]
        target "get_user" { strategy "route_epoch" }
    }
}
```
