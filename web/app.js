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
const languageSelect = document.querySelector('#language-select');
const settingsLanguageSelect = document.querySelector('#settings-language-select');
const overviewRuntimeState = document.querySelector('#overview-runtime-state');
const overviewRuntimeDetail = document.querySelector('#overview-runtime-detail');
const overviewModelState = document.querySelector('#overview-model-state');
const overviewModelDetail = document.querySelector('#overview-model-detail');
const overviewDeviceState = document.querySelector('#overview-device-state');
const overviewDeviceDetail = document.querySelector('#overview-device-detail');
const modelCountBadge = document.querySelector('#model-count');
const activityCountBadge = document.querySelector('#activity-count');
const activityActiveJobs = document.querySelector('#activity-active-jobs');
const activityReadyEngines = document.querySelector('#activity-ready-engines');
const activityTotalEngines = document.querySelector('#activity-total-engines');
const chatWelcome = document.querySelector('#chat-welcome');
const chatMessages = document.querySelector('#chat-messages');
const chatConversationList = document.querySelector('#chat-conversation-list');
const chatNewButton = document.querySelector('#chat-new');
const chatClearHistoryButton = document.querySelector('#chat-clear-history');
const chatStopButton = document.querySelector('#chat-stop');
const chatMessageInput = document.querySelector('#message');

const SUPPORTED_LOCALES = ['en', 'tr'];
const CHAT_HISTORY_KEY = 'lar.chat.history.v1';
const MAX_CHAT_CONVERSATIONS = 24;
const MAX_CHAT_MESSAGES = 80;
const MAX_CHAT_CONTEXT_MESSAGES = 24;
const VIEW_COPY_KEYS = {
  overview: ['page.overview.title', 'page.overview.description'],
  chat: ['page.chat.title', 'page.chat.description'],
  models: ['page.models.title', 'page.models.description'],
  activity: ['page.activity.title', 'page.activity.description'],
  settings: ['page.settings.title', 'page.settings.description'],
};
const LEGACY_VIEW_HASHES = { 'chat-panel': 'chat', 'catalog-panel': 'models', 'runtime-panel': 'activity', 'device-panel': 'overview' };
let currentView = 'overview';
let currentLocale = 'en';
let localeMessages = {};
let chatConversations = [];
let activeChatId = null;
let activeChatController = null;
let activeAssistantMessageElement = null;

function translation(key, fallback = key) {
  return Object.prototype.hasOwnProperty.call(localeMessages, key) ? localeMessages[key] : fallback;
}

function applyLocale() {
  document.documentElement.lang = currentLocale;
  for (const element of document.querySelectorAll('[data-i18n]')) {
    const key = element.dataset.i18n;
    if (key) element.textContent = translation(key, element.textContent);
  }
  for (const element of document.querySelectorAll('[data-i18n-placeholder]')) {
    const key = element.dataset.i18nPlaceholder;
    if (key) element.setAttribute('placeholder', translation(key, element.getAttribute('placeholder') || ''));
  }
  for (const element of document.querySelectorAll('[data-i18n-aria]')) {
    const key = element.dataset.i18nAria;
    if (key) element.setAttribute('aria-label', translation(key, element.getAttribute('aria-label') || ''));
  }
  for (const element of document.querySelectorAll('[data-i18n-title]')) {
    const key = element.dataset.i18nTitle;
    if (key) element.setAttribute('title', translation(key, element.getAttribute('title') || ''));
  }
  if (languageSelect) languageSelect.value = currentLocale;
  if (settingsLanguageSelect) settingsLanguageSelect.value = currentLocale;
  const pageCopy = VIEW_COPY_KEYS[currentView];
  if (pageCopy) {
    document.querySelector('#page-title').textContent = translation(pageCopy[0]);
    document.querySelector('#page-description').textContent = translation(pageCopy[1]);
  }
  for (const button of catalogFilterButtons) {
    const key = button.dataset.labelKey;
    if (key) button.dataset.label = translation(key, button.dataset.label || key);
  }
}

