const apiStatus = document.querySelector('#api-status');
const apiDot = document.querySelector('#api-dot');
const engineList = document.querySelector('#engine-list');
const engineError = document.querySelector('#engine-error');
const chatError = document.querySelector('#chat-error');
const answerWrap = document.querySelector('#answer-wrap');
const answer = document.querySelector('#answer');
const updateNotice = document.querySelector('#update-notice');
const updateTitle = document.querySelector('#update-title');
const updateCopy = document.querySelector('#update-copy');
const updateLink = document.querySelector('#update-link');
const updateDownloadButton = document.querySelector('#update-download');
const updateApplyButton = document.querySelector('#update-apply');
const runtimeVersion = document.querySelector('#runtime-version');
const chatReadiness = document.querySelector('#chat-readiness');
const chatSendButton = document.querySelector('#send');
const llamaSetupForm = document.querySelector('#llama-setup-form');
const setupStatus = document.querySelector('#setup-status');
const catalogList = document.querySelector('#catalog-list');
const catalogSearch = document.querySelector('#catalog-search');
const catalogSummary = document.querySelector('#catalog-summary');
const catalogFilterButtons = [...document.querySelectorAll('[data-catalog-filter]')];
const downloadList = document.querySelector('#download-list');
const catalogError = document.querySelector('#catalog-error');
const catalogRefreshButton = document.querySelector('#catalog-refresh');
const downloadsRefreshButton = document.querySelector('#downloads-refresh');
const diagnosticsExportButton = document.querySelector('#diagnostics-export');
const diagnosticsStatus = document.querySelector('#diagnostics-status');
const includeEngineLogsCheckbox = document.querySelector('#include-engine-logs');
const completedDownloadJobs = new Set();
let catalogItems = [];
let activeCatalogFilter = 'all';
let availableRuntimeUpdate = null;
let updateStageJobId = null;
let updateApplySupported = false;
let updateApplyInProgress = false;

const REPOSITORY_RELEASES = 'https://github.com/azmisahin-ai/local-ai-runtime/releases';
const FALLBACK_RELEASE_API = 'https://api.github.com/repos/azmisahin-ai/local-ai-runtime/releases?per_page=10';

function make(tag, className, text) {
  const el = document.createElement(tag);
  if (className) el.className = className;
  if (text !== undefined) el.textContent = text;
  return el;
}

async function api(path, options = {}) {
  const response = await fetch(path, {
    ...options,
    headers: { ...(options.body ? { 'Content-Type': 'application/json' } : {}), ...(options.headers || {}) },
  });
  const contentType = response.headers.get('content-type') || '';
  const data = contentType.includes('application/json') ? await response.json() : await response.text();
  if (!response.ok) {
    const message = typeof data === 'object' ? data?.error?.message || JSON.stringify(data) : data;
    if (!path.startsWith('/v1/chat/')) reportDiagnosticEvent('ui.request.failed', `${options.method || 'GET'} ${path} returned ${response.status}: ${String(message || '').slice(0, 900)}`);
    const error = new Error(message || `Request failed (${response.status})`);
    error.httpStatus = response.status;
    throw error;
  }
  return data;
}

function reportDiagnosticEvent(event, detail) {
  try {
    void fetch('/v1/diagnostics/events', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ event, detail: String(detail || '').slice(0, 1100) }),
      keepalive: true,
    }).catch(() => {});
  } catch (_) {
    // Diagnostics are best-effort and must not interrupt the dashboard.
  }
}

window.addEventListener('error', (event) => {
  const where = event.filename ? `${event.filename.split('/').pop()}:${event.lineno || 0}:${event.colno || 0}` : 'unknown source';
  reportDiagnosticEvent('ui.error', `${String(event.message || 'Uncaught browser error').slice(0, 700)} at ${where}`);
});
window.addEventListener('unhandledrejection', (event) => {
  const reason = event.reason;
  const name = reason && typeof reason === 'object' && reason.name ? String(reason.name) : 'UnhandledRejection';
  const message = reason && typeof reason === 'object' && reason.message ? String(reason.message) : String(reason || 'unknown reason');
  reportDiagnosticEvent('ui.unhandledrejection', `${name}: ${message.slice(0, 700)}`);
});

function parseVersion(value) {
  const match = String(value || '').trim().replace(/^v/i, '').match(/^(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z.-]+))?(?:\+[0-9A-Za-z.-]+)?$/);
  if (!match) return null;
  return { major: Number(match[1]), minor: Number(match[2]), patch: Number(match[3]), prerelease: match[4] || '' };
}

