const apiStatus = document.querySelector('#api-status');
const apiDot = document.querySelector('#api-dot');
const engineList = document.querySelector('#engine-list');
const engineError = document.querySelector('#engine-error');
const hardwareSummary = document.querySelector('#hardware-summary');
const hardwareError = document.querySelector('#hardware-error');
const hardwareRefreshButton = document.querySelector('#hardware-refresh');
const gpuObservationButton = document.querySelector('#gpu-observation-run');
const gpuObservationStatus = document.querySelector('#gpu-observation-status');
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
const checkUpdatesButton = document.querySelector('#check-updates');
const updateCheckStatus = document.querySelector('#update-check-status');
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
let updateCheckInProgress = false;
let desktopShellInstalled = false;
let lastUpdateCheckAt = 0;

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

async function checkForUpdates(manual = false) {
  if (updateCheckInProgress) return;
  updateCheckInProgress = true;
  lastUpdateCheckAt = Date.now();
  if (manual) {
    checkUpdatesButton.disabled = true;
    checkUpdatesButton.textContent = 'Checking…';
    updateCheckStatus.textContent = 'Checking the official release channel…';
  }
  try {
    const local = await api('/version');
    const current = local.version || '0.0.0';
    runtimeVersion.textContent = current;
    desktopShellInstalled = Boolean(local.desktop_shell_installed);
    updateApplySupported = Boolean(local.update_apply_supported) && !desktopShellInstalled;
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

    if (!latest || compareVersions(latest.tag_name, current) <= 0) {
      updateCheckStatus.textContent = `Up to date · ${current}`;
      availableRuntimeUpdate = null;
      updateDownloadButton.hidden = true;
      updateDownloadButton.disabled = true;
      updateApplyButton.hidden = true;
      updateNotice.hidden = !manual;
      if (manual) {
        updateTitle.textContent = 'You’re up to date';
        updateCopy.textContent = `This installation is running ${current}. No newer version is available in the selected release channel. Your local models and settings are unchanged.`;
        updateLink.href = REPOSITORY_RELEASES;
        updateLink.textContent = 'All releases';
        updateNotice.hidden = false;
        updateCheckStatus.textContent = `Up to date · ${current}`;
      }
      return;
    }

    updateTitle.textContent = `Version ${latest.tag_name} is available`;
    updateLink.href = safeHttpsUrl(latest.html_url) || REPOSITORY_RELEASES;
    updateLink.textContent = 'Release notes';
    availableRuntimeUpdate = null;
    updateDownloadButton.hidden = true;
    updateDownloadButton.disabled = true;
    updateApplyButton.hidden = true;

    if (desktopShellInstalled) {
      const installerName = String(local.desktop_installer_asset_name || 'local-ai-runtime-windows-x64-setup.exe');
      const installer = Array.isArray(latest.assets)
        ? latest.assets.find((candidate) => candidate.name === installerName)
        : null;
      updateLink.href = safeHttpsUrl(latest.html_url) || REPOSITORY_RELEASES;
      updateLink.textContent = installer ? 'Release notes' : 'Open official releases';
      updateDownloadButton.hidden = false;
      updateDownloadButton.disabled = !installer;
      updateDownloadButton.textContent = 'Download & install desktop update';
      updateCopy.textContent = installer
        ? `The complete desktop update (${latest.tag_name}) is available. The desktop shell will verify the official Windows setup installer against GitHub's SHA-256 digest, create a full application backup, update the shell and runtime together, and check local health before keeping the update. Settings, engines, models and logs remain in your user-data folder.`
        : `A newer release (${latest.tag_name}) is available, but its Windows desktop installer is not attached yet. The current installation will not be changed.`;
      updateNotice.hidden = false;
      updateCheckStatus.textContent = `Desktop app update available · ${latest.tag_name}`;
      return;
    }

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
      updateDownloadButton.textContent = 'Download & verify update';
      updateDownloadButton.hidden = false;
      updateDownloadButton.disabled = false;
      const applyGuidance = updateApplySupported
        ? 'After verification, choose Apply update & restart. The previous files are kept for rollback.'
        : /windows-x64\.zip$/i.test(asset.name)
          ? 'This installation cannot safely replace itself in place. Use the release download to extract the verified package into a new application folder, then start it there. Your settings, engines and models remain in the separate user-data folder.'
          : 'This platform currently verifies the official package in the app but does not yet install it in place. Close the app, extract the new archive into a new folder, and start it again; settings, engines and models remain in your user-data folder.';
      updateCopy.textContent = `You are using ${current}. Downloading stores the official ${latest.tag_name} package under your Local AI Runtime user-data folder and checks its advertised size and SHA-256 digest. ${applyGuidance}`;
      const stagedStatus = await getStagedUpdateStatus(latest.tag_name).catch(() => null);
      if (stagedStatus?.staged) showStagedUpdateStatus(stagedStatus, current);
    } else {
      updateCopy.textContent = `You are using ${current}. A newer release exists, but the official release metadata does not include a supported package and SHA-256 digest for this computer. Open the release notes to download manually; automatic staging is disabled.`;
    }
    updateNotice.hidden = false;
    updateCheckStatus.textContent = `Update available · ${latest.tag_name}`;
  } catch (error) {
    // Offline use is expected. Keep local inference usable when GitHub is unreachable.
    console.info('Update check skipped:', error.message);
    if (!manual) updateCheckStatus.textContent = 'Could not check for updates · local app remains available';
    if (manual) {
      updateTitle.textContent = 'Could not check for updates';
      updateCopy.textContent = 'Local AI Runtime is still available. Check your internet connection or open GitHub Releases to check the current version manually.';
      updateLink.href = REPOSITORY_RELEASES;
      updateLink.textContent = 'Open GitHub Releases';
      updateDownloadButton.hidden = true;
      updateApplyButton.hidden = true;
      updateNotice.hidden = false;
      updateCheckStatus.textContent = 'Update check failed · local app remains available';
    }
  } finally {
    updateCheckInProgress = false;
    if (manual) {
      checkUpdatesButton.disabled = false;
      checkUpdatesButton.textContent = 'Check for updates';
    }
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
  if (['applying', 'stopping', 'replacing', 'awaiting_health'].includes(state)) {
    updateTitle.textContent = `Applying update ${tagName}`;
    updateApplyButton.hidden = true;
    updateCopy.textContent = state === 'awaiting_health'
      ? 'Application files were replaced and the new runtime is being checked. If the update helper stops unexpectedly, the launcher will restore the verified backup before starting the app.'
      : 'The update helper is applying the verified package. The runtime will restart; keep this folder and the user-data folder in place. If the operation is interrupted, the launcher will attempt verified backup recovery.';
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
    updateCopy.textContent += /windows-x64\.zip$/i.test(status.asset_name || availableRuntimeUpdate?.asset_name || '')
      ? ' This installation cannot safely apply updates in place. No application files were changed; download the verified release and extract it into a new application folder. Your user-data folder is preserved.'
      : ' This platform verifies update packages in-app but does not yet install them in place. After verification, close the app and replace the application archive; settings, engines and models remain in your user-data folder.';
  } else if (!status.apply_supported) {
    updateCopy.textContent += ' Apply-and-restart is not supported by this installation. The verified package is kept staged and no files were changed.';
  }
}