async function setLocale(locale) {
  const requested = SUPPORTED_LOCALES.includes(locale) ? locale : 'en';
  try {
    const response = await fetch(`/locales/${requested}.json`, { cache: 'no-store' });
    if (!response.ok) throw new Error(`Language resource returned ${response.status}`);
    const messages = await response.json();
    if (!messages || typeof messages !== 'object' || Array.isArray(messages)) throw new Error('Language resource is invalid.');
    localeMessages = messages;
    currentLocale = requested;
    try { localStorage.setItem('lar.locale', requested); } catch (_) { /* Preference persistence is optional. */ }
    applyLocale();
    renderCatalog();
    renderChatWorkspace();
    reportDiagnosticEvent('ui.locale.changed', `locale=${requested}`);
  } catch (error) {
    if (requested !== 'en') {
      reportDiagnosticEvent('ui.locale.load_failed', `locale=${requested}; error=${String(error.message || error).slice(0, 180)}`);
      await setLocale('en');
      return;
    }
    reportDiagnosticEvent('ui.locale.load_failed', `locale=en; error=${String(error.message || error).slice(0, 180)}`);
  }
}

function setActiveView(view, updateHash = true) {
  if (!Object.prototype.hasOwnProperty.call(VIEW_COPY_KEYS, view)) view = 'overview';
  currentView = view;
  for (const section of document.querySelectorAll('[data-view]')) {
    const active = section.dataset.view === view;
    section.classList.toggle('active', active);
    section.hidden = !active;
  }
  for (const button of document.querySelectorAll('[data-view-target]')) {
    if (!button.classList.contains('nav-item')) continue;
    const active = button.dataset.viewTarget === view;
    button.classList.toggle('active', active);
    if (active) button.setAttribute('aria-current', 'page');
    else button.removeAttribute('aria-current');
  }
  const pageCopy = VIEW_COPY_KEYS[view];
  document.querySelector('#page-title').textContent = translation(pageCopy[0]);
  document.querySelector('#page-description').textContent = translation(pageCopy[1]);
  if (updateHash && window.location.hash !== `#${view}`) history.pushState(null, '', `#${view}`);
  const reducedMotion = typeof window.matchMedia === 'function' && window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  if (typeof window.scrollTo === 'function') window.scrollTo({ top: 0, behavior: reducedMotion ? 'auto' : 'smooth' });
  if (view === 'activity') {
    void refreshEngines();
    void refreshDownloads();
    void refreshActivitySnapshot();
  } else if (view === 'models') {
    void refreshCatalog();
  }
}