function compareVersions(leftText, rightText) {
  const left = parseVersion(leftText);
  const right = parseVersion(rightText);
  if (!left || !right) return 0;
  for (const key of ['major', 'minor', 'patch']) {
    if (left[key] !== right[key]) return left[key] > right[key] ? 1 : -1;
  }
  if (!left.prerelease && right.prerelease) return 1;
  if (left.prerelease && !right.prerelease) return -1;
  if (left.prerelease === right.prerelease) return 0;
  const channelRank = (value) => {
    const channel = String(value || '').split('.')[0].toLowerCase();
    if (channel === 'dev' || channel.startsWith('dev-')) return 0;
    if (channel === 'alpha' || channel.startsWith('alpha-')) return 1;
    if (channel === 'beta' || channel.startsWith('beta-')) return 2;
    if (channel === 'rc' || channel.startsWith('rc-')) return 3;
    return 0;
  };
  const leftRank = channelRank(left.prerelease);
  const rightRank = channelRank(right.prerelease);
  if (leftRank !== rightRank) return leftRank > rightRank ? 1 : -1;
  return left.prerelease.localeCompare(right.prerelease, undefined, { numeric: true });
}

async function checkForUpdates() {
  try {
    const local = await api('/version');
    const current = local.version || '0.0.0';
    runtimeVersion.textContent = current;
    updateApplySupported = Boolean(local.update_apply_supported);
    updateApplyButton.hidden = true;
    updateApplyButton.disabled = true;
    const releaseApi = local.releases_api_url || FALLBACK_RELEASE_API;
    const response = await fetch(releaseApi, { headers: { Accept: 'application/vnd.github+json' }, cache: 'no-store' });
    if (!response.ok) throw new Error(`Release service returned ${response.status}`);
    const releases = await response.json();
    if (!Array.isArray(releases)) throw new Error('Release response was not a list');
    const installedIsPrerelease = String(current).includes('-');
    const latest = releases
      .filter((release) => !release.draft && parseVersion(release.tag_name) &&
        (installedIsPrerelease || !release.prerelease))
      .sort((left, right) => compareVersions(right.tag_name, left.tag_name))[0];
    if (!latest || compareVersions(latest.tag_name, current) <= 0) return;

    updateTitle.textContent = `Version ${latest.tag_name} is available`;
    updateLink.href = safeHttpsUrl(latest.html_url) || REPOSITORY_RELEASES;
    updateLink.textContent = 'Release notes';
    availableRuntimeUpdate = null;
    updateDownloadButton.hidden = true;
    updateDownloadButton.disabled = true;

    const asset = Array.isArray(latest.assets)
      ? latest.assets.find((candidate) => candidate.name === local.update_asset_name)
      : null;
    const digest = String(asset?.digest || '').toLowerCase();
    const size = Number(asset?.size);
    const assetUrl = safeHttpsUrl(asset?.browser_download_url);
    const canStage = Boolean(asset && asset.name && assetUrl &&
      /^sha256:[a-f0-9]{64}$/.test(digest) && Number.isSafeInteger(size) && size >= 1024 && size <= 256 * 1024 * 1024 &&
      assetUrl === `https://github.com/azmisahin-ai/local-ai-runtime/releases/download/${latest.tag_name}/${asset.name}` &&
      asset.name === local.update_asset_name);
    if (canStage) {
      availableRuntimeUpdate = {
        tag_name: latest.tag_name,
        asset_name: asset.name,
        browser_download_url: assetUrl,
        digest,
        size,
      };
      updateDownloadButton.textContent = 'Download & verify package';
      updateDownloadButton.hidden = false;
      updateDownloadButton.disabled = false;
      updateCopy.textContent = `You are using ${current}. This update is for this computer's platform. Downloading stores the package under your Local AI Runtime user-data folder and verifies GitHub's SHA-256 digest. No application files change until you explicitly apply a verified update.`;
      const stagedStatus = await getStagedUpdateStatus(latest.tag_name).catch(() => null);
      if (stagedStatus?.staged) showStagedUpdateStatus(stagedStatus, current);
    } else {
      updateCopy.textContent = `You are using ${current}. A newer release exists, but GitHub did not provide a supported platform package with a SHA-256 digest. Open the release notes to inspect available downloads; automatic staging is disabled.`;
    }
    updateNotice.hidden = false;
  } catch (error) {
    // Offline use is expected. Keep local inference usable when GitHub is unreachable.
    console.info('Update check skipped:', error.message);
  }
}

async function getStagedUpdateStatus(tagName) {
  return api('/v1/updates/status', {
    method: 'POST',
    body: JSON.stringify({ tag_name: tagName }),
  });
}

