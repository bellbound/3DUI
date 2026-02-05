0.9.2
**Breaking changes**
- Replaced `CreateRoot()` with `GetOrCreateRoot()` method
  - Returns existing root if ID already registered, otherwise creates new root
  - Idempotent: safe to call multiple times with the same ID
  - Migration: Replace `api->CreateRoot(config)` with `api->GetOrCreateRoot(config)`
- Removed `GetBuildNumber()`. Use `GetInterfaceVersion()` instead

0.9.0-alpha
- Initial alpha release