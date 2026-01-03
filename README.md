# node-iracing-sdk workspace

This repository is an npm workspace that publishes two packages:

- `node-iracing-sdk` (runtime + prebuilt native binaries)
- `node-iracing-sdk-types` (TypeScript definitions)

For usage, API details, and build instructions, see `packages/runtime/README.md`.

## Workspace commands

```bash
npm install
npm run build      # builds native addon + JS in the runtime package
npm run prebuild   # builds precompiled binaries (run on Windows)
```

SDK sources for local native builds should be placed in `packages/runtime/irsdk_1_19/`
and are intentionally excluded from git/npm.