function showStagedUpdateStatus(status, currentVersion) {
  updateApplyButton.hidden = true;
  updateApplyButton.disabled = true;
  if (!status || !status.staged) return;
  const tagName = status.tag_name || availableRuntimeUpdate?.tag_name || 'the selected release';
  updateCopy.textContent = `Verified package ${tagName} is staged (${formatBytes(status.size_bytes)}). Its size and SHA-256 were checked again on this device. Your running application, settings, engines and models have not been changed.`;
  const state = String(status.apply_state || '');
  if (state === 'applying') {
    updateTitle.textContent = `Applying update ${tagName}`;
    updateCopy.textContent = 'The update helper is applying the verified package. The runtime will restart; keep this folder and the user-data folder in place.';
    return;
  }
  if (state === 'rolled_back') {
    updateCopy.textContent += ` Previous update attempt was rolled back: ${String(status.apply_message || '').slice(0, 500)}`;
  } else if (state === 'rollback_failed') {
    updateTitle.textContent = `Manual recovery needed for ${tagName}`;
    updateCopy.textContent = `${String(status.apply_message || 'Automatic rollback failed.').slice(0, 700)} Check the local update log before attempting another update.`;
    return;
  } else if (state === 'failed') {
    updateCopy.textContent += ` Last apply attempt failed: ${String(status.apply_message || '').slice(0, 500)}`;
  }
  if (updateApplySupported && status.apply_supported && !updateApplyInProgress) {
    updateApplyButton.hidden = false;
    updateApplyButton.disabled = false;
    updateApplyButton.textContent = 'Apply update & restart';
  } else if (!updateApplySupported) {
    updateCopy.textContent += ' This development/preview build can stage updates but cannot apply a public release in place; this prevents replacing newer preview code with an older tagged build.';
  } else if (!status.apply_supported) {
    updateCopy.textContent += ' Apply-and-restart is not supported by this installation. The verified package is kept staged and no files were changed.';
  }
}

async function stageRuntimeUpdate() {
  if (!availableRuntimeUpdate || updateStageJobId) return;
  updateDownloadButton.disabled = true;
  updateDownloadButton.textContent = 'Starting download…';
  updateTitle.textContent = `Preparing ${availableRuntimeUpdate.tag_name}`;
  updateCopy.textContent = 'Requesting the official platform package. No running files will be changed.';
  reportDiagnosticEvent('ui.update.stage_started', `version=${availableRuntimeUpdate.tag_name}; asset=${availableRuntimeUpdate.asset_name}`);
  try {
    const started = await api('/v1/updates/stage', {
      method: 'POST',
      body: JSON.stringify(availableRuntimeUpdate),
    });
    updateStageJobId = started.job_id;
    while (updateStageJobId) {
      const job = await api(`/v1/downloads/${encodeURIComponent(updateStageJobId)}`);
      const progress = Math.max(0, Math.min(100, Number(job.progress_percent || 0)));
      const downloaded = Number(job.downloaded_bytes || 0);
      const total = Number(job.total_bytes || 0);
      updateCopy.textContent = total > 0
        ? `${job.phase || job.state} · ${formatBytes(downloaded)} of ${formatBytes(total)} (${progress}%). Your current installation and user data are unchanged.`
        : `${job.phase || job.state}. Your current installation and user data are unchanged.`;
      if (job.state === 'succeeded') {
        updateTitle.textContent = `Verified update ${job.version} is ready`;
        updateCopy.textContent = `The ${job.version} package was downloaded and its SHA-256 digest matched GitHub release metadata. It is staged in the Local AI Runtime user-data folder. Nothing has been changed.`;
        reportDiagnosticEvent('ui.update.stage_succeeded', `version=${job.version}; bytes=${downloaded}; sha256_verified=true; install=not_performed`);
        const stagedStatus = await getStagedUpdateStatus(job.version).catch(() => null);
        if (stagedStatus?.staged) showStagedUpdateStatus(stagedStatus, runtimeVersion.textContent);
        break;
      }
      if (job.state === 'failed' || job.state === 'canceled') {
        const message = job.error || job.phase || job.state;
        updateTitle.textContent = job.state === 'canceled' ? 'Update download canceled' : 'Update download failed';
        updateCopy.textContent = `${message} Your current installation and user data are unchanged.`;
        reportDiagnosticEvent(`ui.update.stage_${job.state}`, `version=${availableRuntimeUpdate.tag_name}; http_status=0`);
        break;
      }
      await new Promise((resolve) => setTimeout(resolve, 450));
    }
  } catch (error) {
    updateTitle.textContent = 'Update could not be staged';
    updateCopy.textContent = `${error.message} Your current installation and user data are unchanged.`;
    reportDiagnosticEvent('ui.update.stage_failed', `http_status=${Number.isInteger(error?.httpStatus) ? error.httpStatus : 0}; error_type=${String(error?.name || 'Error').slice(0, 40)}`);
  } finally {
    updateStageJobId = null;
    updateDownloadButton.disabled = !availableRuntimeUpdate;
    updateDownloadButton.textContent = 'Download & verify package';
    await refreshDownloads();
  }
}

