# Releases, downloads, and updates

## Sources of truth

- `CMakeLists.txt` embeds the runtime version (`LAR_VERSION`); developer builds default to `<project-version>-dev`.
- Git tags use Semantic Versioning and begin with `v`, for example `v0.1.0-alpha` or `v0.1.0`.
- `.github/workflows/pipeline.yml` is the single canonical workflow for pull-request validation, `main` preview packages, website publication and tag-driven Releases. On `main`, Windows x64, Linux x64, macOS x64 and macOS arm64 build/test/package jobs run in parallel; the Pages job starts only after all four package smoke tests pass. Preview artifacts are retained for 14 days. It publishes public Releases only after a `v*` tag is pushed.
- Release packages include the runtime, dashboard assets, an example engine manifest, a launcher, and the project license. They do **not** include inference engines, CUDA, or model weights.
- `SHA256SUMS.txt` is attached to each release so users can detect accidental corruption and verify file integrity against the published checksum.
- `site/product.json` is the feature/status manifest used by the website. Keep it updated in the same change that adds/removes a user-visible feature. `site/app.js` fetches release assets and notes from the public GitHub Releases API.

## Preview builds and release rehearsal

The single `pipeline.yml` run orchestrates the whole lifecycle. Pull requests build and test on Windows, Linux, macOS Intel and macOS Apple Silicon without producing distribution packages. Each push to `main` runs one Windows, one Linux and two macOS architecture targets in parallel; every extracted package must pass `/health`. Only after all four target jobs succeed does the final website job publish `site/` to GitHub Pages. Preview artifacts are retained for 14 days. These are development previews, not public Releases and not recommended as stable updates. The weekly scheduled run invokes only the macOS compatibility matrix.

For a version-specific rehearsal before publishing:

1. Merge tested changes to `main` and update `CHANGELOG.md` and `site/product.json`.
2. Make sure the canonical pipeline is green. For changes submitted via pull request, require Windows, Linux, macOS x64 and macOS arm64 validation jobs to be green before merging.
3. Open **Actions → Build, test, package & publish → Run workflow**, select `main`, and use a version such as `0.1.0-alpha`. The manual run is a **dry run**: it builds/tests all four targets, launches each extracted archive and checks `/health`, and uploads the four platform/architecture packages as workflow artifacts. It does not publish a GitHub Release or create a tag.
4. Download and inspect the four platform/architecture artifacts from the completed workflow. Confirm the embedded version, launcher, dashboard and config example are present and each package starts cleanly.
5. Only after the dry run succeeds, create and push an annotated tag:

   ```bash
   git tag -a v0.1.0-alpha -m "Local AI Runtime v0.1.0-alpha"
   git push origin v0.1.0-alpha
   ```

6. The tag-triggered run repeats the build and package smoke tests, then publishes the GitHub Release with `SHA256SUMS.txt`. A tag containing a hyphen is published as a prerelease; a tag without a hyphen is a stable release.
7. Review the resulting GitHub Release and CI run before announcing it.

Do not reuse or move an already published release tag. Cut a new version for every fix.

## Website deployment

Enable **Settings → Pages → Build and deployment → Source: GitHub Actions** once. The `publish-pages` job in `.github/workflows/pipeline.yml` deploys `site/` at the end of a successful `main` pipeline, after all four package smoke tests pass (or on a successful manual run from `main`). This produces one workflow run with visible platform jobs and a final website deployment job. The release panel reads current GitHub Releases at page load, so downloadable assets do not need to be hardcoded into HTML.

The expected URL is `https://azmisahin-ai.github.io/local-ai-runtime/` after Pages has been enabled and the first deployment succeeds.

## In-app update behavior in this alpha

On launch, the local dashboard calls the runtime's `/version` endpoint and checks the public GitHub Releases API for a newer version appropriate to the current release channel. If one exists, it offers release notes and a **Download & verify package** action for the exact current OS/architecture. Offline operation continues if GitHub is unreachable.

The stage action downloads only the matching asset from the exact official GitHub release URL, requires a SHA-256 digest exposed by the GitHub Releases API, caps the package at 256 MiB, checks the advertised size and digest, and stores the verified archive under the per-user `updates/<tag>/` directory. A same-sized file or cached filename is never treated as verified without hashing it again. Malformed tags, older releases, unsupported asset names, non-official URLs, missing digests, and oversized assets fail closed. A staged package is not extracted, executed, or installed by this alpha. This avoids replacing a working runtime before the platform-specific apply/rollback path has passed tests.

This is **user-confirmed download + verification**, not self-installation. SHA-256 fetched from the public release API detects corruption and mismatches but does not independently prove publisher identity if the repository/release account or transport trust is compromised. Do not call auto-install secure until signed provenance/attestation and key trust are implemented.

## Update stages

1. **Implemented:** embedded version, release API, matching per-platform asset selection, in-app staged download, size/SHA-256 verification, local progress/cancellation, and persistence under the user-data directory. The running binary and user settings/models are not modified.
2. **Next:** verified package application through platform-specific helpers, with a same-volume backup, post-update `/health`/version check, rollback on failure, and explicit user confirmation. Do not start this step until the staged download is tested on Windows/Linux/macOS.
3. **Later, opt-in only:** optional background download and one-click restart/install after signed release provenance, rollback, interrupted-update, and threat-model tests are in place.

Never auto-update from arbitrary branches or execute updater scripts included inside a model package.
