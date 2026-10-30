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
const resourcesSummary = document.querySelector('#resources-summary');
const resourcesError = document.querySelector('#resources-error');
const resourcesRefreshButton = document.querySelector('#resources-refresh');
const chatWelcome = document.querySelector('#chat-welcome');
const chatMessages = document.querySelector('#chat-messages');
const chatConversationList = document.querySelector('#chat-conversation-list');
const chatNewButton = document.querySelector('#chat-new');
const chatClearHistoryButton = document.querySelector('#chat-clear-history');
const chatStopButton = document.querySelector('#chat-stop');
const chatMessageInput = document.querySelector('#message');
const runtimeProfilePanel = document.querySelector('#runtime-profile-panel');
const runtimeProfileState = document.querySelector('#runtime-profile-state');
const runtimeProfileSummary = document.querySelector('#runtime-profile-summary');
const runtimeProfileError = document.querySelector('#runtime-profile-error');
const runtimeProfileEngineSelect = document.querySelector('#profile-engine');
const runtimeProfileModelSelect = document.querySelector('#profile-model');
const runtimeProfileContextSelect = document.querySelector('#profile-context');
const runtimeProfileApplyButton = document.querySelector('#runtime-profile-apply');
const firstRunPanel = document.querySelector('#first-run-panel');
const firstRunProgress = document.querySelector('#first-run-progress');
const firstRunProgressLabel = document.querySelector('#first-run-progress-label');
const firstRunStatus = document.querySelector('#first-run-status');
const firstRunDismissButton = document.querySelector('#first-run-dismiss');
const onboardingShowButton = document.querySelector('#onboarding-show');

const SUPPORTED_LOCALES = ['en', 'tr'];
const CHAT_HISTORY_KEY = 'lar.chat.history.v1';
const ONBOARDING_DISMISSED_KEY = 'lar.onboarding.dismissed.v1';
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
let hardwareInventoryLoaded = false;
let engineSnapshot = [];
let runtimeProfile = null;
let runtimeProfileDraft = { engine_component_id: '', model_component_id: '', context_size: 2048 };
let runtimeProfileDraftDirty = false;
let activeChatEngineReady = false;
let onboardingDismissed = (() => { try { return localStorage.getItem(ONBOARDING_DISMISSED_KEY) === '1'; } catch (_) { return false; } })();
if (firstRunPanel) firstRunPanel.hidden = onboardingDismissed;
let localeInitialized = false;

function translation(key, fallback = key) {
  return Object.prototype.hasOwnProperty.call(localeMessages, key) ? localeMessages[key] : fallback;
}

function formatMessage(key, values = {}, fallback = key) {
  return String(translation(key, fallback)).replace(/\{([a-zA-Z0-9_]+)\}/g, (match, name) =>
    Object.prototype.hasOwnProperty.call(values, name) ? String(values[name]) : match);
}

function formatError(error) {
  const detail = String(error && error.message ? error.message : error || translation('common.unknownError', 'Unknown error'));
  return formatMessage('common.errorDetails', { detail }, `Details: ${detail}`);
}

function updateChatSendAvailability() {
  if (!chatSendButton || !chatMessageInput) return;
  chatSendButton.disabled = Boolean(activeChatController) || !activeChatEngineReady ||
    chatMessageInput.disabled || chatMessageInput.value.trim().length === 0;
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
  updateFirstRunGuide();
}

function updateFirstRunGuide() {
  if (!firstRunPanel) return;
  firstRunPanel.hidden = onboardingDismissed;
  if (onboardingDismissed) return;

  const hasSelectedModel = Boolean(runtimeProfile?.engine_configured && runtimeProfile?.model_configured);
  const hasReadyChatEngine = engineSnapshot.some((engine) => engine && engine.default_for_chat && engine.ready && engine.model_configured);
  const hasCompletedChat = Array.isArray(chatConversations) && chatConversations.some((conversation) =>
    Array.isArray(conversation.messages) && conversation.messages.some((message) =>
      message.role === 'assistant' && message.state === 'complete' && String(message.content || '').trim().length > 0));
  const steps = [
    { id: 'device', complete: hardwareInventoryLoaded },
    { id: 'model', complete: hasSelectedModel },
    { id: 'engine', complete: hasReadyChatEngine },
    { id: 'chat', complete: hasCompletedChat },
  ];
  const completed = steps.filter((step) => step.complete).length;
  if (firstRunProgress) {
    firstRunProgress.value = completed;
    firstRunProgress.setAttribute('aria-valuenow', String(completed));
    firstRunProgress.setAttribute('aria-valuetext', formatMessage('onboarding.progress', { completed, total: steps.length }, `${completed} of ${steps.length} steps complete`));
  }
  if (firstRunProgressLabel) firstRunProgressLabel.textContent = formatMessage('onboarding.progress', { completed, total: steps.length }, `${completed} of ${steps.length} steps complete`);
  for (let index = 0; index < steps.length; index += 1) {
    const step = steps[index];
    const item = firstRunPanel.querySelector(`[data-first-run-step="${step.id}"]`);
    if (!item) continue;
    item.classList.toggle('is-complete', step.complete);
    item.classList.toggle('is-current', !step.complete && steps.slice(0, index).every((previous) => previous.complete));
    if (step.complete) item.removeAttribute('aria-current');
    else if (steps.slice(0, index).every((previous) => previous.complete)) item.setAttribute('aria-current', 'step');
    else item.removeAttribute('aria-current');
    const status = item.querySelector('[data-first-run-status]');
    if (status) status.textContent = translation(step.complete ? 'onboarding.stepComplete' : 'onboarding.stepPending', step.complete ? 'Complete' : 'Not completed');
    const action = item.querySelector('[data-first-run-action]');
    if (action) action.setAttribute('aria-label', translation(`onboarding.${step.id}ActionLabel`, action.textContent));
  }
  if (firstRunStatus) {
    firstRunStatus.className = completed === steps.length ? 'status-message success' : 'status-message';
    firstRunStatus.textContent = completed === steps.length
      ? translation('onboarding.allComplete', 'Your local chat setup is complete. You can hide this guide or reopen it from Settings.')
      : translation('onboarding.optional', 'This guide is optional. You can skip any step and return later.');
  }
}

function dismissFirstRunGuide() {
  onboardingDismissed = true;
  try { localStorage.setItem(ONBOARDING_DISMISSED_KEY, '1'); } catch (_) { /* The guide remains dismissible for this session. */ }
  updateFirstRunGuide();
  const pageTitle = document.querySelector('#page-title');
  if (pageTitle) pageTitle.focus();
}

function showFirstRunGuide() {
  onboardingDismissed = false;
  try { localStorage.removeItem(ONBOARDING_DISMISSED_KEY); } catch (_) { /* In-memory visibility is sufficient for this session. */ }
  setActiveView('overview');
  updateFirstRunGuide();
  if (firstRunPanel && typeof firstRunPanel.scrollIntoView === 'function') firstRunPanel.scrollIntoView({ block: 'start', behavior: 'auto' });
  if (document.querySelector('#first-run-title')) document.querySelector('#first-run-title').focus();
}