async function applyRuntimeUpdate() {
  if (!availableRuntimeUpdate || !updateApplySupported || updateApplyInProgress) return;
  const tagName = availableRuntimeUpdate.tag_name;
  const accepted = window.confirm(
    `Apply ${tagName} and restart Local AI Runtime?\n\nThe current runtime and managed AI engines will stop briefly. User settings, downloaded engines and model files are stored separately and will not be replaced. A backup will be kept for rollback. Do not close this folder or shut down the computer during the update.`
  );
  if (!accepted) return;

  updateApplyInProgress = true;
  updateApplyButton.disabled = true;
  updateDownloadButton.disabled = true;
  updateTitle.textContent = `Applying ${tagName}`;
  updateCopy.textContent = 'The update helper is verifying the staged package once more, preparing a backup, and will restart the runtime. The dashboard may briefly disconnect.';
  reportDiagnosticEvent('ui.update.apply_requested', `version=${tagName}; confirmation=accepted`);
  const previousVersion = runtimeVersion.textContent;
  try {
    await api('/v1/updates/apply', {
      method: 'POST',
      body: JSON.stringify({ tag_name: tagName }),
    });
    updateCopy.textContent = 'Update accepted. Waiting for the runtime to stop and return. The page will reload when the apply result is available.';
    await waitForUpdateRestart(tagName, previousVersion);
  } catch (error) {
    updateTitle.textContent = `Could not apply ${tagName}`;
    updateCopy.textContent = `${error.message} The verified package remains staged; no update is reported as successful unless the restarted runtime passes its health check.`;
    reportDiagnosticEvent('ui.update.apply_failed', `version=${tagName}; http_status=${Number.isInteger(error?.httpStatus) ? error.httpStatus : 0}; error_type=${String(error?.name || 'Error').slice(0, 40)}`);
    updateApplyInProgress = false;
    updateApplyButton.disabled = false;
    updateDownloadButton.disabled = false;
  }
}

async function waitForUpdateRestart(tagName, previousVersion) {
  // Give the detached helper time to receive the current request and stop the
  // old process. Then reload only after the restarted/rolled-back runtime reports
  // a terminal apply state, rather than accidentally reloading the old process.
  await new Promise((resolve) => setTimeout(resolve, 1800));
  for (let attempt = 0; attempt < 120; attempt += 1) {
    try {
      const health = await api('/health');
      if (health?.status === 'ok') {
        const status = await getStagedUpdateStatus(tagName);
        const terminal = ['succeeded', 'failed', 'rolled_back', 'rollback_failed'].includes(String(status.apply_state || ''));
        if (terminal || (health.version && health.version !== previousVersion)) {
          reportDiagnosticEvent('ui.update.apply_terminal', `version=${tagName}; state=${String(status.apply_state || 'version_changed').slice(0, 40)}; runtime_version=${String(health.version || '').slice(0, 40)}`);
          window.location.reload();
          return;
        }
      }
    } catch (_) {
      // Runtime downtime is expected between graceful shutdown and restart.
    }
    await new Promise((resolve) => setTimeout(resolve, 750));
  }
  updateTitle.textContent = `Update status for ${tagName} is taking longer than expected`;
  updateCopy.textContent = 'The runtime has not returned to a verifiable state yet. Do not delete the rollback folder. Inspect %LOCALAPPDATA%/LocalAIRuntime/logs/update-apply.log and apply-status.json before retrying.';
  updateApplyInProgress = false;
}

async function refreshHealth() {
  try {
    const data = await api('/health');
    apiStatus.textContent = `API online · ${data.version}`;
    apiDot.className = 'dot good';
  } catch (error) {
    apiStatus.textContent = 'API unavailable';
    apiDot.className = 'dot bad';
  }
}

function formatBytes(bytes) {
  const value = Number(bytes || 0);
  if (!Number.isFinite(value) || value <= 0) return 'Size resolved and verified during download';
  const units = ['B', 'KB', 'MB', 'GB', 'TB'];
  let size = value;
  let unit = 0;
  while (size >= 1000 && unit < units.length - 1) { size /= 1000; unit += 1; }
  return `${size.toFixed(unit >= 2 ? 2 : 0)} ${units[unit]}`;
}

function safeHttpsUrl(value) {
  try {
    const parsed = new URL(String(value || ''));
    return parsed.protocol === 'https:' ? parsed.href : null;
  } catch (_) {
    return null;
  }
}

