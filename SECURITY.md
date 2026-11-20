# Security policy

## Reporting a vulnerability

Do not publish exploit details in a public issue. Use GitHub's private vulnerability reporting feature if it is enabled for this repository. If private reporting is unavailable, contact the repository owner through a private channel and provide a concise description, impact, affected revision, and reproducible evidence.

## Scope

Security-sensitive areas include save-game parsing, content and asset import, archive extraction, mod or user-provided content handling, network-facing features, update delivery, and file-system path resolution.

## Engineering expectations

- Treat imported content and save files as untrusted input.
- Reject path traversal and unsafe archive entries.
- Bound memory, recursion, dimensions, and decompression work.
- Avoid executing content as code.
- Do not embed credentials, signing keys, or private endpoints in the repository.
- Keep dependencies pinned and review their security advisories.
- Do not log secrets or sensitive local paths unnecessarily.