if (window.chrome?.webview?.addEventListener) {
  window.chrome.webview.addEventListener('message', (event) => {
    if (event.data && typeof event.data === 'object' && event.data.type === 'desktop-update-progress') {
      updateNotice.hidden = false;
      updateTitle.textContent = String(event.data.title || 'Desktop update');
      updateCopy.textContent = String(event.data.detail || '');
      if (updateDownloadButton && desktopShellInstalled) {
        updateDownloadButton.disabled = true;
        updateDownloadButton.textContent = String(event.data.title || 'Updating…').slice(0, 64);
      }
      return;
    }
    if (event.data !== 'desktop-update-check-finished') return;
    if (desktopShellInstalled && updateDownloadButton) {
      updateDownloadButton.disabled = false;
      updateDownloadButton.textContent = 'Download & install desktop update';
    }
  });
}

async function stageRuntimeUpdate() {
  if (desktopShellInstalled) {
    const bridge = window.chrome?.webview;
    if (bridge && typeof bridge.postMessage === 'function') {
      updateDownloadButton.disabled = true;
      updateDownloadButton.textContent = 'Checking desktop update…';
      bridge.postMessage('check-for-desktop-updates');
    } else {
      updateCopy.textContent = 'The secure in-app installer is available in the Windows desktop shell. Use the Local AI Runtime notification-area icon and choose “Check for updates”.';
    }
    return;
  }
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
  // Keep the catalog scannable as more runtimes arrive. Technical details are
  // available on demand rather than taking over every component card.
  const compatibility = item.compatibility && typeof item.compatibility === 'object' ? item.compatibility : null;
  const resources = item.resources && typeof item.resources === 'object' ? item.resources : null;
  const provenance = item.provenance && typeof item.provenance === 'object' ? item.provenance : null;
  const evidence = item.evidence && typeof item.evidence === 'object' ? item.evidence : null;
  if (compatibility || resources || provenance || evidence) {
    const details = make('details', 'catalog-details');
    details.append(make('summary', '', 'Compatibility & requirements'));
    if (compatibility) {
      const targets = Array.isArray(compatibility.targets) ? compatibility.targets : [];
      if (targets.length) {
        const targetText = targets.map((target) => `${String(target.platform || 'unknown')} ${String(target.architecture || '')}`.trim()).join(' · ');
        details.append(make('p', 'hint', `Targets: ${targetText}`));
      }
      if (Array.isArray(compatibility.driver_requirements) && compatibility.driver_requirements.length) {
        details.append(make('p', 'hint', `Prerequisites: ${compatibility.driver_requirements.join('; ')}`));
      }
      if (Array.isArray(compatibility.known_limits) && compatibility.known_limits.length) {
        details.append(make('p', 'catalog-limit', `Known limits: ${compatibility.known_limits.join(' ')}`));
      }
    }
    if (resources) {
      const memoryParts = [];
      if (Number.isFinite(Number(resources.minimum_system_ram_bytes)) && resources.minimum_system_ram_bytes !== null) memoryParts.push(`minimum system RAM ≈ ${formatBytes(resources.minimum_system_ram_bytes)}`);
      if (Number.isFinite(Number(resources.recommended_system_ram_bytes)) && resources.recommended_system_ram_bytes !== null) memoryParts.push(`recommended ≈ ${formatBytes(resources.recommended_system_ram_bytes)}`);
      if (Number.isFinite(Number(resources.minimum_vram_bytes)) && resources.minimum_vram_bytes !== null) memoryParts.push(`minimum VRAM ≈ ${formatBytes(resources.minimum_vram_bytes)}`);
      if (Number.isFinite(Number(resources.recommended_vram_bytes)) && resources.recommended_vram_bytes !== null) memoryParts.push(`recommended VRAM ≈ ${formatBytes(resources.recommended_vram_bytes)}`);
      if (memoryParts.length) details.append(make('p', 'hint', `Memory guidance (${resources.estimate_confidence || 'unspecified'}): ${memoryParts.join('; ')}. ${resources.notes || ''}`));
      else if (resources.notes) details.append(make('p', 'hint', resources.notes));
    }
    if (provenance && Array.isArray(provenance.dependencies) && provenance.dependencies.length) {
      const dependencies = provenance.dependencies.map((dependency) => `${dependency.id} (${dependency.resolution})`);
      details.append(make('p', 'hint', `Included dependencies: ${dependencies.join('; ')}`));
    }
    if (evidence) {
      details.append(make('p', 'hint', `Evidence: package ${evidence.package_verification || 'unspecified'}; real task ${evidence.real_task_test || 'unspecified'}; hardware profile ${evidence.profile_validation || 'unspecified'}.`));
    }
    details.append(make('p', 'hint', 'Compatibility and resource estimates are metadata, not a guarantee that this component will work on every machine.'));
    info.append(details);
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

function formatBytes(value) {
  const bytes = Number(value);
  if (!Number.isFinite(bytes) || bytes <= 0) return 'Not available';
  const units = ['B', 'KB', 'MB', 'GB', 'TB'];
  let amount = bytes;
  let unit = 0;
  while (amount >= 1024 && unit < units.length - 1) { amount /= 1024; unit += 1; }
  return `${amount >= 10 || unit === 0 ? amount.toFixed(0) : amount.toFixed(1)} ${units[unit]}`;
}

function renderHardwareRecommendations(recommendations) {
  const section = make('section', 'hardware-recommendations');
  section.append(make('h3', '', 'Recommended starting points'));
  section.append(make('p', 'hint', 'These are local hardware-based suggestions, not compatibility guarantees. A package being installed or a GPU being detected does not prove that inference uses the GPU.'));
  const grid = make('div', 'recommendation-grid');
  const labels = {
    recommended: 'Good starting point',
    caution: 'Check memory headroom',
    'resource-constrained': 'Limited memory',
    'needs-space': 'Free disk space first',
    unknown: 'Needs manual review',
    candidate: 'GPU candidate · not verified',
    review: 'GPU needs review',
    'not-detected': 'NVIDIA adapter not detected',
  };
  for (const item of Array.isArray(recommendations) ? recommendations : []) {
    const card = make('article', `recommendation-card status-${String(item.status || 'unknown')}`);
    const heading = make('div', 'recommendation-heading');
    heading.append(make('h4', '', item.title || 'Starting point'));
    heading.append(make('span', 'recommendation-status', labels[item.status] || 'Needs review'));
    card.append(heading);
    if (item.model) card.append(make('p', 'recommendation-model', item.model));
    if (item.engine) card.append(make('p', 'recommendation-engine', item.engine));
    if (item.reason) card.append(make('p', 'recommendation-reason', item.reason));
    const action = document.createElement('a');
    action.className = 'button secondary';
    action.href = '#catalog-panel';
    action.textContent = item.id === 'nvidia-cuda-chat' ? 'Review engine options' : 'Browse starter components';
    card.append(action);
    grid.append(card);
  }
  section.append(grid);
  return section;
}

function renderHardware(info) {
  hardwareSummary.replaceChildren();
  const facts = make('div', 'hardware-facts');
  const addFact = (label, value) => {
    const fact = make('div', 'hardware-fact');
    fact.append(make('strong', '', value), make('span', '', label));
    facts.append(fact);
  };
  addFact('Platform', `${info.platform || 'Unknown'} ${info.architecture || ''}`.trim());
  addFact('Logical processors', Number.isFinite(Number(info.logical_cpu_count)) ? String(info.logical_cpu_count) : 'Not available');
  addFact('System RAM', formatBytes(info.total_memory_bytes));
  addFact('Available RAM', formatBytes(info.available_memory_bytes));
  addFact('Free space for models', formatBytes(info.available_disk_bytes));
  hardwareSummary.append(facts);

  const gpuHeading = make('p', 'eyebrow', 'GRAPHICS ADAPTERS');
  hardwareSummary.append(gpuHeading);
  if (Array.isArray(info.gpus) && info.gpus.length) {
    const gpuList = make('div', 'hardware-gpu-list');
    for (const gpu of info.gpus) {
      const item = make('article', 'hardware-gpu');
      item.append(make('h3', '', gpu.name || `${gpu.vendor || 'Unknown'} graphics adapter`));
      item.append(make('p', '', `${gpu.vendor || 'Unknown vendor'} · ${gpu.vendor_id || 'Vendor ID unavailable'} / ${gpu.device_id || 'Device ID unavailable'}`));
      item.append(make('p', '', `Dedicated video memory: ${formatBytes(gpu.dedicated_memory_bytes)}`));
      gpuList.append(item);
    }
    hardwareSummary.append(gpuList);
  } else {
    const probeMessage = info.gpu_probe_message || (info.gpu_probe_status === 'unsupported'
      ? 'GPU enumeration is not supported by this build.'
      : 'No graphics adapter was reported by the operating system.');
    hardwareSummary.append(make('p', 'hint', probeMessage));
  }

  const guidance = make('div', 'hardware-guidance');
  guidance.append(make('strong', '', `Starting profile · ${String(info.memory_profile || 'unknown').replaceAll('-', ' ')}`));
  guidance.append(make('p', '', info.recommendation || 'Start with the CPU baseline and verify each model on this computer.'));
  guidance.append(make('p', '', 'GPU inference is not automatically confirmed by detecting an adapter or installing a CUDA package. Check engine logs and GPU activity during a real request.'));
  hardwareSummary.append(guidance);
  hardwareSummary.append(renderHardwareRecommendations(info.recommendations));
}

async function refreshHardware() {
  hardwareError.textContent = '';
  hardwareRefreshButton.disabled = true;
  hardwareRefreshButton.textContent = 'Checking…';
  try {
    const info = await api('/v1/system/hardware');
    renderHardware(info);
  } catch (error) {
    hardwareError.textContent = error.message;
    hardwareSummary.replaceChildren(make('div', 'empty', 'Device information could not be read. The local model runtime remains available.'));
  } finally {
    hardwareRefreshButton.disabled = false;
    hardwareRefreshButton.textContent = 'Refresh device info';
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
  if (engine.backend_package && engine.backend_package !== 'manual-or-unknown-package') {
    const packageLabel = engine.backend_package === 'cuda12-package' ? 'CUDA 12 package'
      : engine.backend_package === 'cuda13-package' ? 'CUDA 13 package' : 'CPU package';
    meta.append(make('span', 'tag', packageLabel));
  }
  if (engine.backend_target === 'gpu-requested') meta.append(make('span', 'tag loading', 'GPU layers requested'));
  else if (engine.backend_target === 'cpu') meta.append(make('span', 'tag', 'CPU requested'));
  if (engine.gpu_execution_evidence === 'offload-reported') {
    const count = Number.isInteger(engine.gpu_offloaded_layers) && Number.isInteger(engine.gpu_total_layers)
      ? `${engine.gpu_offloaded_layers}/${engine.gpu_total_layers} layers reported` : 'offload reported';
    meta.append(make('span', 'tag ready', `GPU ${count}`));
  } else if (engine.gpu_execution_evidence === 'no-layers-offloaded') {
    meta.append(make('span', 'tag loading', 'No GPU layers reported'));
  } else if (engine.gpu_execution_evidence === 'cuda-runtime-reported') {
    meta.append(make('span', 'tag loading', 'CUDA initialized · offload unknown'));
  } else if (engine.backend_target === 'gpu-requested') {
    meta.append(make('span', 'tag loading', 'GPU execution not verified'));
  }
  info.append(meta);
  if (engine.backend_target === 'gpu-requested' || engine.backend_package === 'cuda12-package' || engine.backend_package === 'cuda13-package') {
    const evidenceSummary = engine.gpu_execution_evidence_summary || 'GPU execution has not been verified from the current engine log.';
    info.append(make('p', engine.gpu_execution_evidence === 'offload-reported' ? 'engine-loading' : 'hint', evidenceSummary));
    if (engine.gpu_execution_evidence === 'offload-reported') {
      info.append(make('p', 'hint', 'This confirms that the engine reported layer offload at startup. It does not measure live GPU utilization during inference.'));
    }
  }
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
    const gpuReady = Boolean(defaultChat?.ready && defaultChat.backend_target === 'gpu-requested');
    gpuObservationButton.disabled = !gpuReady;
    gpuObservationButton.title = gpuReady
      ? 'Runs a fixed local chat task and samples NVIDIA system utilization/memory while it runs.'
      : 'Configure a CUDA-enabled engine with GPU layers set to auto, then start it.';
    if (!defaultChat) chatReadiness.textContent = 'Configure llama.cpp above, then start the engine.';
    else if (defaultChat.ready) chatReadiness.textContent = 'Model server ready. Chat requests stay on this machine.';
    else if (defaultChat.status === 'running') chatReadiness.textContent = 'The model is loading or the server is not ready yet. Chat will enable when its health check succeeds.';
    else if (!defaultChat.executable_configured || !defaultChat.model_configured) chatReadiness.textContent = 'Complete the llama.cpp setup above, then start the engine.';
    else chatReadiness.textContent = 'Start the configured engine to enable chat.';
  } catch (error) {
    engineError.textContent = error.message;
    engineList.replaceChildren(make('div', 'empty', 'Could not load engine status.'));
    gpuObservationButton.disabled = true;
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

gpuObservationButton.addEventListener('click', async () => {
  gpuObservationStatus.className = 'hint';
  gpuObservationStatus.textContent = 'Running a fixed local inference request and sampling NVIDIA GPU utilization/memory. Other applications may also contribute to these system-wide measurements.';
  gpuObservationButton.disabled = true;
  const originalLabel = gpuObservationButton.textContent;
  gpuObservationButton.textContent = 'Measuring…';
  try {
    const result = await api('/v1/diagnostics/gpu-observation', { method: 'POST', body: '{}' });
    const lines = [
      result.task_succeeded
        ? `Local inference request completed in ${(Number(result.elapsed_ms || 0) / 1000).toFixed(2)} s.`
        : 'The local inference request did not complete successfully.',
      result.message || 'No additional measurement details were provided.',
      `Engine startup evidence: ${String(result.startup_evidence || 'not-verified')}. ${String(result.startup_evidence_summary || '')}`.trim(),
    ];
    for (const device of Array.isArray(result.devices) ? result.devices : []) {
      const utilization = Number.isInteger(device.peak_utilization_percent) ? `${device.peak_utilization_percent}%` : 'not available';
      const before = Number.isInteger(device.memory_before_mib) ? `${device.memory_before_mib} MiB` : 'not available';
      const peak = Number.isInteger(device.memory_peak_mib) ? `${device.memory_peak_mib} MiB` : 'not available';
      const after = Number.isInteger(device.memory_after_mib) ? `${device.memory_after_mib} MiB` : 'not available';
      const total = Number.isInteger(device.memory_total_mib) ? ` / ${device.memory_total_mib} MiB total` : '';
      lines.push(`GPU ${device.index}: peak utilization ${utilization}; memory before ${before}, peak ${peak}, after ${after}${total}.`);
    }
    gpuObservationStatus.className = !result.task_succeeded ? 'status-message failure' : (result.status === 'sampled' ? 'status-message success' : 'hint');
    gpuObservationStatus.textContent = lines.join('\n');
    reportDiagnosticEvent('ui.gpu_observation.completed', [
      `task_succeeded=${Boolean(result.task_succeeded)}`,
      `measurement=${String(result.status || 'unknown').slice(0, 32)}`,
      `samples=${Number(result.samples_during_request || 0)}`,
      `elapsed_ms=${Number(result.elapsed_ms || 0)}`,
      `device_count=${Array.isArray(result.devices) ? result.devices.length : 0}`,
      `startup_evidence=${String(result.startup_evidence || 'unknown').slice(0, 32)}`,
    ].join('; '));
  } catch (error) {
    gpuObservationStatus.className = 'status-message failure';
    gpuObservationStatus.textContent = error.message;
    reportDiagnosticEvent('ui.gpu_observation.failed', `http_status=${Number.isInteger(error?.httpStatus) ? error.httpStatus : 0}; error_type=${String(error?.name || 'Error').slice(0, 40)}`);
  } finally {
    gpuObservationButton.textContent = originalLabel;
    await refreshEngines();
  }
});

hardwareRefreshButton.addEventListener('click', refreshHardware);
updateDownloadButton.addEventListener('click', stageRuntimeUpdate);
checkUpdatesButton.addEventListener('click', () => checkForUpdates(true));
updateApplyButton.addEventListener('click', applyRuntimeUpdate);

chatSendButton.disabled = true;
refreshHealth();
refreshHardware();
refreshEngines();
refreshCatalog();
refreshDownloads();
checkForUpdates();
window.addEventListener('focus', () => { if (Date.now() - lastUpdateCheckAt > 5 * 60 * 1000) checkForUpdates(); });
setInterval(() => { if (Date.now() - lastUpdateCheckAt > 6 * 60 * 60 * 1000) checkForUpdates(); }, 60 * 1000);
setInterval(refreshHealth, 5000);
setInterval(refreshEngines, 5000);
setInterval(refreshDownloads, 2000);