function catalogItemCard(item) {
  const card = make('article', 'catalog-card');
  const info = make('div');
  info.append(make('h3', '', item.name || item.id));
  info.append(make('p', '', item.description || 'Reviewed catalog component.'));
  const meta = make('div', 'engine-meta');
  meta.append(make('span', 'tag', item.type || 'component'));
  if (item.backend) meta.append(make('span', 'tag', String(item.backend).toUpperCase()));
  if (item.version) {
    const versionLabel = item.version === 'latest-compatible' ? 'latest compatible build' : (item.version === 'latest-stable' ? 'latest stable' : `revision ${String(item.version).slice(0, 12)}`);
    meta.append(make('span', 'tag', versionLabel));
  }
  meta.append(make('span', 'tag', item.installed ? 'installed' : formatBytes(item.size_bytes)));
  if (item.installed) meta.append(make('span', 'tag ready', 'verified file present'));
  info.append(meta);
  const licenseUrl = safeHttpsUrl(item.license_url);
  const license = make('p', 'catalog-license');
  license.append(document.createTextNode(`License: ${item.license || 'Not specified'} · `));
  if (licenseUrl) {
    const link = make('a', '', 'review license');
    link.href = licenseUrl;
    link.target = '_blank';
    link.rel = 'noopener noreferrer';
    license.append(link);
  } else {
    license.append(document.createTextNode('license URL unavailable'));
  }
  info.append(license);
  if (item.sha256) info.append(make('p', 'hint', `Pinned SHA-256: ${item.sha256}`));
  if (item.source_url) {
    const sourceUrl = safeHttpsUrl(item.source_url);
    if (sourceUrl) {
      const source = make('a', 'catalog-source', 'Open official model source');
      source.href = sourceUrl;
      source.target = '_blank';
      source.rel = 'noopener noreferrer';
      info.append(source);
    }
  }
  if (item.installed_path) info.append(make('p', 'hint', `Installed at: ${item.installed_path}`));

  const actions = make('div', 'catalog-actions');
  const acknowledgeLabel = make('label', 'license-ack');
  const acknowledge = make('input');
  acknowledge.type = 'checkbox';
  acknowledge.id = `license-ack-${String(item.id).replace(/[^a-zA-Z0-9_-]/g, '-')}`;
  acknowledge.checked = false;
  const acknowledgeCopy = make('span', 'license-ack-copy');
  acknowledgeCopy.append(make('strong', '', 'Required before install'), make('span', '', 'I have reviewed and accept this component’s license.'));
  acknowledgeLabel.htmlFor = acknowledge.id;
  acknowledgeLabel.append(acknowledge, acknowledgeCopy);
  const install = make('button', 'button primary', item.installed ? 'Installed' : 'Download & install');
  install.type = 'button';
  install.disabled = Boolean(item.installed) || !licenseUrl;
  acknowledge.addEventListener('change', () => {
    install.classList.toggle('license-confirmed', acknowledge.checked);
    if (acknowledge.checked) install.removeAttribute('aria-describedby');
    else install.setAttribute('aria-describedby', acknowledge.id);
  });
  install.addEventListener('click', async () => {
    if (!acknowledge.checked) {
      catalogError.textContent = 'Before downloading, tick the clearly marked “Required before install” license box on that component card.';
      acknowledge.focus();
      acknowledgeLabel.classList.add('needs-attention');
      return;
    }
    acknowledgeLabel.classList.remove('needs-attention');
    const sizeHint = Number(item.size_bytes || 0) > 0 ? ` Approximate download: ${formatBytes(item.size_bytes)}.` : ' The exact archive size will be resolved from official release metadata.';
    if (!window.confirm(`Download and install “${item.name || item.id}”?${sizeHint} Only files verified against the published SHA-256 will be activated.`)) return;
    install.disabled = true;
    catalogError.textContent = '';
    try {
      const started = await api(`/v1/downloads/${encodeURIComponent(item.id)}`, {
        method: 'POST', body: JSON.stringify({ accept_license: true }),
      });
      catalogError.textContent = `Queued ${item.name || item.id} as ${started.job_id}. Follow the installation status below.`;
      await refreshDownloads();
    } catch (error) {
      catalogError.textContent = error.message;
      install.disabled = Boolean(item.installed) || !licenseUrl;
    }
  });
  actions.append(acknowledgeLabel, install);
  if (!licenseUrl) actions.append(make('p', 'engine-error', 'Installation is disabled because no safe HTTPS license link is available.'));
  card.append(info, actions);
  return card;
}

function renderCatalog() {
  const query = catalogSearch.value.trim().toLocaleLowerCase();
  const filtered = catalogItems.filter((item) => {
    const type = String(item.type || 'component').toLowerCase();
    if (activeCatalogFilter !== 'all' && type !== activeCatalogFilter) return false;
    const searchable = [item.name, item.id, item.description, item.type, item.backend, item.license, item.version]
      .filter(Boolean).join(' ').toLocaleLowerCase();
    return !query || searchable.includes(query);
  });
  catalogList.replaceChildren();
  catalogSummary.textContent = `Showing ${filtered.length} of ${catalogItems.length} components`;
  for (const button of catalogFilterButtons) {
    const filter = button.dataset.catalogFilter;
    const count = filter === 'all' ? catalogItems.length : catalogItems.filter((item) => String(item.type || 'component').toLowerCase() === filter).length;
    button.textContent = `${button.dataset.label} (${count})`;
    const selected = filter === activeCatalogFilter;
    button.classList.toggle('active', selected);
    button.setAttribute('aria-pressed', String(selected));
  }
  if (!filtered.length) {
    catalogList.append(make('div', 'empty', catalogItems.length ? 'No components match this filter. Try another type or clear the search.' : 'No compatible catalog components are available for this computer yet.'));
    return;
  }
  for (const item of filtered) catalogList.append(catalogItemCard(item));
}