async function setLocale(locale) {
  const requested = SUPPORTED_LOCALES.includes(locale) ? locale : 'en';
  try {
    const response = await fetch(`/locales/${requested}.json`, { cache: 'no-store' });
    if (!response.ok) throw new Error(`Language resource returned ${response.status}`);
    const messages = await response.json();
    if (!messages || typeof messages !== 'object' || Array.isArray(messages)) throw new Error('Language resource is invalid.');
    localeMessages = messages;
    const changed = localeInitialized && currentLocale !== requested;
    currentLocale = requested;
    localeInitialized = true;
    try { localStorage.setItem('lar.locale', requested); } catch (_) { /* Preference persistence is optional. */ }
    applyLocale();
    renderCatalog();
    renderChatWorkspace();
    updateFirstRunGuide();
    if (changed) {
      void refreshEngines();
      void refreshHardware();
      void refreshResources();
      void refreshDownloads();
      void checkForUpdates();
    }
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
if (firstRunDismissButton) firstRunDismissButton.addEventListener('click', dismissFirstRunGuide);
if (onboardingShowButton) onboardingShowButton.addEventListener('click', showFirstRunGuide);
for (const button of document.querySelectorAll('[data-first-run-action]')) {
  button.addEventListener('click', () => {
    const action = button.dataset.firstRunAction;
    if (action === 'device') {
      setActiveView('overview');
      void refreshHardware();
      const target = document.querySelector('#device-panel');
      if (target && typeof target.scrollIntoView === 'function') target.scrollIntoView({ block: 'start' });
      const heading = document.querySelector('#device-title');
      if (heading) heading.focus();
    } else if (action === 'models') {
      setActiveView('models');
      document.querySelector('#page-title').focus();
    } else if (action === 'activity') {
      setActiveView('activity');
      document.querySelector('#page-title').focus();
    } else if (action === 'chat') {
      setActiveView('chat');
      if (chatMessageInput) chatMessageInput.focus();
    }
  });
}
activateHashView();
void setLocale(initialLocale);
const completedDownloadJobs = new Set();
let catalogItems = [];
let downloadJobsSnapshot = [];
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
    checkUpdatesButton.textContent = translation('updates.checking', 'Checking…');
    updateCheckStatus.textContent = translation('updates.checkingChannel', 'Checking the official release channel…');
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
      updateCheckStatus.textContent = formatMessage('updates.upToDate', { version: current }, `Up to date · ${current}`);
      availableRuntimeUpdate = null;
      updateDownloadButton.hidden = true;
      updateDownloadButton.disabled = true;
      updateApplyButton.hidden = true;
      updateNotice.hidden = !manual;
      if (manual) {
        updateTitle.textContent = translation('updates.upToDateTitle', 'You’re up to date');
        updateCopy.textContent = formatMessage('updates.upToDateCopy', { version: current });
        updateLink.href = REPOSITORY_RELEASES;
        updateLink.textContent = translation('updates.allReleases', 'All releases');
        updateNotice.hidden = false;
        updateCheckStatus.textContent = formatMessage('updates.upToDate', { version: current });
      }
      return;
    }

    updateTitle.textContent = formatMessage('updates.versionAvailable', { version: latest.tag_name }, `Version ${latest.tag_name} is available`);
    updateLink.href = safeHttpsUrl(latest.html_url) || REPOSITORY_RELEASES;
    updateLink.textContent = translation('updates.releaseNotes', 'Release notes');
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
      updateLink.textContent = installer ? translation('updates.releaseNotes', 'Release notes') : translation('updates.openOfficial', 'Open official releases');
      updateDownloadButton.hidden = false;
      updateDownloadButton.disabled = !installer;
      updateDownloadButton.textContent = translation('updates.desktopDownload', 'Download & install desktop update');
      updateCopy.textContent = installer
        ? formatMessage('updates.desktopCopy', { version: latest.tag_name })
        : formatMessage('updates.desktopMissing', { version: latest.tag_name });
      updateNotice.hidden = false;
      updateCheckStatus.textContent = formatMessage('updates.desktopAvailable', { version: latest.tag_name });
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
      updateDownloadButton.textContent = translation('updates.downloadVerify', 'Download & verify update');
      updateDownloadButton.hidden = false;
      updateDownloadButton.disabled = false;
      const applyGuidance = updateApplySupported
        ? translation('updates.applyDesktop', 'After verification, choose Apply update & restart. The previous files are kept for rollback.')
        : /windows-x64\.zip$/i.test(asset.name)
          ? translation('updates.applyWindowsArchive', 'This installation cannot safely replace itself in place.')
          : translation('updates.applyManualArchive', 'This platform verifies the official package but does not yet install it in place.');
      updateCopy.textContent = formatMessage('updates.downloadIntro', { current, version: latest.tag_name, guidance: applyGuidance });
      const stagedStatus = await getStagedUpdateStatus(latest.tag_name).catch(() => null);
      if (stagedStatus?.staged) showStagedUpdateStatus(stagedStatus, current);
    } else {
      updateCopy.textContent = formatMessage('updates.unsupportedPackage', { current });
    }
    updateNotice.hidden = false;
    updateCheckStatus.textContent = formatMessage('updates.available', { version: latest.tag_name });
  } catch (error) {
    // Offline use is expected. Keep local inference usable when GitHub is unreachable.
    console.info('Update check skipped:', error.message);
    if (!manual) updateCheckStatus.textContent = translation('updates.unavailableBackground', 'Could not check for updates · local app remains available');
    if (manual) {
      updateTitle.textContent = translation('updates.failedTitle', 'Could not check for updates');
      updateCopy.textContent = translation('updates.failedCopy', 'Local AI Runtime is still available. Check your internet connection or open GitHub Releases to check manually.');
      updateLink.href = REPOSITORY_RELEASES;
      updateLink.textContent = translation('updates.openGithub', 'Open GitHub Releases');
      updateDownloadButton.hidden = true;
      updateApplyButton.hidden = true;
      updateNotice.hidden = false;
      updateCheckStatus.textContent = translation('updates.checkFailed', 'Update check failed · local app remains available');
    }
  } finally {
    updateCheckInProgress = false;
    if (manual) {
      checkUpdatesButton.disabled = false;
      checkUpdatesButton.textContent = translation('common.checkUpdates', 'Check for updates');
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
  updateCopy.textContent = formatMessage('updates.staged', { version: tagName, size: formatBytes(status.size_bytes) });
  const state = String(status.apply_state || '');
  if (['applying', 'stopping', 'replacing', 'awaiting_health'].includes(state)) {
    updateTitle.textContent = formatMessage('updates.applying', { version: tagName });
    updateApplyButton.hidden = true;
    updateCopy.textContent = state === 'awaiting_health'
      ? translation('updates.awaitingHealth', 'Application files were replaced and the new runtime is being checked.')
      : translation('updates.applyingCopy', 'The update helper is applying the verified package. The runtime will restart.');
    return;
  }
  if (state === 'rolled_back') {
    updateCopy.textContent += ` ${formatMessage('updates.rolledBack', { detail: String(status.apply_message || '').slice(0, 500) })}`;
  } else if (state === 'rollback_failed') {
    updateTitle.textContent = formatMessage('updates.manualRecovery', { version: tagName });
    updateCopy.textContent = `${String(status.apply_message || translation('updates.rollbackFailed', 'Automatic rollback failed.')).slice(0, 700)} ${translation('updates.rollbackFailed', 'Check the local update log before attempting another update.')}`;
    return;
  } else if (state === 'failed') {
    updateCopy.textContent += ` ${formatMessage('updates.applyFailed', { detail: String(status.apply_message || '').slice(0, 500) })}`;
  }
  if (updateApplySupported && status.apply_supported && !updateApplyInProgress) {
    updateApplyButton.hidden = false;
    updateApplyButton.disabled = false;
    updateApplyButton.textContent = translation('updates.applyRestart', 'Apply update & restart');
  } else if (!updateApplySupported) {
    updateCopy.textContent += ` ${/windows-x64\.zip$/i.test(status.asset_name || availableRuntimeUpdate?.asset_name || '')
      ? translation('updates.applyWindowsArchive', 'This installation cannot safely replace itself in place.')
      : translation('updates.applyManualArchive', 'This platform verifies the official package but does not yet install it in place.')}`;
  } else if (!status.apply_supported) {
    updateCopy.textContent += ` ${translation('updates.applyUnsupported', 'Apply-and-restart is not supported by this installation.')}`;
  }
}

if (window.chrome?.webview?.addEventListener) {
  window.chrome.webview.addEventListener('message', (event) => {
    if (event.data && typeof event.data === 'object' && event.data.type === 'desktop-update-progress') {
      updateNotice.hidden = false;
      updateTitle.textContent = String(event.data.title || translation('updates.desktopTitle', 'Desktop update'));
      updateCopy.textContent = String(event.data.detail || '');
      if (updateDownloadButton && desktopShellInstalled) {
        updateDownloadButton.disabled = true;
        updateDownloadButton.textContent = String(event.data.title || translation('updates.updating', 'Updating…')).slice(0, 64);
      }
      return;
    }
    if (event.data !== 'desktop-update-check-finished') return;
    if (desktopShellInstalled && updateDownloadButton) {
      updateDownloadButton.disabled = false;
      updateDownloadButton.textContent = translation('updates.desktopDownload', 'Download & install desktop update');
    }
  });
}

async function stageRuntimeUpdate() {
  if (desktopShellInstalled) {
    const bridge = window.chrome?.webview;
    if (bridge && typeof bridge.postMessage === 'function') {
      updateDownloadButton.disabled = true;
      updateDownloadButton.textContent = translation('updates.checking', 'Checking desktop update…');
      bridge.postMessage('check-for-desktop-updates');
    } else {
      updateCopy.textContent = translation('updates.desktopShellHint', 'The secure in-app installer is available in the Windows desktop shell. Use the notification-area icon and choose “Check for updates”.');
    }
    return;
  }
  if (!availableRuntimeUpdate || updateStageJobId) return;
  updateDownloadButton.disabled = true;
  updateDownloadButton.textContent = translation('updates.startingDownload', 'Starting download…');
  updateTitle.textContent = formatMessage('updates.preparing', { version: availableRuntimeUpdate.tag_name });
  updateCopy.textContent = translation('updates.requestPackage', 'Requesting the official platform package. No running files will be changed.');
  reportDiagnosticEvent('ui.update.stage_started', `version=${availableRuntimeUpdate.tag_name}; asset=${availableRuntimeUpdate.asset_name}`);
  try {
    const started = await api('/v1/updates/stage', {
      method: 'POST',
      body: JSON.stringify(availableRuntimeUpdate),
    });
    updateStageJobId = started.job_id;
    while (updateStageJobId) {
      const job = await api(`/v1/jobs/${encodeURIComponent(updateStageJobId)}`);
      const progress = Math.max(0, Math.min(100, Number(job.progress_percent || 0)));
      const downloaded = Number(job.downloaded_bytes || 0);
      const total = Number(job.total_bytes || 0);
      updateCopy.textContent = total > 0
        ? formatMessage('updates.downloadProgress', { phase: translateJobPhase(job.phase || job.state), downloaded: formatBytes(downloaded), total: formatBytes(total), percent: progress })
        : formatMessage('updates.downloadProgressUnknown', { phase: translateJobPhase(job.phase || job.state) });
      if (job.state === 'succeeded') {
        updateTitle.textContent = formatMessage('updates.verifiedReady', { version: job.version });
        updateCopy.textContent = formatMessage('updates.verifiedReadyCopy', { version: job.version });
        reportDiagnosticEvent('ui.update.stage_succeeded', `version=${job.version}; bytes=${downloaded}; sha256_verified=true; install=not_performed`);
        const stagedStatus = await getStagedUpdateStatus(job.version).catch(() => null);
        if (stagedStatus?.staged) showStagedUpdateStatus(stagedStatus, runtimeVersion.textContent);
        break;
      }
      if (job.state === 'failed' || job.state === 'canceled') {
        const message = job.error || job.phase || job.state;
        updateTitle.textContent = translation(job.state === 'canceled' ? 'updates.downloadCanceled' : 'updates.downloadFailed', job.state === 'canceled' ? 'Update download canceled' : 'Update download failed');
        updateCopy.textContent = formatMessage('updates.downloadFailedCopy', { detail: message });
        reportDiagnosticEvent(`ui.update.stage_${job.state}`, `version=${availableRuntimeUpdate.tag_name}; http_status=0`);
        break;
      }
      await new Promise((resolve) => setTimeout(resolve, 450));
    }
  } catch (error) {
    updateTitle.textContent = translation('updates.couldNotStage', 'Update could not be staged');
    updateCopy.textContent = formatMessage('updates.downloadFailedCopy', { detail: error.message });
    reportDiagnosticEvent('ui.update.stage_failed', `http_status=${Number.isInteger(error?.httpStatus) ? error.httpStatus : 0}; error_type=${String(error?.name || 'Error').slice(0, 40)}`);
  } finally {
    updateStageJobId = null;
    updateDownloadButton.disabled = !availableRuntimeUpdate;
    updateDownloadButton.textContent = translation('updates.downloadVerify', 'Download & verify update');
    await refreshDownloads();
  }
}

async function applyRuntimeUpdate() {
  if (!availableRuntimeUpdate || !updateApplySupported || updateApplyInProgress) return;
  const tagName = availableRuntimeUpdate.tag_name;
  const accepted = window.confirm(formatMessage('updates.confirmApply', { version: tagName }));
  if (!accepted) return;

  updateApplyInProgress = true;
  updateApplyButton.disabled = true;
  updateDownloadButton.disabled = true;
  updateTitle.textContent = formatMessage('updates.applying', { version: tagName });
  updateCopy.textContent = translation('updates.applyStarting', 'The update helper is verifying the staged package once more, preparing a backup, and will restart the runtime.');
  reportDiagnosticEvent('ui.update.apply_requested', `version=${tagName}; confirmation=accepted`);
  const previousVersion = runtimeVersion.textContent;
  try {
    await api('/v1/updates/apply', {
      method: 'POST',
      body: JSON.stringify({ tag_name: tagName }),
    });
    updateCopy.textContent = translation('updates.applyAccepted', 'Update accepted. Waiting for the runtime to stop and return.');
    await waitForUpdateRestart(tagName, previousVersion);
  } catch (error) {
    updateTitle.textContent = formatMessage('updates.applyCouldNot', { version: tagName });
    updateCopy.textContent = formatMessage('updates.applyCouldNotCopy', { detail: error.message });
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
  updateTitle.textContent = formatMessage('updates.takingLong', { version: tagName });
  updateCopy.textContent = translation('updates.takingLongCopy', 'The runtime has not returned to a verifiable state yet. Do not delete the rollback folder. Inspect the local update log and status file before retrying.');
  updateApplyInProgress = false;
}

async function refreshHealth() {
  try {
    const data = await api('/health');
    apiStatus.textContent = formatMessage('api.online', { version: data.version }, `API online · ${data.version}`);
    apiDot.className = 'dot good';
  } catch (error) {
    apiStatus.textContent = translation('api.unavailable', 'API unavailable');
    apiDot.className = 'dot bad';
  }
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
  info.append(make('p', '', item.description || translation('models.reviewedComponent', 'Reviewed catalog component.')));
  const meta = make('div', 'engine-meta');
  meta.append(make('span', 'tag', item.type || 'component'));
  if (item.backend) meta.append(make('span', 'tag', String(item.backend).toUpperCase()));
  if (item.version) {
    const versionLabel = item.version === 'latest-compatible' ? translation('models.latestCompatible', 'latest compatible build') : (item.version === 'latest-stable' ? translation('models.latestStable', 'latest stable') : formatMessage('models.revision', { revision: String(item.version).slice(0, 12) }, `revision ${String(item.version).slice(0, 12)}`));
    meta.append(make('span', 'tag', versionLabel));
  }
  meta.append(make('span', 'tag', item.installed ? translation('models.installed', 'installed') : formatBytes(item.size_bytes)));
  if (item.installed) meta.append(make('span', 'tag ready', translation('models.verifiedFile', 'verified file present')));
  info.append(meta);
  const licenseUrl = safeHttpsUrl(item.license_url);
  const license = make('p', 'catalog-license');
  license.append(document.createTextNode(formatMessage('models.license', { license: item.license || translation('common.notSpecified', 'Not specified') })));
  if (licenseUrl) {
    const link = make('a', '', translation('models.reviewLicense', 'review license'));
    link.href = licenseUrl;
    link.target = '_blank';
    link.rel = 'noopener noreferrer';
    license.append(link);
  } else {
    license.append(document.createTextNode(translation('models.licenseUnavailable', 'license URL unavailable')));
  }
  info.append(license);
  if (item.sha256) info.append(make('p', 'hint', formatMessage('models.pinnedSha', { digest: item.sha256 })));
  if (item.source_url) {
    const sourceUrl = safeHttpsUrl(item.source_url);
    if (sourceUrl) {
      const source = make('a', 'catalog-source', translation('models.openOfficialSource', 'Open official model source'));
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
    details.append(make('summary', '', translation('models.compatibility', 'Compatibility & requirements')));
    if (compatibility) {
      const targets = Array.isArray(compatibility.targets) ? compatibility.targets : [];
      if (targets.length) {
        const targetText = targets.map((target) => `${String(target.platform || 'unknown')} ${String(target.architecture || '')}`.trim()).join(' · ');
        details.append(make('p', 'hint', formatMessage('models.targets', { targets: targetText })));
      }
      if (Array.isArray(compatibility.driver_requirements) && compatibility.driver_requirements.length) {
        details.append(make('p', 'hint', formatMessage('models.prerequisites', { requirements: compatibility.driver_requirements.join('; ') })));
      }
      if (Array.isArray(compatibility.known_limits) && compatibility.known_limits.length) {
        details.append(make('p', 'catalog-limit', formatMessage('models.knownLimits', { limits: compatibility.known_limits.join(' ') })));
      }
    }
    if (resources) {
      const memoryParts = [];
      if (Number.isFinite(Number(resources.minimum_system_ram_bytes)) && resources.minimum_system_ram_bytes !== null) memoryParts.push(formatMessage('models.memoryMinimumRam', { size: formatBytes(resources.minimum_system_ram_bytes) }));
      if (Number.isFinite(Number(resources.recommended_system_ram_bytes)) && resources.recommended_system_ram_bytes !== null) memoryParts.push(formatMessage('models.memoryRecommendedRam', { size: formatBytes(resources.recommended_system_ram_bytes) }));
      if (Number.isFinite(Number(resources.minimum_vram_bytes)) && resources.minimum_vram_bytes !== null) memoryParts.push(formatMessage('models.memoryMinimumVram', { size: formatBytes(resources.minimum_vram_bytes) }));
      if (Number.isFinite(Number(resources.recommended_vram_bytes)) && resources.recommended_vram_bytes !== null) memoryParts.push(formatMessage('models.memoryRecommendedVram', { size: formatBytes(resources.recommended_vram_bytes) }));
      if (memoryParts.length) details.append(make('p', 'hint', formatMessage('models.memoryGuidance', { confidence: resources.estimate_confidence || translation('common.unspecified', 'unspecified'), guidance: memoryParts.join('; '), notes: resources.notes || '' })));
      else if (resources.notes) details.append(make('p', 'hint', resources.notes));
    }
    if (provenance && Array.isArray(provenance.dependencies) && provenance.dependencies.length) {
      const dependencies = provenance.dependencies.map((dependency) => `${dependency.id} (${dependency.resolution})`);
      details.append(make('p', 'hint', formatMessage('models.dependencies', { dependencies: dependencies.join('; ') })));
    }
    if (evidence) {
      details.append(make('p', 'hint', formatMessage('models.evidence', { package: evidence.package_verification || translation('common.unspecified', 'unspecified'), task: evidence.real_task_test || translation('common.unspecified', 'unspecified'), profile: evidence.profile_validation || translation('common.unspecified', 'unspecified') })));
    }
    details.append(make('p', 'hint', translation('models.compatibilityDisclaimer', 'Compatibility and resource estimates are metadata, not a guarantee that this component will work on every machine.')));
    info.append(details);
  }
  if (item.installed_path) info.append(make('p', 'hint', formatMessage('models.installedAt', { path: item.installed_path })));

  const actions = make('div', 'catalog-actions');
  const acknowledgeLabel = make('label', 'license-ack');
  const acknowledge = make('input');
  acknowledge.type = 'checkbox';
  acknowledge.id = `license-ack-${String(item.id).replace(/[^a-zA-Z0-9_-]/g, '-')}`;
  acknowledge.checked = false;
  const acknowledgeCopy = make('span', 'license-ack-copy');
  acknowledgeCopy.append(make('strong', '', translation('models.requiredBeforeInstall', 'Required before install')), make('span', '', translation('models.acceptLicense', 'I have reviewed and accept this component’s license.')));
  acknowledgeLabel.htmlFor = acknowledge.id;
  acknowledgeLabel.append(acknowledge, acknowledgeCopy);
  const queuedJob = downloadJobsSnapshot.find((job) => job.item_id === item.id &&
    ['queued', 'running', 'downloading', 'verifying', 'extracting', 'configuring'].includes(job.state));
  const installLabel = item.installed ? translation('models.installed', 'Installed')
    : queuedJob ? (queuedJob.state === 'queued' ? translation('models.inQueue', 'In queue') : translation('models.installing', 'Installing…'))
      : translation('models.downloadInstall', 'Download & install');
  const install = make('button', 'button primary', installLabel);
  install.type = 'button';
  install.dataset.catalogInstallId = item.id;
  install.disabled = Boolean(item.installed) || Boolean(queuedJob) || !licenseUrl;
  acknowledge.addEventListener('change', () => {
    install.classList.toggle('license-confirmed', acknowledge.checked);
    if (acknowledge.checked) install.removeAttribute('aria-describedby');
    else install.setAttribute('aria-describedby', acknowledge.id);
  });
  install.addEventListener('click', async () => {
    if (!acknowledge.checked) {
      catalogError.textContent = translation('models.licenseCheck', 'Before downloading, tick the clearly marked license box on that component card.');
      acknowledge.focus();
      acknowledgeLabel.classList.add('needs-attention');
      return;
    }
    acknowledgeLabel.classList.remove('needs-attention');
    const sizeHint = Number(item.size_bytes || 0) > 0 ? formatMessage('models.approxDownload', { size: formatBytes(item.size_bytes) }) : translation('models.resolveSize', 'The exact archive size will be resolved from official release metadata.');
    if (!window.confirm(formatMessage('models.confirmDownload', { name: item.name || item.id, sizeHint }))) return;
    install.disabled = true;
    catalogError.textContent = '';
    try {
      const started = await api('/v1/jobs', {
        method: 'POST', body: JSON.stringify({ item_id: item.id, accept_license: true }),
      });
      catalogError.textContent = formatMessage('models.queued', { name: item.name || item.id, job: started.job_id });
      await refreshDownloads();
    } catch (error) {
      catalogError.textContent = formatError(error);
      install.disabled = Boolean(item.installed) || !licenseUrl;
    }
  });
  actions.append(acknowledgeLabel, install);
  if (!licenseUrl) actions.append(make('p', 'engine-error', translation('models.installBlocked', 'Installation is disabled because no safe HTTPS license link is available.')));
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
  catalogSummary.textContent = formatMessage('models.showing', { visible: filtered.length, total: catalogItems.length }, `Showing ${filtered.length} of ${catalogItems.length} components`);
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
    catalogList.append(make('div', 'empty', catalogItems.length ? translation('models.noFilterMatch', 'No components match this filter. Try another type or clear the search.') : translation('models.noneAvailable', 'No compatible catalog components are available for this computer yet.')));
    return;
  }
  for (const item of filtered) catalogList.append(catalogItemCard(item));
  renderRuntimeProfile();
}

function renderRuntimeProfile() {
  if (!runtimeProfilePanel || !runtimeProfileEngineSelect || !runtimeProfileModelSelect) return;
  const previousEngine = runtimeProfileEngineSelect.value;
  const previousModel = runtimeProfileModelSelect.value;
  const installedEngines = catalogItems.filter((item) => item.type === 'engine' && item.installed);
  const installedModels = catalogItems.filter((item) => item.type === 'model' && item.installed);
  const selectedEngineId = runtimeProfileDraftDirty
    ? runtimeProfileDraft.engine_component_id
    : (runtimeProfile?.engine_component_id || previousEngine || '');
  const selectedModelId = runtimeProfileDraftDirty
    ? runtimeProfileDraft.model_component_id
    : (runtimeProfile?.model_component_id || previousModel || '');

  const fillOptions = (select, placeholderKey, placeholderFallback, items, selectedId) => {
    select.replaceChildren();
    const placeholder = make('option', '', translation(placeholderKey, placeholderFallback));
    placeholder.value = '';
    select.append(placeholder);
    for (const item of items) {
      const option = make('option', '', `${item.name || item.id}${item.backend ? ` · ${String(item.backend).toUpperCase()}` : ''}`);
      option.value = item.id;
      select.append(option);
    }
    if (selectedId && !items.some((item) => item.id === selectedId)) {
      const unavailable = make('option', '', `${selectedId} · ${translation('profile.notInstalled', 'not installed')}`);
      unavailable.value = selectedId;
      select.append(unavailable);
    }
    select.value = selectedId;
  };

  fillOptions(runtimeProfileEngineSelect, 'profile.noInstalledEngine', 'No installed engine', installedEngines, selectedEngineId);
  fillOptions(runtimeProfileModelSelect, 'profile.noInstalledModel', 'No installed model', installedModels, selectedModelId);
  const contextValue = Number(runtimeProfileDraftDirty
    ? runtimeProfileDraft.context_size
    : (runtimeProfile?.context_size || 2048));
  const standardContexts = [2048, 4096, 8192, 16384, 32768];
  runtimeProfileContextSelect.replaceChildren(...standardContexts.map((size) => {
    const option = make('option', '', size.toLocaleString(currentLocale === 'tr' ? 'tr-TR' : 'en-US'));
    option.value = String(size);
    return option;
  }));
  if (!standardContexts.includes(contextValue)) {
    const custom = make('option', '', contextValue.toLocaleString(currentLocale === 'tr' ? 'tr-TR' : 'en-US'));
    custom.value = String(contextValue);
    runtimeProfileContextSelect.append(custom);
  }
  runtimeProfileContextSelect.value = String(contextValue);

  const selectedEngine = installedEngines.find((item) => item.id === runtimeProfileEngineSelect.value);
  const selectedModel = installedModels.find((item) => item.id === runtimeProfileModelSelect.value);
  const enginesRunning = engineSnapshot.some((engine) => engine.status === 'running');
  runtimeProfileApplyButton.disabled = !selectedEngine || !selectedModel || enginesRunning;
  const configured = Boolean(runtimeProfile?.engine_configured && runtimeProfile?.model_configured);
  const managed = Boolean(runtimeProfile?.engine_component_id && runtimeProfile?.model_component_id);
  if (runtimeProfileState) {
    runtimeProfileState.classList.toggle('ready', configured);
    runtimeProfileState.textContent = configured
      ? (managed ? translation('profile.configured', 'Profile configured') : translation('profile.customConfigured', 'Custom setup active'))
      : translation('profile.incomplete', 'Not configured');
  }
  if (runtimeProfileSummary) {
    if (enginesRunning) runtimeProfileSummary.textContent = translation('profile.stopEngineFirst', 'Stop the running engine before changing its active profile.');
    else if (configured && !managed) runtimeProfileSummary.textContent = translation('profile.customApplied', 'A manually configured engine/model is active. Use Advanced Settings to edit it, or choose an installed catalog profile here.');
    else if (!installedEngines.length) runtimeProfileSummary.textContent = translation('profile.installEngineFirst', 'Install a compatible engine package below. Installing it will not activate it automatically.');
    else if (!installedModels.length) runtimeProfileSummary.textContent = translation('profile.installModelFirst', 'Install a model below. Downloading it will not replace the model currently used by Chat.');
    else if (configured) runtimeProfileSummary.textContent = translation('profile.currentApplied', 'The selected engine and model are saved for Chat. Start the engine from Activity when you are ready.');
    else runtimeProfileSummary.textContent = translation('profile.chooseAndApply', 'Choose an installed engine and model, then apply the profile. This does not start the engine.');
  }
}

async function refreshRuntimeProfile() {
  try {
    runtimeProfile = await api('/v1/runtime/profile');
    if (!runtimeProfileDraftDirty) {
      runtimeProfileDraft = {
        engine_component_id: runtimeProfile.engine_component_id || '',
        model_component_id: runtimeProfile.model_component_id || '',
        context_size: Number(runtimeProfile.context_size || 2048),
      };
    }
    renderRuntimeProfile();
    updateFirstRunGuide();
  } catch (error) {
    if (runtimeProfileError) runtimeProfileError.textContent = formatError(error);
  }
}

for (const control of [runtimeProfileEngineSelect, runtimeProfileModelSelect, runtimeProfileContextSelect]) {
  control.addEventListener('change', () => {
    runtimeProfileDraft = {
      engine_component_id: runtimeProfileEngineSelect.value,
      model_component_id: runtimeProfileModelSelect.value,
      context_size: Number(runtimeProfileContextSelect.value),
    };
    runtimeProfileDraftDirty = true;
    renderRuntimeProfile();
  });
}

runtimeProfileApplyButton.addEventListener('click', async () => {
  runtimeProfileError.textContent = '';
  const engineComponentId = runtimeProfileEngineSelect.value;
  const modelComponentId = runtimeProfileModelSelect.value;
  if (!engineComponentId || !modelComponentId) {
    runtimeProfileError.textContent = translation('profile.selectBoth', 'Select an installed engine package and an installed model.');
    return;
  }
  runtimeProfileApplyButton.disabled = true;
  try {
    const result = await api('/v1/runtime/profile', {
      method: 'POST',
      body: JSON.stringify({
        engine_component_id: engineComponentId,
        model_component_id: modelComponentId,
        context_size: Number(runtimeProfileContextSelect.value),
      }),
    });
    runtimeProfile = result.profile;
    runtimeProfileDraftDirty = false;
    runtimeProfileDraft = {
      engine_component_id: runtimeProfile.engine_component_id || '',
      model_component_id: runtimeProfile.model_component_id || '',
      context_size: Number(runtimeProfile.context_size || 2048),
    };
    runtimeProfileSummary.textContent = translation('profile.applied', 'Runtime profile saved. Start the selected engine from Activity to enable Chat.');
    await Promise.all([refreshEngines(), refreshRuntimeProfile()]);
  } catch (error) {
    runtimeProfileError.textContent = formatError(error);
  } finally {
    renderRuntimeProfile();
  }
});

async function refreshCatalog() {
  catalogError.textContent = '';
  catalogRefreshButton.disabled = true;
  try {
    const result = await api('/v1/catalog');
    if (!Array.isArray(result.data)) throw new Error(translation('models.catalogInvalid', 'The component catalog response is invalid.'));
    catalogItems = result.data;
    const installedModels = catalogItems.filter((item) => item.type === 'model' && item.installed).length;
    updateFirstRunGuide();
    if (modelCountBadge) { modelCountBadge.textContent = String(installedModels); modelCountBadge.hidden = installedModels === 0; }
    renderCatalog();
    await refreshRuntimeProfile();
  } catch (error) {
    catalogItems = [];
    renderCatalog();
    catalogList.replaceChildren(make('div', 'empty', translation('models.catalogCouldNotLoad', 'Could not load the component catalog.')));
    catalogError.textContent = formatError(error);
  } finally {
    catalogRefreshButton.disabled = false;
  }
}

function translateJobState(state) {
  const normalized = String(state || 'unknown').toLowerCase();
  const known = ['queued', 'running', 'downloading', 'verifying', 'extracting', 'configuring', 'succeeded', 'failed', 'canceled', 'interrupted'];
  return known.includes(normalized) ? translation(`activity.jobState.${normalized}`, normalized) : normalized;
}

function translateJobPhase(phase) {
  const raw = String(phase || '').trim();
  const normalized = raw.toLowerCase();
  const patterns = [
    [/interrupt|not running/, 'interrupted'],
    [/stream|generat|inference|model output/, 'streaming'],
    [/download|fetch|transfer/, 'downloading'],
    [/verif|checksum|hash/, 'verifying'],
    [/extract|unpack/, 'extracting'],
    [/install|finaliz|register|configur/, 'installing'],
    [/start|launch|boot/, 'starting'],
    [/stop|shut.?down/, 'stopping'],
    [/cancel/, 'canceled'],
    [/fail|error/, 'failed'],
    [/complete|success|ready|finished/, 'completed'],
    [/queue|pending/, 'queued'],
    [/^running$/, 'running'],
  ];
  const match = patterns.find(([pattern]) => pattern.test(normalized));
  return match ? translation(`activity.phase.${match[1]}`, raw) : (raw || translation('activity.phase.running', 'Running'));
}

function downloadJobCard(job) {
  const card = make('article', 'download-card');
  const header = make('div', 'download-job-header');
  const operationTitles = {
    'engine-start': translation('activity.operation.engineStart', 'Start engine'),
    'engine-stop': translation('activity.operation.engineStop', 'Stop engine'),
    inference: translation('activity.operation.inference', 'Chat inference'),
  };
  const isOperation = Object.prototype.hasOwnProperty.call(operationTitles, job.kind);
  const jobTitle = operationTitles[job.kind] || (job.item_id === 'runtime-update' ? translation('activity.runtimeUpdate', 'Local AI Runtime update') : job.item_id);
  const stateLabel = translateJobState(job.state);
  header.append(make('h3', '', `${jobTitle} · ${stateLabel}`));
  const kindLabel = translation(`activity.kind.${String(job.kind || 'component').toLowerCase()}`, job.kind || translation('common.component', 'component'));
  header.append(make('span', 'tag', isOperation ? translation('activity.operationTag', 'operation') : kindLabel));
  card.append(header);
  card.append(make('p', '', translateJobPhase(job.phase || job.state)));
  if (job.state === 'queued' && Number(job.queue_position || 0) > 0) {
    card.append(make('p', 'hint', formatMessage('activity.queuePosition', { position: job.queue_position }, `Queue position: ${job.queue_position}`)));
  }
  if (Array.isArray(job.dependency_plan) && job.dependency_plan.length) {
    const dependencyNames = job.dependency_plan.map((dependency) => dependency.id).join(', ');
    card.append(make('p', 'hint', formatMessage('activity.dependencyPlan', { dependencies: dependencyNames }, `Required components: ${dependencyNames}`)));
  }
  if (Number(job.attempt || 1) > 1) card.append(make('p', 'hint', `${translation('activity.attempt', 'Attempt')} ${job.attempt}`));
  if (isOperation) {
    if (Number(job.progress_bytes || 0) > 0) card.append(make('p', 'hint', formatMessage('activity.outputReceivedBytes', { size: formatBytes(Number(job.progress_bytes)) }, `${formatBytes(Number(job.progress_bytes))} ${translation('activity.outputReceived', 'output received')}`)));
    if (Number(job.duration_ms || 0) > 0) card.append(make('p', 'hint', `${translation('activity.duration', 'Duration')}: ${(Number(job.duration_ms) / 1000).toFixed(2)} s`));
  } else {
    const progress = make('progress', 'download-progress');
    progress.max = 100;
    progress.value = Math.max(0, Math.min(100, Number(job.progress_percent || 0)));
    progress.setAttribute('aria-label', formatMessage('activity.downloadProgress', { percent: progress.value }, `Download progress ${progress.value}%`));
    card.append(progress);
    const total = Number(job.total_bytes || 0);
    const downloaded = Number(job.downloaded_bytes || 0);
    card.append(make('p', 'hint', total > 0 ? formatMessage('activity.downloadedOf', { downloaded: formatBytes(downloaded), total: formatBytes(total), percent: progress.value }) : formatMessage('activity.downloaded', { downloaded: formatBytes(downloaded) })));
  }
  if (job.version) card.append(make('p', 'hint', formatMessage('activity.upstreamVersion', { version: job.version })));
  if (job.installed_path) card.append(make('p', 'hint', job.kind === 'update' ? formatMessage('activity.verifiedPackageAt', { path: job.installed_path }) : formatMessage('activity.installedAt', { path: job.installed_path })));
  if (job.error) {
    card.append(make('p', 'engine-error', job.error));
    if (String(job.error).includes('verified staging file was kept')) {
      card.append(make('p', 'recovery-hint', translation('models.recoveryHint', 'The model is already downloaded and verified. Close any app that may be using the model file, confirm the license again, then retry finalization without downloading it again.')));
    }
  }
  if (job.last_error && !job.error) {
    card.append(make('p', 'hint', formatMessage('activity.previousErrorDetail', { detail: job.last_error }, `${translation('activity.previousError', 'Previous attempt')}: ${job.last_error}`)));
  }
  const active = ['queued', 'running', 'downloading', 'verifying', 'extracting', 'configuring'].includes(job.state);
  if (active && (!isOperation || job.cancellable)) {
    const cancel = make('button', 'button secondary', job.cancel_requested
      ? translation('activity.cancelRequested', 'Cancellation requested…') : translation('activity.cancel', 'Cancel'));
    cancel.type = 'button';
    cancel.disabled = Boolean(job.cancel_requested);
    cancel.addEventListener('click', async () => {
      cancel.disabled = true;
      try {
        await api(`/v1/jobs/${encodeURIComponent(job.id)}`, { method: 'DELETE' });
        await refreshDownloads();
      } catch (error) {
        catalogError.textContent = formatError(error);
        cancel.disabled = false;
      }
    });
    card.append(cancel);
  } else if (job.retryable) {
    const retry = make('button', 'button secondary', translation('activity.retry', 'Retry'));
    retry.type = 'button';
    retry.addEventListener('click', async () => {
      retry.disabled = true;
      try {
        await api(`/v1/jobs/${encodeURIComponent(job.id)}/retry`, { method: 'POST' });
        await refreshDownloads();
      } catch (error) {
        catalogError.textContent = formatError(error);
        retry.disabled = false;
      }
    });
    card.append(retry);
  }
  return card;
}

async function refreshDownloads() {
  try {
    const result = await api('/v1/jobs');
    if (!Array.isArray(result.data)) throw new Error('The unified job list response is invalid.');
    const jobs = [...result.data].sort((left, right) => Number(right.created_at || 0) - Number(left.created_at || 0));
    downloadJobsSnapshot = jobs;
    // Update only the install action label while polling. Rebuilding the full
    // catalog would reset license acknowledgements and move keyboard focus.
    for (const button of catalogList.querySelectorAll('button[data-catalog-install-id]')) {
      const componentId = button.dataset.catalogInstallId;
      const component = catalogItems.find((item) => item.id === componentId);
      if (!component) continue;
      const activeJob = jobs.find((job) => job.item_id === componentId &&
        ['queued', 'running', 'downloading', 'verifying', 'extracting', 'configuring'].includes(job.state));
      button.textContent = component.installed ? translation('models.installed', 'Installed')
        : activeJob ? (activeJob.state === 'queued' ? translation('models.inQueue', 'In queue') : translation('models.installing', 'Installing…'))
          : translation('models.downloadInstall', 'Download & install');
      button.disabled = Boolean(component.installed) || Boolean(activeJob) || !component.license_url;
    }
    const activeJobs = jobs.filter((job) => ['queued', 'running', 'downloading', 'verifying', 'extracting', 'configuring'].includes(job.state)).length;
    if (activityCountBadge) { activityCountBadge.textContent = String(activeJobs); activityCountBadge.hidden = activeJobs === 0; }
    if (activityActiveJobs) activityActiveJobs.textContent = String(activeJobs);
    downloadList.replaceChildren();
    if (!jobs.length) {
      downloadList.append(make('div', 'empty', translation('activity.noJobs', 'No runtime operations yet.')));
      return;
    }
    let hasActive = false;
    let changedComponentTerminalState = false;
    let completedComponentSuccessfully = false;
    for (const job of jobs) {
      downloadList.append(downloadJobCard(job));
      if (['queued', 'running', 'downloading', 'verifying', 'extracting', 'configuring'].includes(job.state)) hasActive = true;
      if (['succeeded', 'failed', 'canceled', 'interrupted'].includes(job.state) && !completedDownloadJobs.has(job.id)) {
        completedDownloadJobs.add(job.id);
        if (['engine', 'model'].includes(job.kind)) {
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
    catalogError.textContent = formatError(error);
    return false;
  }
}

function renderResources(snapshot) {
  resourcesSummary.replaceChildren();
  const facts = make('div', 'resource-facts');
  const addFact = (label, value, detail = '') => {
    const card = make('article', 'resource-fact');
    card.append(make('span', 'resource-label', label));
    card.append(make('strong', '', value));
    if (detail) card.append(make('small', '', detail));
    facts.append(card);
  };
  const cpu = typeof snapshot.cpu_utilization_percent === 'number' && Number.isFinite(snapshot.cpu_utilization_percent)
    ? `${snapshot.cpu_utilization_percent.toFixed(1)}%`
    : translation('activity.resourcesSampling', 'Sampling…');
  const memory = typeof snapshot.memory_utilization_percent === 'number' && Number.isFinite(snapshot.memory_utilization_percent)
    ? `${snapshot.memory_utilization_percent.toFixed(1)}%`
    : translation('activity.resourcesUnavailable', 'Not available');
  const pressureLabels = {
    normal: translation('activity.pressureNormal', 'Normal'),
    constrained: translation('activity.pressureConstrained', 'Constrained'),
    critical: translation('activity.pressureCritical', 'Critical'),
    unknown: translation('activity.pressureUnknown', 'Unknown'),
  };
  addFact(translation('activity.resourcesCpu', 'System CPU'), cpu,
    `${Number(snapshot.logical_cpu_count || 0)} ${translation('activity.resourcesLogicalCpus', 'logical CPUs')}`);
  addFact(translation('activity.resourcesMemory', 'Memory used'), memory,
    `${formatBytes(snapshot.used_memory_bytes)} / ${formatBytes(snapshot.total_memory_bytes)}`);
  addFact(translation('activity.resourcesAvailable', 'Available RAM'), formatBytes(snapshot.available_memory_bytes));
  addFact(translation('activity.resourcesPressure', 'Memory pressure'), pressureLabels[snapshot.memory_pressure] || pressureLabels.unknown);
  resourcesSummary.append(facts);

  const systemMemoryPercent = snapshot.memory_utilization_percent;
  if (typeof systemMemoryPercent === 'number' && Number.isFinite(systemMemoryPercent)) {
    const meter = document.createElement('progress');
    meter.className = 'resource-meter';
    meter.max = 100;
    meter.value = Math.max(0, Math.min(100, systemMemoryPercent));
    meter.setAttribute('aria-label', translation('activity.resourcesMemory', 'Memory used'));
    resourcesSummary.append(meter);
  }

  const processSection = make('section', 'resource-section');
  processSection.append(make('h3', '', translation('activity.resourcesEngines', 'Managed engine processes')));
  const engines = Array.isArray(snapshot.engines) ? snapshot.engines : [];
  if (!engines.length) {
    processSection.append(make('p', 'hint', translation('activity.resourcesNoEngines', 'No managed engines are configured.')));
  } else {
    const processList = make('div', 'resource-process-list');
    for (const engine of engines) {
      const item = make('article', 'resource-process');
      const heading = make('div', 'resource-process-heading');
      heading.append(make('strong', '', engine.engine_id || translation('activity.resourcesEngine', 'Engine')));
      heading.append(make('span', 'tag', translateEngineState(engine.state)));
      item.append(heading);
      const cpuValue = typeof engine.cpu_percent === 'number' && Number.isFinite(engine.cpu_percent)
        ? `${engine.cpu_percent.toFixed(1)}%`
        : translation('activity.resourcesSampling', 'Sampling…');
      item.append(make('p', '', `${translation('activity.resourcesProcessCpu', 'CPU share')}: ${cpuValue}`));
      item.append(make('p', '', `${translation('activity.resourcesProcessMemory', 'Resident memory')}: ${formatBytes(engine.resident_memory_bytes)}`));
      processList.append(item);
    }
    processSection.append(processList);
  }
  resourcesSummary.append(processSection);

  const gpuSection = make('section', 'resource-section');
  gpuSection.append(make('h3', '', translation('activity.resourcesGpu', 'GPU telemetry')));
  const gpuTelemetry = snapshot.gpu_telemetry || {};
  if (gpuTelemetry.status === 'available' && Array.isArray(gpuTelemetry.devices) && gpuTelemetry.devices.length) {
    const gpuList = make('div', 'resource-process-list');
    for (const gpu of gpuTelemetry.devices) {
      const item = make('article', 'resource-process');
      item.append(make('strong', '', `${translation('activity.resourcesGpuDevice', 'GPU')} ${Number(gpu.index)}`));
      const utilization = typeof gpu.utilization_percent === 'number' && Number.isFinite(gpu.utilization_percent)
        ? `${gpu.utilization_percent}%` : translation('activity.resourcesUnavailable', 'Not available');
      item.append(make('p', '', `${translation('activity.resourcesGpuUtilization', 'Utilization')}: ${utilization}`));
      item.append(make('p', '', `${translation('activity.resourcesGpuMemory', 'VRAM')}: ${formatBytes(gpu.memory_used_bytes)} / ${formatBytes(gpu.memory_total_bytes)}`));
      gpuList.append(item);
    }
    gpuSection.append(gpuList);
  } else {
    gpuSection.append(make('p', 'hint', gpuTelemetry.message || translation('activity.resourcesGpuUnavailable', 'Live GPU telemetry is unavailable on this device.')));
  }
  gpuSection.append(make('p', 'hint', translation('activity.resourcesGpuScope', 'GPU readings are system-wide and do not prove that a particular engine uses the GPU.')));
  resourcesSummary.append(gpuSection);
}

async function refreshResources() {
  if (!resourcesSummary) return;
  resourcesError.textContent = '';
  resourcesRefreshButton.disabled = true;
  try {
    const snapshot = await api('/v1/system/resources');
    if (snapshot.schema_version !== 1 || !snapshot.platform || !Array.isArray(snapshot.engines) || !snapshot.gpu_telemetry) {
      throw new Error('The live resource snapshot response is invalid.');
    }
    renderResources(snapshot);
  } catch (error) {
    resourcesError.textContent = formatError(error);
  } finally {
    resourcesRefreshButton.disabled = false;
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
  if (!Number.isFinite(bytes) || bytes <= 0) return translation('common.notAvailable', 'Not available');
  const units = ['B', 'KB', 'MB', 'GB', 'TB'];
  let amount = bytes;
  let unit = 0;
  while (amount >= 1024 && unit < units.length - 1) { amount /= 1024; unit += 1; }
  return `${amount >= 10 || unit === 0 ? amount.toFixed(0) : amount.toFixed(1)} ${units[unit]}`;
}

function renderHardwareRecommendations(recommendations) {
  const section = make('section', 'hardware-recommendations');
  section.append(make('h3', '', translation('hardware.recommendationsTitle', 'Recommended starting points')));
  section.append(make('p', 'hint', translation('hardware.recommendationsDisclaimer', 'These are local hardware-based suggestions, not compatibility guarantees.')));
  const grid = make('div', 'recommendation-grid');
  const labels = {
    recommended: translation('hardware.goodStart', 'Good starting point'),
    caution: translation('hardware.checkMemory', 'Check memory headroom'),
    'resource-constrained': translation('hardware.limitedMemory', 'Limited memory'),
    'needs-space': translation('hardware.freeDisk', 'Free disk space first'),
    unknown: translation('hardware.manualReview', 'Needs manual review'),
    candidate: translation('hardware.gpuCandidate', 'GPU candidate · not verified'),
    review: translation('hardware.gpuReview', 'GPU needs review'),
    'not-detected': translation('hardware.nvidiaMissing', 'NVIDIA adapter not detected'),
  };
  for (const item of Array.isArray(recommendations) ? recommendations : []) {
    const card = make('article', `recommendation-card status-${String(item.status || 'unknown')}`);
    const heading = make('div', 'recommendation-heading');
    heading.append(make('h4', '', item.title || translation('hardware.startingPoint', 'Starting point')));
    heading.append(make('span', 'recommendation-status', labels[item.status] || translation('hardware.manualReview', 'Needs review')));
    card.append(heading);
    if (item.model) card.append(make('p', 'recommendation-model', item.model));
    if (item.engine) card.append(make('p', 'recommendation-engine', item.engine));
    if (item.reason) card.append(make('p', 'recommendation-reason', item.reason));
    const action = document.createElement('a');
    action.className = 'button secondary';
    action.href = '#models';
    action.textContent = item.id === 'nvidia-cuda-chat' ? translation('hardware.reviewEngineOptions', 'Review engine options') : translation('hardware.browseStarter', 'Browse starter components');
    card.append(action);
    grid.append(card);
  }
  section.append(grid);
  return section;
}

function renderHardware(info) {
  hardwareSummary.replaceChildren();
  const platformLabel = `${String(info.platform || 'Unknown').toUpperCase()} ${String(info.architecture || '').toUpperCase()}`.trim();
  if (overviewDeviceState) overviewDeviceState.textContent = platformLabel || translation('hardware.unknown', 'Unknown');
  if (overviewDeviceDetail) overviewDeviceDetail.textContent = `${formatMessage('hardware.processorCount', { count: Number(info.logical_cpu_count || 0) })} · ${formatBytes(info.total_memory_bytes)} RAM`;

  const facts = make('div', 'hardware-facts');
  const addFact = (label, value) => {
    const fact = make('div', 'hardware-fact');
    fact.append(make('strong', '', value), make('span', '', label));
    facts.append(fact);
  };
  addFact(translation('hardware.platform', 'Platform'), `${info.platform || translation('hardware.unknown', 'Unknown')} ${info.architecture || ''}`.trim());
  addFact(translation('hardware.logicalProcessors', 'Logical processors'), Number.isFinite(Number(info.logical_cpu_count)) ? String(info.logical_cpu_count) : translation('common.notAvailable', 'Not available'));
  addFact(translation('hardware.systemRam', 'System RAM'), formatBytes(info.total_memory_bytes));
  addFact(translation('hardware.availableRam', 'Available RAM'), formatBytes(info.available_memory_bytes));
  addFact(translation('hardware.freeModelSpace', 'Free space for models'), formatBytes(info.available_disk_bytes));
  hardwareSummary.append(facts);

  const gpuHeading = make('p', 'eyebrow', translation('hardware.graphicsAdapters', 'GRAPHICS ADAPTERS'));
  hardwareSummary.append(gpuHeading);
  if (Array.isArray(info.gpus) && info.gpus.length) {
    const gpuList = make('div', 'hardware-gpu-list');
    for (const gpu of info.gpus) {
      const item = make('article', 'hardware-gpu');
      item.append(make('h3', '', gpu.name || `${gpu.vendor || translation('hardware.unknownVendor', 'Unknown vendor')} ${translation('hardware.graphicsAdapter', 'graphics adapter')}`));
      item.append(make('p', '', `${gpu.vendor || translation('hardware.unknownVendor', 'Unknown vendor')} · ${gpu.vendor_id || translation('hardware.vendorIdUnavailable', 'Vendor ID unavailable')} / ${gpu.device_id || translation('hardware.deviceIdUnavailable', 'Device ID unavailable')}`));
      item.append(make('p', '', formatMessage('hardware.dedicatedMemory', { size: formatBytes(gpu.dedicated_memory_bytes) })));
      gpuList.append(item);
    }
    hardwareSummary.append(gpuList);
  } else {
    const probeMessage = info.gpu_probe_message || (info.gpu_probe_status === 'unsupported'
      ? translation('hardware.gpuUnsupported', 'GPU enumeration is not supported by this build.')
      : translation('hardware.noGpuReported', 'No graphics adapter was reported by the operating system.'));
    hardwareSummary.append(make('p', 'hint', probeMessage));
  }

  const guidance = make('div', 'hardware-guidance');
  guidance.append(make('strong', '', formatMessage('hardware.startingProfile', { profile: String(info.memory_profile || translation('hardware.unknown', 'unknown')).replaceAll('-', ' ') })));
  guidance.append(make('p', '', info.recommendation || translation('hardware.cpuBaseline', 'Start with the CPU baseline and verify each model on this computer.')));
  guidance.append(make('p', '', translation('hardware.gpuUnverified', 'GPU inference is not automatically confirmed by detecting an adapter or installing a CUDA package.')));
  hardwareSummary.append(guidance);
  hardwareSummary.append(renderHardwareRecommendations(info.recommendations));
}

async function refreshHardware() {
  hardwareError.textContent = '';
  hardwareRefreshButton.disabled = true;
  hardwareRefreshButton.textContent = translation('updates.checking', 'Checking…');
  try {
    const info = await api('/v1/system/hardware');
    hardwareInventoryLoaded = true;
    renderHardware(info);
    updateFirstRunGuide();
  } catch (error) {
    hardwareInventoryLoaded = false;
    updateFirstRunGuide();
    hardwareError.textContent = formatError(error);
    hardwareSummary.replaceChildren(make('div', 'empty', translation('hardware.readFailed', 'Device information could not be read. The local model runtime remains available.')));
  } finally {
    hardwareRefreshButton.disabled = false;
    hardwareRefreshButton.textContent = translation('device.refresh', 'Refresh device info');
  }
}

function translateEngineState(state) {
  const normalized = String(state || 'unknown').toLowerCase();
  const keys = { running: 'engine.running', starting: 'engine.starting', stopping: 'engine.stopping', stopped: 'engine.stopped', failed: 'engine.failed', ready: 'engine.ready', unknown: 'engine.unknownState' };
  return translation(keys[normalized] || 'engine.unknownState', normalized);
}

function engineCard(engine) {
  const card = make('article', 'engine-card');
  const info = make('div');
  info.append(make('p', 'engine-name', engine.display_name || engine.id));
  const meta = make('div', 'engine-meta');
  const readinessClass = engine.ready ? 'ready' : (engine.status === 'running' ? 'loading' : '');
  const readinessLabel = engine.ready ? translation('engine.ready', 'ready') : (engine.status === 'running' ? translation('engine.loadingModel', 'loading model') : translateEngineState(engine.status));
  meta.append(make('span', `tag ${readinessClass}`, readinessLabel));
  if (engine.pid) meta.append(make('span', 'tag', `PID ${engine.pid}`));
  for (const capability of (engine.capabilities || [])) meta.append(make('span', 'tag', capability));
  if (engine.backend_package && engine.backend_package !== 'manual-or-unknown-package') {
    const packageLabel = engine.backend_package === 'cuda12-package' ? translation('engine.package.cuda12', 'CUDA 12 package')
      : engine.backend_package === 'cuda13-package' ? translation('engine.package.cuda13', 'CUDA 13 package') : translation('engine.package.cpu', 'CPU package');
    meta.append(make('span', 'tag', packageLabel));
  }
  if (engine.backend_target === 'gpu-requested') meta.append(make('span', 'tag loading', translation('engine.gpuLayersRequested', 'GPU layers requested')));
  else if (engine.backend_target === 'cpu') meta.append(make('span', 'tag', translation('engine.cpuRequested', 'CPU requested')));
  if (engine.gpu_execution_evidence === 'offload-reported') {
    const count = Number.isInteger(engine.gpu_offloaded_layers) && Number.isInteger(engine.gpu_total_layers)
      ? formatMessage('engine.offloadLayersReported', { offloaded: engine.gpu_offloaded_layers, total: engine.gpu_total_layers }) : translation('engine.offloadReported', 'offload reported');
    meta.append(make('span', 'tag ready', formatMessage('engine.gpuEvidence', { detail: count })));
  } else if (engine.gpu_execution_evidence === 'no-layers-offloaded') {
    meta.append(make('span', 'tag loading', translation('engine.noGpuLayersReported', 'No GPU layers reported')));
  } else if (engine.gpu_execution_evidence === 'cuda-runtime-reported') {
    meta.append(make('span', 'tag loading', translation('engine.cudaInitializedUnknown', 'CUDA initialized · offload unknown')));
  } else if (engine.backend_target === 'gpu-requested') {
    meta.append(make('span', 'tag loading', translation('engine.gpuExecutionUnverified', 'GPU execution not verified')));
  }
  info.append(meta);
  if (engine.backend_target === 'gpu-requested' || engine.backend_package === 'cuda12-package' || engine.backend_package === 'cuda13-package') {
    const evidenceSummary = engine.gpu_execution_evidence_summary || translation('engine.gpuEvidenceMissing', 'GPU execution has not been verified from the current engine log.');
    info.append(make('p', engine.gpu_execution_evidence === 'offload-reported' ? 'engine-loading' : 'hint', evidenceSummary));
    if (engine.gpu_execution_evidence === 'offload-reported') {
      info.append(make('p', 'hint', translation('engine.gpuEvidenceDisclaimer', 'This confirms that the engine reported layer offload at startup. It does not measure live GPU utilization during inference.')));
    }
  }
  if (engine.last_error) info.append(make('p', 'engine-error', engine.last_error));
  else if (!engine.executable_configured) info.append(make('p', 'engine-error', translation('engine.configureNeeded', 'Configure llama.cpp above or set the engine executable path.')));
  else if ((engine.capabilities || []).includes('chat') && !engine.model_configured) info.append(make('p', 'engine-error', translation('engine.profileModelNeeded', 'Choose and apply an installed model in Models → Active runtime profile before starting this engine.')));
  else if (engine.status === 'running' && !engine.ready) info.append(make('p', 'engine-loading', translation('engine.waitingReady', 'The process is running; waiting for the model server to report ready. Large models may take a while to load.')));
  const actions = make('div', 'engine-actions');
  const start = make('button', 'button primary', translation('engine.start', 'Start'));
  start.setAttribute('aria-label', formatMessage('engine.startAria', { name: engine.display_name || engine.id }, `Start ${engine.display_name || engine.id}`));
  start.type = 'button';
  start.disabled = engine.status === 'running' || !engine.executable_configured || ((engine.capabilities || []).includes('chat') && !engine.model_configured);
  start.addEventListener('click', () => controlEngine(engine.id, 'start', start));
  const stop = make('button', 'button secondary', translation('engine.stop', 'Stop'));
  stop.setAttribute('aria-label', formatMessage('engine.stopAria', { name: engine.display_name || engine.id }, `Stop ${engine.display_name || engine.id}`));
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
    engineSnapshot = Array.isArray(result.data) ? result.data : [];
    updateFirstRunGuide();
    engineList.replaceChildren();
    if (!result.data?.length) {
      activeChatEngineReady = false;
      engineList.append(make('div', 'empty', translation('engine.setupNeeded', 'No engines configured yet. Install an engine from Models or configure one manually in Settings.')));
      updateChatSendAvailability();
      renderRuntimeProfile();
      chatReadiness.textContent = translation('chat.notReady', 'Install and start a chat model to begin.');
      if (overviewRuntimeState) overviewRuntimeState.textContent = translation('engine.setupRequired', 'Setup needed');
      if (overviewRuntimeDetail) overviewRuntimeDetail.textContent = translation('engine.noChatEngine', 'No chat engine is configured');
      if (overviewModelState) overviewModelState.textContent = translation('engine.noModel', 'No model yet');
      if (overviewModelDetail) overviewModelDetail.textContent = translation('overview.modelDetail');
      return;
    }
    for (const engine of result.data) engineList.append(engineCard(engine));
    const defaultChat = result.data.find((engine) => engine.default_for_chat);
    if (overviewRuntimeState) overviewRuntimeState.textContent = defaultChat?.ready
      ? translation('engine.ready', 'Ready')
      : (defaultChat?.status === 'running' ? translation('engine.loading', 'Loading') : translation('engine.stopped', 'Stopped'));
    if (overviewRuntimeDetail) overviewRuntimeDetail.textContent = defaultChat?.ready
      ? translation('engine.serverReady', 'Local model server is ready')
      : (defaultChat?.status === 'running' ? translation('engine.waitingModel', 'Waiting for the model to load') : translation('engine.noActiveChat', 'No active chat engine'));
    if (overviewModelState) overviewModelState.textContent = defaultChat?.model_configured
      ? translation('engine.configured', 'Configured')
      : translation('engine.notConfigured', 'Not configured');
    if (overviewModelDetail) overviewModelDetail.textContent = defaultChat?.display_name || translation('overview.modelDetail');
    activeChatEngineReady = Boolean(defaultChat?.ready && defaultChat?.model_configured);
    updateChatSendAvailability();
    renderRuntimeProfile();
    const gpuReady = Boolean(defaultChat?.ready && defaultChat.backend_target === 'gpu-requested');
    gpuObservationButton.disabled = !gpuReady;
    gpuObservationButton.title = gpuReady
      ? translation('engine.gpuMeasureTitle', 'Runs a fixed local chat task and samples NVIDIA system utilization/memory while it runs.')
      : translation('engine.gpuConfigureTitle', 'Configure a CUDA-enabled engine with GPU layers set to auto, then start it.');
    if (!defaultChat) chatReadiness.textContent = translation('engine.chatConfigure', 'Configure llama.cpp above, then start the engine.');
    else if (defaultChat.ready) chatReadiness.textContent = translation('engine.chatReady', 'Model server ready. Chat requests stay on this machine.');
    else if (defaultChat.status === 'running') chatReadiness.textContent = translation('engine.chatLoading', 'The model is loading or the server is not ready yet.');
    else if (!defaultChat.executable_configured || !defaultChat.model_configured) chatReadiness.textContent = translation('engine.chatCompleteSetup', 'Choose an installed engine and model in Models → Active runtime profile, apply it, then start the engine from Activity.');
    else chatReadiness.textContent = translation('engine.chatStart', 'Start the configured engine to enable chat.');
  } catch (error) {
    activeChatEngineReady = false;
    updateChatSendAvailability();
    engineError.textContent = formatError(error);
    engineList.replaceChildren(make('div', 'empty', translation('engine.loadFailed', 'Could not load engine status.')));
    gpuObservationButton.disabled = true;
    renderRuntimeProfile();
  }
}

async function waitForEngineReady(id, timeoutMs = 180000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const result = await api('/v1/engines');
    const engine = result.data?.find((entry) => entry.id === id);
    if (!engine) throw new Error(formatMessage('engine.missing', { id }, `Engine '${id}' is not present in the runtime configuration.`));
    if (engine.ready) return;
    if (engine.status === 'failed') throw new Error(engine.last_error || translation('engine.exited', 'Engine exited before becoming ready. Export diagnostics or inspect the per-user runtime logs.'));
    engineError.textContent = engine.status === 'running'
      ? translation('engine.waiting', 'Engine started. Waiting for the local model server to become ready…')
      : translation('engine.waitingStartup', 'Waiting for engine startup…');
    await new Promise((resolve) => setTimeout(resolve, 1000));
  }
  throw new Error(translation('engine.timeout', 'Timed out waiting for the model server. Export diagnostics and check the engine executable/model path.'));
}

async function controlEngine(id, action, button) {
  button.disabled = true;
  engineError.textContent = '';
  try {
    await api(`/v1/engines/${encodeURIComponent(id)}/${action}`, { method: 'POST', body: '{}' });
    if (action === 'start') await waitForEngineReady(id);
    await refreshEngines();
  } catch (error) {
    engineError.textContent = formatError(error);
    await refreshEngines();
  } finally {
    button.disabled = false;
  }
}

llamaSetupForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  setupStatus.className = 'status-message';
  setupStatus.textContent = translation('setup.validating', 'Validating paths and saving configuration…');
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
    setupStatus.textContent = formatMessage('setup.saved', { id: result.id, backend: result.backend.toUpperCase() });
    await refreshEngines();
  } catch (error) {
    setupStatus.className = 'status-message failure';
    setupStatus.textContent = formatError(error);
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
  diagnosticsStatus.textContent = translation('diagnostics.preparing', 'Preparing a local diagnostic report…');
  try {
    const diagnosticsUrl = includeEngineLogsCheckbox.checked ? '/v1/diagnostics/export?include_engine_logs=1' : '/v1/diagnostics/export';
    const response = await fetch(diagnosticsUrl, { cache: 'no-store' });
    if (!response.ok) throw new Error(formatMessage('diagnostics.httpError', { status: response.status }, `Could not create diagnostic report (HTTP ${response.status}).`));
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
    diagnosticsStatus.textContent = translation('diagnostics.created', 'Diagnostic report created on this device. Review it before sharing; it was not uploaded anywhere.');
  } catch (error) {
    diagnosticsStatus.className = 'status-message failure';
    diagnosticsStatus.textContent = formatError(error);
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
  updateChatSendAvailability();
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
updateFirstRunGuide();

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

chatMessageInput.addEventListener('input', updateChatSendAvailability);

document.querySelector('#chat-form').addEventListener('submit', async (event) => {
  event.preventDefault();
  if (activeChatController) return;
  chatError.textContent = '';
  const messageText = chatMessageInput.value.trim();
  if (!messageText) return;
  if (!activeChatEngineReady) {
    chatError.textContent = translation('chat.engineRequired', 'The selected chat engine is not ready. Choose and apply a model in Models, then start the engine from Activity. Your message is still here.');
    updateChatSendAvailability();
    return;
  }
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
    updateFirstRunGuide();
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
      chatError.textContent = formatError(error);
      reportDiagnosticEvent('ui.chat.request_failed', `elapsed_ms=${Math.round(performance.now() - startedAt)}; input_chars=${messageText.length}; error_type=${String(error?.name || 'Error').slice(0, 40)}`);
    }
    conversation.updated_at = Date.now();
    saveChatHistory();
    if (conversation.id === activeChatId) renderChatWorkspace();
  } finally {
    if (activeChatController === controller) activeChatController = null;
    chatStopButton.hidden = true;
    chatMessageInput.disabled = false;
    updateChatSendAvailability();
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
  gpuObservationStatus.textContent = translation('gpu.running', 'Running a fixed local inference request and sampling NVIDIA GPU utilization/memory.');
  gpuObservationButton.disabled = true;
  const originalLabel = gpuObservationButton.textContent;
  gpuObservationButton.textContent = translation('gpu.measuring', 'Measuring…');
  try {
    const result = await api('/v1/diagnostics/gpu-observation', { method: 'POST', body: '{}' });
    const lines = [
      result.task_succeeded
        ? formatMessage('gpu.requestCompleted', { seconds: (Number(result.elapsed_ms || 0) / 1000).toFixed(2) })
        : translation('gpu.requestFailed', 'The local inference request did not complete successfully.'),
      result.message || translation('gpu.noDetails', 'No additional measurement details were provided.'),
      formatMessage('gpu.startupEvidence', { evidence: String(result.startup_evidence || 'not-verified'), detail: String(result.startup_evidence_summary || '') }),
    ];
    for (const device of Array.isArray(result.devices) ? result.devices : []) {
      const utilization = Number.isInteger(device.peak_utilization_percent) ? `${device.peak_utilization_percent}%` : translation('gpu.notAvailable', 'not available');
      const before = Number.isInteger(device.memory_before_mib) ? `${device.memory_before_mib} MiB` : translation('gpu.notAvailable', 'not available');
      const peak = Number.isInteger(device.memory_peak_mib) ? `${device.memory_peak_mib} MiB` : translation('gpu.notAvailable', 'not available');
      const after = Number.isInteger(device.memory_after_mib) ? `${device.memory_after_mib} MiB` : translation('gpu.notAvailable', 'not available');
      const total = Number.isInteger(device.memory_total_mib) ? ` / ${device.memory_total_mib} MiB total` : '';
      lines.push(formatMessage('gpu.peakUtilization', { index: device.index, utilization, before, peak, after, total }));
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
    gpuObservationStatus.textContent = formatError(error);
    reportDiagnosticEvent('ui.gpu_observation.failed', `http_status=${Number.isInteger(error?.httpStatus) ? error.httpStatus : 0}; error_type=${String(error?.name || 'Error').slice(0, 40)}`);
  } finally {
    gpuObservationButton.textContent = originalLabel;
    await refreshEngines();
  }
});

hardwareRefreshButton.addEventListener('click', refreshHardware);
resourcesRefreshButton.addEventListener('click', refreshResources);
updateDownloadButton.addEventListener('click', stageRuntimeUpdate);
checkUpdatesButton.addEventListener('click', () => checkForUpdates(true));
updateApplyButton.addEventListener('click', applyRuntimeUpdate);

updateChatSendAvailability();
refreshHealth();
refreshHardware();
refreshResources();
refreshEngines();
refreshCatalog();
refreshDownloads();
checkForUpdates();
window.addEventListener('focus', () => { if (Date.now() - lastUpdateCheckAt > 5 * 60 * 1000) checkForUpdates(); });
setInterval(() => { if (Date.now() - lastUpdateCheckAt > 6 * 60 * 60 * 1000) checkForUpdates(); }, 60 * 1000);
setInterval(refreshHealth, 5000);
setInterval(refreshEngines, 5000);
setInterval(refreshDownloads, 2000);
setInterval(() => { if (currentView === 'activity') { refreshActivitySnapshot(); refreshResources(); } }, 5000);
