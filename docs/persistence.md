# Repository and payment boundaries

`Repository::commit(products, orders, audit)` is the persistence extension point. It receives the proposed product/order snapshot and corresponding audit event under the checkout mutex. Return only after all changes are committed; throw before exposing any partial state. The domain publishes its in-memory changes only after this call succeeds.

A future adapter needs products, immutable order lines, orders, idempotency keys, receipts and audit records. Enforce a unique `(member, idempotency_key)` and unique nonempty receipt. Use integer cents. Do not overwrite committed order prices when catalogue prices change. Guard stock updates with a transaction and row locks or a version comparison.

The current `MemoryRepository` retains audit events only. Durable restart recovery requires adding a load/snapshot method for products, orders and keys, plus sequence restoration. A multi-process service needs database-level concurrency control; the C++ mutex protects only one process. Provide expiry processing from a trusted scheduler.

`Principal` is a trusted domain input, not an authentication token. Validate membership, household, unit, role, blocked status, email, verified age and PIN outside the library. Never accept those flags directly from a public request. `PaymentVerifier` must validate a real receipt against the complete order. It must not re-enter the checkout while verification holds its mutex.

No database implementation, PIN storage, identity provider or payment provider is included.