async function refreshCatalog() {
  catalogError.textContent = '';
  catalogRefreshButton.disabled = true;
  try {
    const result = await api('/v1/catalog');
    if (!Array.isArray(result.data)) throw new Error('The component catalog response is invalid.');
    catalogItems = result.data;
    renderCatalog();
  } catch (error) {
    catalogItems = [];
    renderCatalog();
    catalogList.replaceChildren(make('div', 'empty', 'Could not load the component catalog.'));
    catalogError.textContent = error.message;
  } finally {
    catalogRefreshButton.disabled = false;
  }
}

function downloadJobCard(job) {
  const card = make('article', 'download-card');
  const header = make('div', 'download-job-header');
  const jobTitle = job.item_id === 'runtime-update' ? 'Local AI Runtime update' : job.item_id;
  header.append(make('h3', '', `${jobTitle} · ${job.state}`));
  header.append(make('span', 'tag', job.kind || 'component'));
  card.append(header);
  card.append(make('p', '', job.phase || job.state));
  const progress = make('progress', 'download-progress');
  progress.max = 100;
  progress.value = Math.max(0, Math.min(100, Number(job.progress_percent || 0)));
  progress.setAttribute('aria-label', `Download progress ${progress.value}%`);
  card.append(progress);
  const total = Number(job.total_bytes || 0);
  const downloaded = Number(job.downloaded_bytes || 0);
  card.append(make('p', 'hint', total > 0 ? `${formatBytes(downloaded)} of ${formatBytes(total)} (${progress.value}%)` : `${formatBytes(downloaded)} downloaded`));
  if (job.version) card.append(make('p', 'hint', `Upstream version: ${job.version}`));
  if (job.installed_path) card.append(make('p', 'hint', job.kind === 'update' ? `Verified package staged at: ${job.installed_path}` : `Installed at: ${job.installed_path}`));
  if (job.error) {
    card.append(make('p', 'engine-error', job.error));
    if (String(job.error).includes('verified staging file was kept')) {
      card.append(make('p', 'recovery-hint', 'The model is already downloaded and verified. Close any app that may be using the model file, confirm the license again, then click Download & install to retry the finalization step without downloading 1.12 GB again.'));
    }
  }
  const active = ['queued', 'downloading', 'verifying', 'extracting', 'configuring'].includes(job.state);
  if (active) {
    const cancel = make('button', 'button secondary', 'Cancel');
    cancel.type = 'button';
    cancel.addEventListener('click', async () => {
      cancel.disabled = true;
      try {
        await api(`/v1/downloads/${encodeURIComponent(job.id)}`, { method: 'DELETE' });
        await refreshDownloads();
      } catch (error) {
        catalogError.textContent = error.message;
        cancel.disabled = false;
      }
    });
    card.append(cancel);
  }
  return card;
}

async function refreshDownloads() {
  try {
    const result = await api('/v1/downloads');
    if (!Array.isArray(result.data)) throw new Error('The download job list response is invalid.');
    downloadList.replaceChildren();
    if (!result.data.length) {
      downloadList.append(make('div', 'empty', 'No downloads started.'));
      return;
    }
    let hasActive = false;
    let changedComponentTerminalState = false;
    let completedComponentSuccessfully = false;
    for (const job of result.data) {
      downloadList.append(downloadJobCard(job));
      if (['queued', 'downloading', 'verifying', 'extracting', 'configuring'].includes(job.state)) hasActive = true;
      if (['succeeded', 'failed', 'canceled'].includes(job.state) && !completedDownloadJobs.has(job.id)) {
        completedDownloadJobs.add(job.id);
        if (job.kind !== 'update') {
          changedComponentTerminalState = true;
          completedComponentSuccessfully = completedComponentSuccessfully || job.state === 'succeeded';
        }
      }
    }
    // Runtime update staging never changes installed components or engine state.
    if (changedComponentTerminalState) {
      await refreshCatalog();
      if (completedComponentSuccessfully) await refreshEngines();
    }
    return hasActive;
  } catch (error) {
    catalogError.textContent = error.message;
    return false;
  }
}

