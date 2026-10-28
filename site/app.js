const REPO = 'azmisahin-ai/local-ai-runtime';
const RELEASES_API = `https://api.github.com/repos/${REPO}/releases?per_page=10`;
const FALLBACK_RELEASES = `https://github.com/${REPO}/releases`;
const featureGrid = document.querySelector('#feature-grid');
const downloadGrid = document.querySelector('#download-grid');
const latestVersion = document.querySelector('#latest-version');
const latestMeta = document.querySelector('#latest-meta');
const projectStatus = document.querySelector('#project-status');
const siteUpdated = document.querySelector('#site-updated');

function el(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
}

function externalLink(href, label, className = 'button button-primary') {
  const a = el('a', className, label);
  a.href = href;
  a.target = '_blank';
  a.rel = 'noopener noreferrer';
  return a;
}

async function loadProductData() {
  const response = await fetch('./product.json', { cache: 'no-store' });
  if (!response.ok) throw new Error(`Product manifest unavailable (${response.status})`);
  return response.json();
}

function renderFeatures(product) {
  featureGrid.replaceChildren();
  for (const [index, feature] of (product.features || []).entries()) {
    const card = el('article', 'feature-card');
    card.append(el('span', 'feature-index', `CAPABILITY / ${String(index + 1).padStart(2, '0')}`));
    card.append(el('h3', '', feature.name));
    card.append(el('p', '', feature.description));
    const status = String(feature.status || 'planned').toLowerCase();
    card.append(el('span', `feature-status ${status}`, status));
    featureGrid.append(card);
  }
  projectStatus.textContent = `${product.status || 'OPEN SOURCE'} · OPEN SOURCE`;
  siteUpdated.textContent = `Product manifest · ${product.currentVersion || 'development'}`;
}

function parseReleaseVersion(value) {
  const match = String(value || '').trim().replace(/^v/i, '')
    .match(/^(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z.-]+))?(?:\+[0-9A-Za-z.-]+)?$/);
  if (!match) return null;
  return {
    major: Number(match[1]),
    minor: Number(match[2]),
    patch: Number(match[3]),
    prerelease: match[4] ? match[4].split('.') : [],
  };
}

function compareReleaseVersions(leftText, rightText) {
  const left = parseReleaseVersion(leftText);
  const right = parseReleaseVersion(rightText);
  if (!left || !right) return 0;
  for (const field of ['major', 'minor', 'patch']) {
    if (left[field] !== right[field]) return left[field] > right[field] ? 1 : -1;
  }
  if (!left.prerelease.length && right.prerelease.length) return 1;
  if (left.prerelease.length && !right.prerelease.length) return -1;
  for (let i = 0; i < Math.min(left.prerelease.length, right.prerelease.length); i += 1) {
    const a = left.prerelease[i];
    const b = right.prerelease[i];
    if (a === b) continue;
    const aNumeric = /^\d+$/.test(a);
    const bNumeric = /^\d+$/.test(b);
    if (aNumeric && bNumeric) return Number(a) > Number(b) ? 1 : -1;
    if (aNumeric !== bNumeric) return aNumeric ? -1 : 1;
    return a > b ? 1 : -1;
  }
  if (left.prerelease.length === right.prerelease.length) return 0;
  return left.prerelease.length > right.prerelease.length ? 1 : -1;
}

function findAsset(release, preferredNames) {
  for (const name of preferredNames) {
    const asset = (release.assets || []).find((item) => item.name === name);
    if (asset) return asset;
  }
  return null;
}

function downloadOption(title, detail, symbol, asset, releaseUrl) {
  const row = el('div', 'download-option');
  row.append(el('span', 'platform-icon', symbol));
  const info = el('div');
  info.append(el('strong', '', title));
  info.append(el('small', '', detail));
  row.append(info);
  if (asset?.browser_download_url) {
    row.append(externalLink(asset.browser_download_url, 'Download ↓'));
  } else {
    row.append(externalLink(releaseUrl, 'Release files ↗', 'button button-ghost'));
  }
  return row;
}

