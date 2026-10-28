# Releases, downloads, and updates

## Sources of truth

- `CMakeLists.txt` embeds the runtime version (`LAR_VERSION`); developer builds default to `<project-version>-dev`.
- Git tags use Semantic Versioning and begin with `v`, for example `v0.1.0-alpha` or `v0.1.0`.
- `.github/workflows/release.yml` builds/tests Windows x64 and Linux x64 and publishes portable packages only after a `v*` tag is pushed.
- Release packages include the runtime, dashboard assets, an example engine manifest, a launcher, and the project license. They do **not** include inference engines, CUDA, or model weights.
- `SHA256SUMS.txt` is attached to each release so users can detect accidental corruption and verify file integrity against the published checksum.
- `site/product.json` is the feature/status manifest used by the website. Keep it updated in the same change that adds/removes a user-visible feature. `site/app.js` fetches release assets and notes from the public GitHub Releases API.

## Rehearse a release before publishing

1. Merge tested changes to `main` and update `CHANGELOG.md` and `site/product.json`.
2. Make sure the default-branch CI and website deployment are green.
3. Open **Actions → Build and publish release → Run workflow**, select `main`, and use a version such as `0.1.0-alpha`. The manual run is a **dry run**: it builds/tests both packages, launches each extracted archive and checks `/health`, and uploads Windows/Linux packages as workflow artifacts. It does not publish a GitHub Release or create a tag.
4. Download and inspect both artifacts from the completed workflow. Confirm the embedded version, launcher, dashboard and config example are present and the package starts cleanly.
5. Only after the dry run succeeds, create and push an annotated tag:

   ```bash
   git tag -a v0.1.0-alpha -m "Local AI Runtime v0.1.0-alpha"
   git push origin v0.1.0-alpha
   ```

6. The tag-triggered run repeats the build and package smoke tests, then publishes the GitHub Release with `SHA256SUMS.txt`. A tag containing a hyphen is published as a prerelease; a tag without a hyphen is a stable release.
7. Review the resulting GitHub Release and CI run before announcing it.

Do not reuse or move an already published release tag. Cut a new version for every fix.

## Website deployment

Enable **Settings → Pages → Build and deployment → Source: GitHub Actions** once. `.github/workflows/pages.yml` deploys the `site/` directory on every push to `main`. The release panel reads current GitHub Releases at page load, so downloadable assets do not need to be hardcoded into HTML.

The expected URL is `https://azmisahin-ai.github.io/local-ai-runtime/` after Pages has been enabled and the first deployment succeeds.

## In-app update behavior in this alpha

On launch, the local dashboard calls the runtime's `/version` endpoint and checks the public GitHub Releases API for a newer stable version. If one exists, it shows a download/release-notes link. Offline operation continues if GitHub is unreachable.

This is deliberately **notify + user-confirmed download**, not silent self-installation. A safe unattended updater needs platform-specific staging, cryptographic signing/verification, a rollback path, and tests for interrupted downloads and a running executable being replaced. SHA-256 alone proves only that a file matches a published checksum; it does not prove who published the checksum. Do not call auto-install secure until signed releases/attestations and key trust are implemented.

## Update stages

1. **Current foundation:** semantic version embedded in runtime, release API, website asset discovery, dashboard update notification, checksums.
2. **Next:** download to a staging directory, verify signed provenance, verify hash, ask the user to install, preserve configuration/models, and support rollback.
3. **Later, opt-in only:** background download and restart/install orchestration per OS after signing, rollback, and threat-model tests are in place.

Never auto-update from arbitrary branches or execute updater scripts included inside a model package.