function engineCard(engine) {
  const card = make('article', 'engine-card');
  const info = make('div');
  info.append(make('p', 'engine-name', engine.display_name || engine.id));
  const meta = make('div', 'engine-meta');
  const readinessClass = engine.ready ? 'ready' : (engine.status === 'running' ? 'loading' : '');
  const readinessLabel = engine.ready ? 'ready' : (engine.status === 'running' ? 'loading model' : engine.status);
  meta.append(make('span', `tag ${readinessClass}`, readinessLabel));
  if (engine.pid) meta.append(make('span', 'tag', `PID ${engine.pid}`));
  for (const capability of (engine.capabilities || [])) meta.append(make('span', 'tag', capability));
  info.append(meta);
  if (engine.last_error) info.append(make('p', 'engine-error', engine.last_error));
  else if (!engine.executable_configured) info.append(make('p', 'engine-error', 'Configure llama.cpp above or set the engine executable path.'));
  else if ((engine.capabilities || []).includes('chat') && !engine.model_configured) info.append(make('p', 'engine-error', 'Choose a GGUF model file in the setup form before starting.'));
  else if (engine.status === 'running' && !engine.ready) info.append(make('p', 'engine-loading', 'The process is running; waiting for the model server to report ready. Large models may take a while to load.'));
  const actions = make('div', 'engine-actions');
  const start = make('button', 'button primary', 'Start');
  start.type = 'button';
  start.disabled = engine.status === 'running' || !engine.executable_configured || ((engine.capabilities || []).includes('chat') && !engine.model_configured);
  start.addEventListener('click', () => controlEngine(engine.id, 'start', start));
  const stop = make('button', 'button secondary', 'Stop');
  stop.type = 'button';
  stop.disabled = engine.status !== 'running';
  stop.addEventListener('click', () => controlEngine(engine.id, 'stop', stop));
  actions.append(start, stop);
  card.append(info, actions);
  return card;
}

async function refreshEngines() {
  engineError.textContent = '';
  try {
    const result = await api('/v1/engines');
    engineList.replaceChildren();
    if (!result.data?.length) {
      engineList.append(make('div', 'empty', 'No engines configured. Use the setup form above to configure llama.cpp.'));
      chatSendButton.disabled = true;
      chatReadiness.textContent = 'Configure llama.cpp above, then start the engine.';
      return;
    }
    for (const engine of result.data) engineList.append(engineCard(engine));
    const defaultChat = result.data.find((engine) => engine.default_for_chat);
    chatSendButton.disabled = !defaultChat?.ready;
    if (!defaultChat) chatReadiness.textContent = 'Configure llama.cpp above, then start the engine.';
    else if (defaultChat.ready) chatReadiness.textContent = 'Model server ready. Chat requests stay on this machine.';
    else if (defaultChat.status === 'running') chatReadiness.textContent = 'The model is loading or the server is not ready yet. Chat will enable when its health check succeeds.';
    else if (!defaultChat.executable_configured || !defaultChat.model_configured) chatReadiness.textContent = 'Complete the llama.cpp setup above, then start the engine.';
    else chatReadiness.textContent = 'Start the configured engine to enable chat.';
  } catch (error) {
    engineError.textContent = error.message;
    engineList.replaceChildren(make('div', 'empty', 'Could not load engine status.'));
  }
}

async function waitForEngineReady(id, timeoutMs = 180000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const result = await api('/v1/engines');
    const engine = result.data?.find((entry) => entry.id === id);
    if (!engine) throw new Error(`Engine '${id}' is not present in the runtime configuration.`);
    if (engine.ready) return;
    if (engine.status === 'failed') throw new Error(engine.last_error || 'Engine exited before becoming ready. Export diagnostics or inspect the per-user runtime logs.');
    engineError.textContent = engine.status === 'running'
      ? 'Engine started. Waiting for the local model server to become ready…'
      : 'Waiting for engine startup…';
    await new Promise((resolve) => setTimeout(resolve, 1000));
  }
  throw new Error('Timed out waiting for the model server. Export diagnostics and check the engine executable/model path.');
}

async function controlEngine(id, action, button) {
  button.disabled = true;
  engineError.textContent = '';
  try {
    await api(`/v1/engines/${encodeURIComponent(id)}/${action}`, { method: 'POST', body: '{}' });
    if (action === 'start') await waitForEngineReady(id);
    await refreshEngines();
  } catch (error) {
    engineError.textContent = error.message;
    await refreshEngines();
  } finally {
    button.disabled = false;
  }
}

llamaSetupForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  setupStatus.className = 'status-message';
  setupStatus.textContent = 'Validating paths and saving configuration…';
  const saveButton = document.querySelector('#save-llama-setup');
  saveButton.disabled = true;
  try {
    const body = {
      executable_path: document.querySelector('#llama-executable').value.trim(),
      model_path: document.querySelector('#llama-model').value.trim(),
      backend: document.querySelector('#llama-backend').value,
      context_size: Number(document.querySelector('#llama-context').value),
      engine_port: Number(document.querySelector('#llama-port').value),
    };
    const result = await api('/v1/setup/llama-cpp', { method: 'POST', body: JSON.stringify(body) });
    setupStatus.className = 'status-message success';
    setupStatus.textContent = `Saved ${result.id} configuration (${result.backend.toUpperCase()} backend) to this user's local settings. Start the engine below; no model file was uploaded.`;
    await refreshEngines();
  } catch (error) {
    setupStatus.className = 'status-message failure';
    setupStatus.textContent = error.message;
  } finally {
    saveButton.disabled = false;
  }
});