for (const button of document.querySelectorAll('[data-view-target]')) {
  button.addEventListener('click', () => setActiveView(button.dataset.viewTarget));
}
function activateHashView() {
  const hash = String(window.location.hash || '').replace(/^#/, '').toLowerCase();
  setActiveView(LEGACY_VIEW_HASHES[hash] || hash, false);
  if (Object.prototype.hasOwnProperty.call(LEGACY_VIEW_HASHES, hash)) {
    const legacyTarget = document.getElementById(hash);
    if (legacyTarget && typeof legacyTarget.scrollIntoView === 'function') legacyTarget.scrollIntoView({ block: 'start' });
  }
}
window.addEventListener('popstate', activateHashView);
window.addEventListener('hashchange', activateHashView);
for (const button of document.querySelectorAll('[data-view-action]')) {
  button.addEventListener('click', () => {
    if (button.dataset.viewAction === 'check-updates') checkForUpdates(true);
  });
}
const initialLocale = (() => {
  try {
    const saved = localStorage.getItem('lar.locale');
    if (SUPPORTED_LOCALES.includes(saved)) return saved;
  } catch (_) { /* Use the browser language when storage is unavailable. */ }
  return String(navigator.language || 'en').toLowerCase().startsWith('tr') ? 'tr' : 'en';
})();
if (languageSelect) languageSelect.addEventListener('change', () => setLocale(languageSelect.value));
if (settingsLanguageSelect) settingsLanguageSelect.addEventListener('change', () => setLocale(settingsLanguageSelect.value));
activateHashView();
void setLocale(initialLocale);
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
    if (activeCatalogFilter === 'installed' && !item.installed) return false;
    if (activeCatalogFilter !== 'all' && activeCatalogFilter !== 'installed' && type !== activeCatalogFilter) return false;
    const searchable = [item.name, item.id, item.description, item.type, item.backend, item.license, item.version]
      .filter(Boolean).join(' ').toLocaleLowerCase();
    return !query || searchable.includes(query);
  });
  catalogList.replaceChildren();
  catalogSummary.textContent = `Showing ${filtered.length} of ${catalogItems.length} components`;
  for (const button of catalogFilterButtons) {
    const filter = button.dataset.catalogFilter;
    const count = filter === 'all' ? catalogItems.length : (filter === 'installed'
      ? catalogItems.filter((item) => item.installed).length
      : catalogItems.filter((item) => String(item.type || 'component').toLowerCase() === filter).length);
    button.textContent = `${button.dataset.label} (${count})`;
    const selected = filter === activeCatalogFilter;
    button.classList.toggle('active', selected);
    button.setAttribute('aria-pressed', String(selected));
  }
  if (!filtered.length) {
    catalogList.append(make('div', 'empty', catalogItems.length ? (currentLocale === 'tr' ? 'Bu filtreyle eşleşen bileşen yok. Başka bir tür dene veya aramayı temizle.' : 'No components match this filter. Try another type or clear the search.') : (currentLocale === 'tr' ? 'Bu bilgisayar için henüz uyumlu katalog bileşeni yok.' : 'No compatible catalog components are available for this computer yet.')));
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
    const installedModels = catalogItems.filter((item) => item.type === 'model' && item.installed).length;
    if (modelCountBadge) { modelCountBadge.textContent = String(installedModels); modelCountBadge.hidden = installedModels === 0; }
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
    const activeJobs = result.data.filter((job) => ['queued', 'downloading', 'verifying', 'extracting', 'configuring'].includes(job.state)).length;
    if (activityCountBadge) { activityCountBadge.textContent = String(activeJobs); activityCountBadge.hidden = activeJobs === 0; }
    if (activityActiveJobs) activityActiveJobs.textContent = String(activeJobs);
    downloadList.replaceChildren();
    if (!result.data.length) {
      downloadList.append(make('div', 'empty', translation('activity.noJobs', 'No downloads or setup jobs yet.')));
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

async function refreshActivitySnapshot() {
  try {
    const result = await api('/v1/activity');
    if (result.schema_version !== 1 || !Array.isArray(result.data) || !result.summary) {
      throw new Error('The activity snapshot response is invalid.');
    }
    if (activityActiveJobs) activityActiveJobs.textContent = String(Number(result.summary.active_jobs || 0));
    if (activityReadyEngines) activityReadyEngines.textContent = String(Number(result.summary.ready_engines || 0));
    if (activityTotalEngines) activityTotalEngines.textContent = String(Number(result.summary.engine_count || 0));
    if (activityCountBadge) {
      const active = Number(result.summary.active_jobs || 0);
      activityCountBadge.textContent = String(active);
      activityCountBadge.hidden = active === 0;
    }
  } catch (error) {
    if (activityActiveJobs) activityActiveJobs.textContent = '—';
    if (activityReadyEngines) activityReadyEngines.textContent = '—';
    if (activityTotalEngines) activityTotalEngines.textContent = '—';
    reportDiagnosticEvent('ui.activity.refresh_failed', String(error.message || error).slice(0, 260));
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
    action.href = '#models';
    action.textContent = item.id === 'nvidia-cuda-chat' ? 'Review engine options' : 'Browse starter components';
    card.append(action);
    grid.append(card);
  }
  section.append(grid);
  return section;
}

function renderHardware(info) {
  hardwareSummary.replaceChildren();
  const platformLabel = `${String(info.platform || 'Unknown').toUpperCase()} ${String(info.architecture || '').toUpperCase()}`.trim();
  if (overviewDeviceState) overviewDeviceState.textContent = platformLabel || (currentLocale === 'tr' ? 'Bilinmiyor' : 'Unknown');
  if (overviewDeviceDetail) overviewDeviceDetail.textContent = `${Number(info.logical_cpu_count || 0)} ${currentLocale === 'tr' ? 'mantıksal işlemci' : 'logical processors'} · ${formatBytes(info.total_memory_bytes)} RAM`;

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
      engineList.append(make('div', 'empty', currentLocale === 'tr' ? 'Henüz motor yapılandırılmadı. Modeller bölümünden bir motor kur veya gelişmiş ayarlardan elle yapılandır.' : 'No engines configured yet. Install an engine from Models or configure one manually in Settings.'));
      chatSendButton.disabled = true;
      chatReadiness.textContent = translation('chat.notReady', 'Install and start a chat model to begin.');
      if (overviewRuntimeState) overviewRuntimeState.textContent = currentLocale === 'tr' ? 'Kurulum gerekli' : 'Setup needed';
      if (overviewRuntimeDetail) overviewRuntimeDetail.textContent = currentLocale === 'tr' ? 'Sohbet motoru yapılandırılmadı' : 'No chat engine is configured';
      if (overviewModelState) overviewModelState.textContent = currentLocale === 'tr' ? 'Model yok' : 'No model yet';
      if (overviewModelDetail) overviewModelDetail.textContent = translation('overview.modelDetail');
      return;
    }
    for (const engine of result.data) engineList.append(engineCard(engine));
    const defaultChat = result.data.find((engine) => engine.default_for_chat);
    if (overviewRuntimeState) overviewRuntimeState.textContent = defaultChat?.ready
      ? (currentLocale === 'tr' ? 'Hazır' : 'Ready')
      : (defaultChat?.status === 'running' ? (currentLocale === 'tr' ? 'Yükleniyor' : 'Loading') : (currentLocale === 'tr' ? 'Durduruldu' : 'Stopped'));
    if (overviewRuntimeDetail) overviewRuntimeDetail.textContent = defaultChat?.ready
      ? (currentLocale === 'tr' ? 'Yerel model sunucusu hazır' : 'Local model server is ready')
      : (defaultChat?.status === 'running' ? (currentLocale === 'tr' ? 'Modelin yüklenmesi bekleniyor' : 'Waiting for the model to load') : (currentLocale === 'tr' ? 'Etkin bir sohbet motoru yok' : 'No active chat engine'));
    if (overviewModelState) overviewModelState.textContent = defaultChat?.model_configured
      ? (currentLocale === 'tr' ? 'Yapılandırıldı' : 'Configured')
      : (currentLocale === 'tr' ? 'Yapılandırılmadı' : 'Not configured');
    if (overviewModelDetail) overviewModelDetail.textContent = defaultChat?.display_name || translation('overview.modelDetail');
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

function createChatId() {
  if (window.crypto && typeof window.crypto.randomUUID === 'function') return window.crypto.randomUUID();
  return `chat-${Date.now()}-${Math.random().toString(16).slice(2, 10)}`;
}

function loadChatHistory() {
  try {
    const raw = localStorage.getItem(CHAT_HISTORY_KEY);
    if (!raw) return [];
    const parsed = JSON.parse(raw);
    if (!Array.isArray(parsed)) return [];
    return parsed.filter((conversation) => conversation && typeof conversation.id === 'string' && Array.isArray(conversation.messages))
      .slice(0, MAX_CHAT_CONVERSATIONS)
      .map((conversation) => ({
        id: conversation.id.slice(0, 100),
        title: typeof conversation.title === 'string' ? conversation.title.slice(0, 90) : '',
        updated_at: Number.isFinite(conversation.updated_at) ? conversation.updated_at : Date.now(),
        messages: conversation.messages.slice(-MAX_CHAT_MESSAGES).filter((message) =>
          message && ['user', 'assistant'].includes(message.role) && typeof message.content === 'string')
          .map((message) => ({ role: message.role, content: message.content.slice(0, 40000), state: ['stopped', 'generating'].includes(message.state) ? 'stopped' : 'complete' })),
      }))
      .sort((left, right) => right.updated_at - left.updated_at);
  } catch (error) {
    reportDiagnosticEvent('ui.chat.history_load_failed', String(error.message || error).slice(0, 180));
    return [];
  }
}

function saveChatHistory() {
  chatConversations = chatConversations
    .sort((left, right) => right.updated_at - left.updated_at)
    .slice(0, MAX_CHAT_CONVERSATIONS);
  for (const conversation of chatConversations) conversation.messages = conversation.messages.slice(-MAX_CHAT_MESSAGES);
  try {
    localStorage.setItem(CHAT_HISTORY_KEY, JSON.stringify(chatConversations));
    return true;
  } catch (error) {
    // A browser storage quota should never interrupt local inference. Retain the
    // newest conversations and make one bounded retry before reporting failure.
    chatConversations = chatConversations.slice(0, 8).map((conversation) => ({
      ...conversation,
      messages: conversation.messages.slice(-24).map((message) => ({ ...message, content: message.content.slice(-16000) })),
    }));
    try {
      localStorage.setItem(CHAT_HISTORY_KEY, JSON.stringify(chatConversations));
      return true;
    } catch (retryError) {
      reportDiagnosticEvent('ui.chat.history_save_failed', String(retryError.message || error.message || error).slice(0, 180));
      return false;
    }
  }
}

function activeConversation() {
  return chatConversations.find((conversation) => conversation.id === activeChatId) || null;
}

function conversationTitle(conversation) {
  if (conversation.title) return conversation.title;
  const firstUserMessage = conversation.messages.find((message) => message.role === 'user');
  if (!firstUserMessage) return translation('chat.untitled', 'New conversation');
  const compact = firstUserMessage.content.replace(/\s+/g, ' ').trim();
  return compact.length > 52 ? `${compact.slice(0, 51)}…` : compact;
}

function formatChatDate(timestamp) {
  const date = new Date(timestamp);
  if (!Number.isFinite(date.getTime())) return '';
  const today = new Date();
  if (date.toDateString() === today.toDateString()) {
    return date.toLocaleTimeString(currentLocale === 'tr' ? 'tr-TR' : 'en-US', { hour: '2-digit', minute: '2-digit' });
  }
  return date.toLocaleDateString(currentLocale === 'tr' ? 'tr-TR' : 'en-US', { month: 'short', day: 'numeric' });
}

function renderChatHistory() {
  if (!chatConversationList) return;
  chatConversationList.replaceChildren();
  if (chatConversations.length === 0) {
    chatConversationList.append(make('p', 'chat-history-empty', translation('chat.noHistory', 'No conversations yet.')));
    if (chatClearHistoryButton) chatClearHistoryButton.disabled = true;
    return;
  }
  if (chatClearHistoryButton) chatClearHistoryButton.disabled = Boolean(activeChatController);
  for (const conversation of chatConversations) {
    const button = make('button', `chat-history-item${conversation.id === activeChatId ? ' active' : ''}`);
    button.type = 'button';
    button.dataset.conversationId = conversation.id;
    button.disabled = Boolean(activeChatController);
    const title = make('span', 'chat-history-item-title', conversationTitle(conversation));
    const date = make('small', 'chat-history-item-date', formatChatDate(conversation.updated_at));
    button.append(title, date);
    chatConversationList.append(button);
  }
}

function renderChatMessages() {
  if (!chatMessages) return;
  chatMessages.replaceChildren();
  const conversation = activeConversation();
  const messages = conversation ? conversation.messages : [];
  if (chatWelcome) chatWelcome.hidden = messages.length > 0;
  chatMessages.hidden = messages.length === 0;
  activeAssistantMessageElement = null;
  for (const message of messages) {
    const article = make('article', `chat-message ${message.role}${message.state === 'generating' ? ' generating' : ''}`);
    const heading = make('div', 'chat-message-heading');
    heading.append(make('strong', '', message.role === 'user'
      ? translation('chat.you', 'You') : translation('chat.assistant', 'Local model')));
    if (message.role === 'assistant' && message.state === 'stopped') {
      heading.append(make('span', 'tag', translation('chat.stopped', 'Stopped')));
    } else if (message.role === 'assistant' && message.state === 'generating') {
      heading.append(make('span', 'tag loading', translation('chat.generating', 'Generating…')));
    }
    const content = make('div', 'chat-message-content', message.content || (message.state === 'generating' ? ' ' : ''));
    article.append(heading, content);
    chatMessages.append(article);
    if (message.role === 'assistant' && message.state === 'generating') activeAssistantMessageElement = content;
  }
  chatMessages.scrollTop = chatMessages.scrollHeight;
}

function renderChatWorkspace() {
  renderChatHistory();
  renderChatMessages();
}

function createNewConversation() {
  if (activeChatController) activeChatController.abort();
  const conversation = { id: createChatId(), title: '', updated_at: Date.now(), messages: [] };
  chatConversations.unshift(conversation);
  activeChatId = conversation.id;
  saveChatHistory();
  renderChatWorkspace();
  chatError.textContent = '';
  chatMessageInput.value = '';
  chatSendButton.disabled = true;
  chatMessageInput.focus();
}

function stopChatGeneration() {
  if (activeChatController) activeChatController.abort();
}

function appendAssistantDelta(conversation, message, text) {
  if (!text) return;
  message.content += text;
  conversation.updated_at = Date.now();
  if (conversation.id === activeChatId && activeAssistantMessageElement) {
    activeAssistantMessageElement.textContent = message.content;
    chatMessages.scrollTop = chatMessages.scrollHeight;
  }
}

function consumeSseBlock(block, onText) {
  const dataLines = block.split(/\r?\n/).filter((line) => line.startsWith('data:'));
  if (dataLines.length === 0) return false;
  const data = dataLines.map((line) => line.slice(5).replace(/^ /, '')).join('\n');
  if (data === '[DONE]') return true;
  let event;
  try { event = JSON.parse(data); }
  catch (_) { throw new Error(translation('chat.invalidStream', 'The model returned an invalid streaming response.')); }
  if (event?.error?.message) throw new Error(String(event.error.message));
  const choices = Array.isArray(event?.choices) ? event.choices : [];
  for (const choice of choices) {
    const delta = choice?.delta?.content;
    if (typeof delta === 'string') onText(delta);
  }
  return false;
}

async function readChatStream(response, onText) {
  if (!response.body || typeof response.body.getReader !== 'function') {
    throw new Error(translation('chat.streamingUnsupported', 'Streaming is not available in this desktop web runtime.'));
  }
  const reader = response.body.getReader();
  const decoder = new TextDecoder('utf-8');
  let pending = '';
  let receivedDone = false;
  let streamEnded = false;
  while (!streamEnded) {
    const part = await reader.read();
    pending += decoder.decode(part.value || new Uint8Array(), { stream: !part.done });
    while (true) {
      const separator = pending.match(/\r?\n\r?\n/);
      if (!separator || separator.index === undefined) break;
      const block = pending.slice(0, separator.index);
      pending = pending.slice(separator.index + separator[0].length);
      if (!receivedDone && consumeSseBlock(block, onText)) receivedDone = true;
    }
    if (part.done) streamEnded = true;
  }
  pending += decoder.decode();
  if (!receivedDone && pending.trim()) receivedDone = consumeSseBlock(pending, onText);
  if (!receivedDone) throw new Error(translation('chat.streamIncomplete', 'The model stream ended before completion.'));
}

async function parseChatError(response) {
  const raw = await response.text();
  try {
    const parsed = JSON.parse(raw);
    return parsed?.error?.message || raw || `Request failed (${response.status})`;
  } catch (_) {
    return raw || `Request failed (${response.status})`;
  }
}

chatConversations = loadChatHistory();
activeChatId = chatConversations[0]?.id || null;
renderChatWorkspace();

if (chatNewButton) chatNewButton.addEventListener('click', createNewConversation);
if (chatStopButton) chatStopButton.addEventListener('click', stopChatGeneration);
if (chatClearHistoryButton) chatClearHistoryButton.addEventListener('click', () => {
  if (activeChatController) return;
  if (!window.confirm(translation('chat.confirmClear', 'Delete all locally stored conversations?'))) return;
  chatConversations = [];
  activeChatId = null;
  saveChatHistory();
  renderChatWorkspace();
  chatError.textContent = '';
});
if (chatConversationList) chatConversationList.addEventListener('click', (event) => {
  const button = event.target.closest('[data-conversation-id]');
  if (!button || activeChatController) return;
  const selected = chatConversations.find((conversation) => conversation.id === button.dataset.conversationId);
  if (!selected) return;
  activeChatId = selected.id;
  chatError.textContent = '';
  renderChatWorkspace();
});

chatMessageInput.addEventListener('input', () => {
  chatSendButton.disabled = Boolean(activeChatController) || chatMessageInput.value.trim().length === 0;
});

document.querySelector('#chat-form').addEventListener('submit', async (event) => {
  event.preventDefault();
  if (activeChatController) return;
  chatError.textContent = '';
  const messageText = chatMessageInput.value.trim();
  if (!messageText) return;
  let conversation = activeConversation();
  if (!conversation) {
    conversation = { id: createChatId(), title: '', updated_at: Date.now(), messages: [] };
    chatConversations.unshift(conversation);
    activeChatId = conversation.id;
  }
  conversation.messages.push({ role: 'user', content: messageText, state: 'complete' });
  if (!conversation.title) conversation.title = messageText.replace(/\s+/g, ' ').slice(0, 52);
  conversation.updated_at = Date.now();
  chatMessageInput.value = '';
  chatSendButton.disabled = true;
  const assistantMessage = { role: 'assistant', content: '', state: 'generating' };
  conversation.messages.push(assistantMessage);
  if (conversation.messages.length > MAX_CHAT_MESSAGES) conversation.messages = conversation.messages.slice(-MAX_CHAT_MESSAGES);
  saveChatHistory();
  renderChatWorkspace();
  const controller = new AbortController();
  activeChatController = controller;
  chatStopButton.hidden = false;
  chatSendButton.disabled = true;
  chatMessageInput.disabled = true;
  renderChatHistory();
  reportDiagnosticEvent('ui.chat.request_started', `input_chars=${messageText.length}; stream=true`);
  const startedAt = performance.now();
  try {
    const conversationMessages = [
      {
        role: 'system',
        content: translation('chat.systemPrompt', 'You are a helpful assistant. Answer directly and concisely in the same language as the user.'),
      },
      ...conversation.messages.filter((item) => item.role === 'user' || item.role === 'assistant').slice(-MAX_CHAT_CONTEXT_MESSAGES).map((item) => ({ role: item.role, content: item.content })),
    ];
    // The current assistant placeholder is not part of the request yet.
    conversationMessages.pop();
    const response = await fetch('/v1/chat/completions', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      cache: 'no-store',
      signal: controller.signal,
      body: JSON.stringify({ model: 'local-model', messages: conversationMessages, temperature: 0.2, max_tokens: 512, stream: true }),
    });
    if (!response.ok) throw new Error(await parseChatError(response));
    const contentType = response.headers.get('content-type') || '';
    if (contentType.toLowerCase().includes('text/event-stream')) {
      await readChatStream(response, (delta) => appendAssistantDelta(conversation, assistantMessage, delta));
    } else {
      const result = await response.json();
      const content = result?.choices?.[0]?.message?.content;
      if (typeof content === 'string') appendAssistantDelta(conversation, assistantMessage, content);
      else throw new Error(translation('chat.emptyResponse', 'The model returned an empty response.'));
    }
    assistantMessage.state = 'complete';
    if (!assistantMessage.content) assistantMessage.content = translation('chat.emptyResponse', 'The model returned an empty response.');
    conversation.updated_at = Date.now();
    saveChatHistory();
    if (conversation.id === activeChatId) renderChatWorkspace();
    reportDiagnosticEvent('ui.chat.request_succeeded', `elapsed_ms=${Math.round(performance.now() - startedAt)}; input_chars=${messageText.length}; response_chars=${assistantMessage.content.length}; stream=true`);
  } catch (error) {
    if (error && error.name === 'AbortError') {
      assistantMessage.state = 'stopped';
      if (!assistantMessage.content) assistantMessage.content = translation('chat.stoppedBeforeResponse', 'Generation stopped before a response arrived.');
      reportDiagnosticEvent('ui.chat.request_stopped', `elapsed_ms=${Math.round(performance.now() - startedAt)}; input_chars=${messageText.length}`);
    } else {
      assistantMessage.state = assistantMessage.content ? 'stopped' : 'complete';
      if (!assistantMessage.content) conversation.messages = conversation.messages.filter((item) => item !== assistantMessage);
      chatError.textContent = error.message;
      reportDiagnosticEvent('ui.chat.request_failed', `elapsed_ms=${Math.round(performance.now() - startedAt)}; input_chars=${messageText.length}; error_type=${String(error?.name || 'Error').slice(0, 40)}`);
    }
    conversation.updated_at = Date.now();
    saveChatHistory();
    if (conversation.id === activeChatId) renderChatWorkspace();
  } finally {
    if (activeChatController === controller) activeChatController = null;
    chatStopButton.hidden = true;
    chatMessageInput.disabled = false;
    chatSendButton.disabled = chatMessageInput.value.trim().length === 0;
    renderChatHistory();
    chatMessageInput.focus();
  }
});

document.querySelectorAll('[data-prompt-key]').forEach((button) => {
  button.addEventListener('click', () => {
    chatMessageInput.value = translation(button.dataset.promptKey, '');
    chatMessageInput.dispatchEvent(new Event('input', { bubbles: true }));
    chatMessageInput.focus();
  });
});
chatMessageInput.addEventListener('keydown', (event) => {
  if (event.key === 'Enter' && !event.shiftKey && !event.isComposing) {
    event.preventDefault();
    if (!chatSendButton.disabled) document.querySelector('#chat-form').requestSubmit();
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
setInterval(() => { if (currentView === 'activity') refreshActivitySnapshot(); }, 5000);
