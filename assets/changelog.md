0.9.1 - WIP Release - add changes here

0.9.0
- **BREAKING:** Replaced `CreateRoot()` and `GetRoot()` with single `GetOrCreateRoot()` method
  - Returns existing root if ID already registered, otherwise creates new root
  - Idempotent: safe to call multiple times with the same ID
  - Fixes bug where local pointer nullification caused "already exists" errors on re-show
  - Migration: Replace `api->CreateRoot(config)` with `api->GetOrCreateRoot(config)`

0.9.0-alpha
- Initial alpha release