document.querySelector('#refresh').addEventListener('click', async () => {
  await Promise.all([refreshHealth(), refreshEngines(), refreshCatalog(), refreshDownloads()]);
});
catalogRefreshButton.addEventListener('click', refreshCatalog);
catalogSearch.addEventListener('input', renderCatalog);
catalogFilterButtons.forEach((button) => button.addEventListener('click', () => {
  activeCatalogFilter = button.dataset.catalogFilter || 'all';
  renderCatalog();
}));
downloadsRefreshButton.addEventListener('click', async () => {
  downloadsRefreshButton.disabled = true;
  try {
    await refreshDownloads();
    await refreshCatalog();
  } finally {
    downloadsRefreshButton.disabled = false;
  }
});

diagnosticsExportButton.addEventListener('click', async () => {
  diagnosticsExportButton.disabled = true;
  diagnosticsStatus.className = 'status-message';
  diagnosticsStatus.textContent = 'Preparing a local diagnostic report…';
  try {
    const diagnosticsUrl = includeEngineLogsCheckbox.checked ? '/v1/diagnostics/export?include_engine_logs=1' : '/v1/diagnostics/export';
    const response = await fetch(diagnosticsUrl, { cache: 'no-store' });
    if (!response.ok) throw new Error(`Could not create diagnostic report (HTTP ${response.status}).`);
    const blob = await response.blob();
    const objectUrl = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = objectUrl;
    link.download = 'local-ai-runtime-diagnostics.txt';
    document.body.append(link);
    link.click();
    link.remove();
    URL.revokeObjectURL(objectUrl);
    diagnosticsStatus.className = 'status-message success';
    diagnosticsStatus.textContent = 'Diagnostic report created on this device. Review it before sharing; it was not uploaded anywhere.';
  } catch (error) {
    diagnosticsStatus.className = 'status-message failure';
    diagnosticsStatus.textContent = error.message;
    reportDiagnosticEvent('ui.diagnostics.export_failed', error.message);
  } finally {
    diagnosticsExportButton.disabled = false;
  }
});

document.querySelector('#chat-form').addEventListener('submit', async (event) => {
  event.preventDefault();
  chatError.textContent = '';
  answerWrap.hidden = true;
  const message = document.querySelector('#message').value.trim();
  if (!message) return;
  const send = document.querySelector('#send');
  const startedAt = performance.now();
  send.disabled = true;
  send.textContent = 'Waiting…';
  reportDiagnosticEvent('ui.chat.request_started', `input_chars=${message.length}; stream=false`);
  try {
    const result = await api('/v1/chat/completions', {
      method: 'POST',
      body: JSON.stringify({
        model: 'local-model',
        messages: [
          {
            role: 'system',
            content: 'You are a helpful assistant. Answer the user directly and concisely in the same language as the user. Do not claim you cannot answer ordinary greetings or requests. Do not provide code unless the user asks for code.',
          },
          { role: 'user', content: message },
        ],
        temperature: 0.2,
        max_tokens: 256,
        stream: false,
      }),
    });
    const choice = result?.choices?.[0];
    const content = choice?.message?.content;
    answer.textContent = typeof content === 'string' ? content : JSON.stringify(result, null, 2);
    answerWrap.hidden = false;
    const metrics = [
      `elapsed_ms=${Math.round(performance.now() - startedAt)}`,
      `input_chars=${message.length}`,
      `choice_count=${Array.isArray(result?.choices) ? result.choices.length : 0}`,
      `response_chars=${typeof content === 'string' ? content.length : 0}`,
      `finish_reason=${String(choice?.finish_reason || 'unknown').slice(0, 40)}`,
    ];
    const usage = result?.usage;
    for (const key of ['prompt_tokens', 'completion_tokens', 'total_tokens']) {
      if (Number.isFinite(usage?.[key])) metrics.push(`${key}=${usage[key]}`);
    }
    reportDiagnosticEvent('ui.chat.request_succeeded', metrics.join('; '));
  } catch (error) {
    chatError.textContent = error.message;
    reportDiagnosticEvent('ui.chat.request_failed', [
      `elapsed_ms=${Math.round(performance.now() - startedAt)}`,
      `input_chars=${message.length}`,
      `http_status=${Number.isInteger(error?.httpStatus) ? error.httpStatus : 0}`,
      `error_type=${String(error?.name || 'Error').slice(0, 40)}`,
    ].join('; '));
  } finally {
    send.disabled = false;
    send.textContent = 'Send message';
  }
});

updateDownloadButton.addEventListener('click', stageRuntimeUpdate);
updateApplyButton.addEventListener('click', applyRuntimeUpdate);

chatSendButton.disabled = true;
refreshHealth();
refreshEngines();
refreshCatalog();
refreshDownloads();
checkForUpdates();
setInterval(refreshHealth, 5000);
setInterval(refreshEngines, 5000);
setInterval(refreshDownloads, 2000);
