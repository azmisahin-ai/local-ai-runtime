# Releases, downloads, and updates

## Sources of truth

- `CMakeLists.txt` embeds the runtime version (`LAR_VERSION`); developer builds default to `<project-version>-dev`.
- Git tags use Semantic Versioning and begin with `v`, for example `v0.1.0-alpha` or `v0.1.0`.
- `.github/workflows/pipeline.yml` is the single canonical workflow for pull-request validation, version planning, platform builds, release publication and website deployment. On a `main` push, the workflow plans the next alpha version first; Windows x64, Linux x64, macOS x64 and macOS arm64 then build/test/package that version once. A final release job creates an immutable tag for the tested commit and publishes the exact artifacts from those jobs with `SHA256SUMS.txt`; the Pages deployment follows. No tag-triggered build is configured, so creating the tag cannot start a duplicate platform build. The run ends with one summary table for version planning, each platform, Release and Pages. Manual `workflow_dispatch` runs remain dry runs and do not publish tags or Releases.
- Release packages include the runtime, dashboard assets, an example engine manifest, a launcher, and the project license. They do **not** include inference engines, CUDA, or model weights.
- `SHA256SUMS.txt` is attached to each release so users can detect accidental corruption and verify file integrity against the published checksum.
- `site/product.json` is the feature/status manifest used by the website. Keep it updated in the same change that adds/removes a user-visible feature. `site/app.js` fetches release assets and notes from the public GitHub Releases API.

## One repeatable maintainer publish command

Use the repository-local [`scripts/Publish-LocalAIRuntime.ps1`](../scripts/Publish-LocalAIRuntime.ps1) as the single publisher. From a clean checkout on `main`, after committing the intended changes, run:

```powershell
.\scripts\Publish-LocalAIRuntime.ps1
```

The publisher checks the current branch and clean working tree, fetches the official `main` and version tags, and pushes the current commit only when it is a normal fast-forward of GitHub `main`. It never resets or force-pushes. If GitHub is ahead or history has diverged, it stops and asks the maintainer to reconcile the checkout.

After pushing, the publisher waits for the **single** workflow run to complete. That run calculates the next unused alpha tag before compiling, builds and tests the four platform targets once, creates the tag only after every platform gate passes, publishes the artifacts produced by those same jobs, and deploys the website. The local PowerShell script does not create tags or start a second CI run; after success it only verifies the tag and public Release for the commit it pushed. A failed platform build blocks both tag creation and Release publication. If the Pages deployment fails after the Release was published, the final summary identifies that separate failure; the already-published Release is not rebuilt.

For a version-specific dry run before publishing:

1. Use **Actions → Build, test, package & publish → Run workflow**, select `main`, and enter a version such as `0.1.25-alpha`. The manual run builds/tests/packages the four targets and uploads candidate artifacts, but it does not create a tag or publish a Release.
2. Inspect the completed run summary and download the four candidate packages if you need to examine them.
3. For a normal release, commit the reviewed changes to `main` and run `scripts/Publish-LocalAIRuntime.ps1`. The workflow selects the next alpha version from existing tags automatically; do not create or push a version tag manually.
4. Review the single run's platform, tag/release, checksum and website results before announcing the release.

The release job is idempotent for a rerun: if the planned tag already points to the same commit, it verifies rather than moves it; an existing release receives the artifacts again with `--clobber`. A tag that points to another commit is a hard failure. Never move or reuse a published release tag.

## Website deployment

Enable **Settings → Pages → Build and deployment → Source: GitHub Actions** once. The `publish-pages` job in `.github/workflows/pipeline.yml` deploys `site/` at the end of a successful `main` pipeline, after all four package smoke tests and the same-run Release publication pass (or on a successful manual dry run from `main`). This produces one workflow run with visible platform jobs and a final website deployment job. The release panel reads current GitHub Releases at page load, so downloadable assets do not need to be hardcoded into HTML.

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