function renderNoRelease(product) {
  latestVersion.textContent = 'No versioned release found';
  latestMeta.textContent = 'Published release metadata is unavailable. CI previews and source code remain available below.';
  downloadGrid.replaceChildren();
  downloadGrid.append(el('div', 'empty-state', 'No versioned binary release is visible right now. Check GitHub Releases or use a recent preview package after reviewing its workflow result.'));
  downloadGrid.append(externalLink(`https://github.com/${REPO}/actions/workflows/pipeline.yml`, 'Open preview packages ↗', 'button button-primary'));
  downloadGrid.append(externalLink(product.actionsPage || `https://github.com/${REPO}/actions`, 'View build and test runs ↗', 'button button-ghost'));
}

function renderRelease(release) {
  const url = release.html_url || FALLBACK_RELEASES;
  const status = release.prerelease ? 'PRE-RELEASE' : 'STABLE RELEASE';
  latestVersion.textContent = release.tag_name || release.name || 'Latest release';
  const published = release.published_at ? new Date(release.published_at).toLocaleDateString(undefined, { year: 'numeric', month: 'short', day: 'numeric' }) : 'Publication date unavailable';
  latestMeta.textContent = `${status} · Published ${published}${release.assets?.length ? ` · ${release.assets.length} downloadable assets` : ' · assets not attached'}`;
  downloadGrid.replaceChildren(
    downloadOption('Windows x64', 'Portable ZIP · external engine/model required', '⊞', findAsset(release, ['local-ai-runtime-windows-x64.zip']), url),
    downloadOption('Linux x64', 'Portable TAR.GZ · system libraries required', '⌁', findAsset(release, ['local-ai-runtime-linux-x64.tar.gz']), url),
    downloadOption('macOS Intel (x64)', 'Portable TAR.GZ · Intel Macs · not notarized', '⌘', findAsset(release, ['local-ai-runtime-macos-x64.tar.gz']), url),
    downloadOption('macOS Apple Silicon (arm64)', 'Portable TAR.GZ · M-series Macs · not notarized', '⌘', findAsset(release, ['local-ai-runtime-macos-arm64.tar.gz']), url)
  );
  const checksumAsset = findAsset(release, ['SHA256SUMS.txt']);
  const checksumNote = document.querySelector('.checksum-download');
  if (checksumNote) checksumNote.remove();
  if (checksumAsset?.browser_download_url) {
    const note = el('p', 'checksum-download');
    note.append(document.createTextNode('Checksums: '));
    note.append(externalLink(checksumAsset.browser_download_url, 'Download SHA256SUMS.txt ↗', 'text-link'));
    downloadGrid.after(note);
  }
  if (release.body) {
    const notes = el('details', 'release-notes');
    notes.append(el('summary', '', 'Read release summary'));
    const body = el('pre', 'release-body', release.body.slice(0, 8000));
    notes.append(body);
    downloadGrid.after(notes);
  }
}

async function loadReleases(product) {
  try {
    const response = await fetch(product.releasesApi || RELEASES_API, { headers: { Accept: 'application/vnd.github+json' }, cache: 'no-store' });
    if (!response.ok) throw new Error(`GitHub returned ${response.status}`);
    const releases = await response.json();
    if (!Array.isArray(releases)) throw new Error('Unexpected release response');
    const release = releases
      .filter((item) => !item.draft && parseReleaseVersion(item.tag_name))
      .sort((left, right) => compareReleaseVersions(right.tag_name, left.tag_name))[0];
    if (release) renderRelease(release); else renderNoRelease(product);
  } catch (error) {
    latestVersion.textContent = 'Release information is temporarily unavailable';
    latestMeta.textContent = 'The website remains available offline; open GitHub Releases to check manually.';
    downloadGrid.replaceChildren(externalLink(product.releasesPage || FALLBACK_RELEASES, 'Open GitHub Releases ↗'));
    console.warn('Could not load release information:', error.message);
  }
}

(async () => {
  try {
    const product = await loadProductData();
    renderFeatures(product);
    await loadReleases(product);
  } catch (error) {
    featureGrid.replaceChildren(el('div', 'empty-state', 'Feature metadata could not be loaded. See the source repository for the current status.'));
    console.error('Could not load product metadata:', error.message);
  }
})();
