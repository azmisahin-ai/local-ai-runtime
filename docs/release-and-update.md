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

On launch, the local dashboard calls `/version` and checks the public GitHub Releases API for a newer version appropriate to the current release channel. Offline operation continues if GitHub is unreachable. The exact platform asset must be present in the official release metadata with a SHA-256 digest and a bounded advertised size.

The stage action downloads only the matching asset from the exact official GitHub release URL, checks the advertised size and SHA-256 digest, and stores the verified archive under the per-user `updates/<tag>/` directory. The staged manifest is re-checked whenever status is inspected or an update is applied. Malformed tags, older releases, unsupported asset names, non-official URLs, missing digests, and oversized assets fail closed.

**Apply-and-restart is currently supported only from a tagged, portable Windows release package**, not from development/preview builds. It requires explicit confirmation and no active download/install job. Before stopping the runtime, the helper checks that the portable app folder and its parent are writable; it does not silently request elevation, so protected folders must be moved to a user-writable location or updated manually. Before replacement, the helper re-hashes the package, checks the archive paths and required files, prepares a same-volume copy and backs up every incoming application file it will replace. The rollback manifest contains a SHA-256 for each backed-up file. The helper then asks the running runtime to shut down gracefully, records durable replacement/health-check states, installs the prepared files, verifies the executable version, restarts the runtime, and requires `/health` to report the target version. If replacement or startup verification fails after files were changed, the helper restores the verified backup and checks that the previous runtime becomes healthy.

If Windows, PowerShell, or the machine stops during file replacement or restart, run `scripts/Start-LocalAIRuntime.ps1` again. Before launching the executable, the launcher checks for an incomplete update belonging to that installation. It waits rather than racing a still-running update helper, finalizes an update if the target runtime is already healthy, or verifies the rollback manifest and restores the pre-update files. If recovery cannot verify or restore the backup, startup fails closed and the status/log identify the backup location for manual repair. Successful and failed recovery backups remain under the per-user updates folder for inspection. User configuration, downloaded engines and model weights live outside the application folder and are not copied or replaced.

Linux and macOS can stage and verify their matching packages, but do not yet apply them in place. Development/preview builds also deliberately disable apply-and-restart: their source may be newer than the latest public tag despite the `-dev` version string, so allowing a preview to apply an older tagged archive could downgrade functionality. Users of the first `v0.1.0-alpha` release must manually install a release that includes the updater once; future tagged Windows releases can then use the in-app apply flow.

This remains user-confirmed update behavior, not unattended self-updating. SHA-256 fetched from the public release API detects corruption and mismatches but does not independently prove publisher identity if the repository/release account or transport trust is compromised. Do not call auto-install secure until signed provenance/attestation and key trust are implemented.

## Update stages

1. **Implemented:** embedded version, release API, matching per-platform asset selection, in-app staged download, size/SHA-256 verification, local progress/cancellation, persistence under the user-data directory, and staged-status revalidation.
2. **Implemented for tagged Windows portable releases:** user-confirmed apply-and-restart, archive path allowlist/size limits, SHA-256-verified per-file backup, post-update version and `/health` check, automatic rollback on failure, and next-launch recovery after helper interruption or rollback failure. The Windows package smoke test covers tamper rejection, successful update, user-data preservation, a regular rollback, forced helper termination, and retry after a rollback error.
3. **Next:** add equivalent platform-native apply/recovery helpers and tests for Linux and macOS; improve signed release provenance/attestation before considering broader automation.
4. **Later, opt-in only:** optional background download and one-click restart/install after signature trust, rollback, interrupted-update, and threat-model tests are in place.

Never auto-update from arbitrary branches or execute updater scripts included inside a model package.
