# Releases, downloads, and updates

## Sources of truth

- `CMakeLists.txt` embeds the runtime version (`LAR_VERSION`); developer builds default to `<project-version>-dev`.
- Git tags use Semantic Versioning and begin with `v`, for example `v0.1.0-alpha` or `v0.1.0`.
- `.github/workflows/pipeline.yml` is the single canonical workflow for pull-request validation, `main` preview packages, website publication and tag-driven Releases. On `main`, Windows x64, Linux x64, macOS x64 and macOS arm64 build/test/package jobs run in parallel; the Pages job starts only after all four package smoke tests pass. Preview artifacts are retained for 14 days. It publishes public Releases only after a `v*` tag is pushed.
- Release packages include the runtime, dashboard assets, an example engine manifest, a launcher, and the project license. They do **not** include inference engines, CUDA, or model weights.
- `SHA256SUMS.txt` is attached to each release so users can detect accidental corruption and verify file integrity against the published checksum.
- `site/product.json` is the feature/status manifest used by the website. Keep it updated in the same change that adds/removes a user-visible feature. `site/app.js` fetches release assets and notes from the public GitHub Releases API.

## One repeatable maintainer publish command

Use the root-level [`Publish-LocalAIRuntime.ps1`](../Publish-LocalAIRuntime.ps1) as the one stable entry point instead of creating a new push script for each change. Download it once into the Downloads folder as `Publish-LocalAIRuntime.ps1`. The stable helper imports the exact commit from the prepared bundle, pushes `main` by fast-forward only, then delegates CI and release gating to `scripts/Publish-LocalAIRuntime.ps1` included in that bundle. For each prepared change, replace the fixed `local-ai-runtime-update.bundle` file in Downloads, then run the same command from PowerShell 5.1:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\Publish-LocalAIRuntime.ps1
```

The script uses the standard `local-ai-runtime-real-engine-push` working folder, fetches the official `main`, verifies and imports the prepared bundle at its original commit SHA, refuses stale/diverged bundles and dirty worktrees, and does a normal non-force push. It waits for the `main` workflow to pass all four platform/package gates and the Pages deployment before creating the next unused `vX.Y.Z-alpha` tag. It then waits for the tag workflow; GitHub publishes the Release and `SHA256SUMS.txt` only if those platform jobs also pass. The version is calculated from the existing version tags, so maintainers do not hand-edit a version for each alpha iteration. If a gate fails, the script stops before the next step and prints the Actions link. It never force-pushes, deletes a tag, or moves an existing release.

Each new prepared bundle should replace the previous file at the same fixed path; keep the helper script and command unchanged. Preview/site deployment happens after successful `main` CI; the public download links change when the tag-driven Release finishes. Do not describe a preview build as the public release.

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

## One-time migration from the first alpha

The `v0.1.0-alpha` package was published before the top-level platform launchers were added. Its updater intentionally rejects files it does not recognize, so that exact installed version cannot apply the first launcher-enabled archive in place. Users moving from `v0.1.0-alpha` to the next launcher-enabled alpha should download the new package from the project site, extract it into a new application folder, and launch it manually once. Configuration, engines, models and logs remain in the separate user-data folder. The updated Windows updater recognizes the launcher file for subsequent in-app upgrades. Do not remove the old application folder until the new one starts successfully.

## In-app update behavior in this alpha

On launch, the local dashboard calls `/version` and checks the public GitHub Releases API for a newer version appropriate to the current release channel. Offline operation continues if GitHub is unreachable. The exact platform asset must be present in the official release metadata with a SHA-256 digest and a bounded advertised size.

The stage action downloads only the matching asset from the exact official GitHub release URL, checks the advertised size and SHA-256 digest, and stores the verified archive under the per-user `updates/<tag>/` directory. The staged manifest is re-checked whenever status is inspected or an update is applied. Malformed tags, older releases, unsupported asset names, non-official URLs, missing digests, and oversized assets fail closed.

**Apply-and-restart is supported for tagged Windows portable releases and for installed Windows desktop releases that contain the desktop updater helper**, not from development/preview builds. Both paths require explicit user confirmation. The portable helper replaces only the approved runtime package files. The installed-desktop helper instead downloads the complete official `local-ai-runtime-windows-x64-setup.exe` asset and requires the exact GitHub release URL, expected asset name, bounded size and SHA-256 digest advertised by the official release API. It stages the installer under `%LOCALAPPDATA%\LocalAIRuntime\updates\<tag>`, re-hashes it immediately before execution, and does not request elevation.

For an installed desktop update, the shell starts a detached Windows PowerShell 5.1 helper and exits. The helper asks the local runtime to shut down gracefully, waits for the matching desktop process to exit, and creates a full per-file SHA-256 verified backup of the application directory outside the install root. It runs the official Inno Setup package against the existing per-user install path, verifies the runtime version and desktop payload, starts the updated shell, and requires `/health` to report the target runtime version from the expected installed executable plus `/v1/system/identity` to match the persistent per-user data-root ID. If setup, verification or health fails, the helper stops only the matching installed processes, restores the verified complete directory backup, verifies the previous shell/runtime, and restarts the previous version. The backup is retained for inspection/repair. If the helper is interrupted while Windows Setup is still running, recovery detects and waits for that staged installer before touching the install folder; the helper PID is refreshed before a recovery-launched shell starts, avoiding nested recovery loops. The portable updater remains disabled for desktop-shell installs so the shell and runtime cannot drift apart. Settings, engines, models, WebView2 profile and logs remain in `%LOCALAPPDATA%\LocalAIRuntime` and are not copied or replaced.

For the portable helper, if Windows, PowerShell, or the machine stops during file replacement or restart, run `scripts/Start-LocalAIRuntime.ps1` again. Before launching the runtime, the launcher reads the durable portable-update status records and processes only the newest record for that installation. For the installed desktop helper, the next launch of `LocalAIRuntime.Desktop.exe` checks the desktop-update status records before opening the window; if the newest record is interrupted, it starts the staged PowerShell recovery helper and exits so the helper can restore the application without locked files. Both flows wait rather than race a still-running helper, verify the rollback manifest before restoring, and fail closed with a backup/log location if restoration cannot be verified. Terminal records participate in newest-status ordering, so an older `rollback_failed` marker cannot restore stale files after a newer update has completed. Backups remain under the per-user updates folder for inspection. User configuration, downloaded engines and model weights live outside the application folder and are not copied or replaced. Windows CI exercises the portable helper's ValidateOnly path and the installed desktop helper under the inbox Windows PowerShell 5.1 runtime, not only PowerShell 7.

Linux and macOS can stage and verify their matching packages, but do not yet apply them in place. Development/preview builds also deliberately disable apply-and-restart: their source may be newer than the latest public tag despite the `-dev` version string, so allowing a preview to apply an older tagged archive could downgrade functionality. Users of the first `v0.1.0-alpha` release must manually install a release that includes the updater once; future tagged Windows releases can then use the in-app apply flow.

This remains user-confirmed update behavior, not unattended self-updating. SHA-256 fetched from the public release API detects corruption and mismatches but does not independently prove publisher identity if the repository/release account or transport trust is compromised. Do not call auto-install secure until signed provenance/attestation and key trust are implemented.

## Update stages

1. **Implemented:** embedded version, release API, matching per-platform asset selection, in-app staged download, size/SHA-256 verification, local progress/cancellation, persistence under the user-data directory, and staged-status revalidation.
2. **Implemented for tagged Windows portable releases:** user-confirmed apply-and-restart, archive path allowlist/size limits, SHA-256-verified per-file backup, post-update version and `/health` check, automatic rollback on failure, and next-launch recovery after helper interruption or rollback failure. The Windows package smoke test covers tamper rejection, successful update, user-data preservation, a regular rollback, forced helper termination, and retry after a rollback error.
3. **Shipped in `v0.1.11-alpha`:** native shell hand-off to the detached PowerShell 5.1 desktop helper, full setup-installer apply, full-install backup/rollback, startup recovery for interrupted setup, and visible in-window download progress. Main and tagged CI passed, and the user confirmed clean installation, launch, and first starter-model chat on one Windows environment. Separate Windows 10/11 acceptance, upgrade/uninstall coverage, and real GPU inference evidence remain open.
4. **Next:** add equivalent platform-native apply/recovery helpers and tests for Linux and macOS; acquire Authenticode signing and improve signed release provenance/attestation before considering broader automation.
5. **Later, opt-in only:** optional background download and one-click restart/install after signature trust, rollback, interrupted-update, and threat-model tests are in place.

Never auto-update from arbitrary branches or execute updater scripts included inside a model package.
