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
const runtimeVersion = document.querySelector('#runtime-version');

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
    throw new Error(message || `Request failed (${response.status})`);
  }
  return data;
}


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
    if (!latest) return;
    if (compareVersions(latest.tag_name, current) > 0) {
      updateTitle.textContent = `Version ${latest.tag_name} is available`;
      updateCopy.textContent = `You are using ${current}. Review the release notes and download the package for your operating system. Updates are never installed silently.`;
      updateLink.href = latest.html_url || REPOSITORY_RELEASES;
      updateLink.textContent = 'Download update';
      updateNotice.hidden = false;
    }
  } catch (error) {
    // Offline use is expected. Keep local inference usable when GitHub is unreachable.
    console.info('Update check skipped:', error.message);
  }
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

function engineCard(engine) {
  const card = make('article', 'engine-card');
  const info = make('div');
  info.append(make('p', 'engine-name', engine.display_name || engine.id));
  const meta = make('div', 'engine-meta');
  meta.append(make('span', `tag ${engine.status === 'running' ? 'running' : ''}`, engine.status));
  if (engine.pid) meta.append(make('span', 'tag', `PID ${engine.pid}`));
  for (const capability of (engine.capabilities || [])) meta.append(make('span', 'tag', capability));
  info.append(meta);
  if (engine.last_error) info.append(make('p', 'engine-error', engine.last_error));
  else if (!engine.executable_configured) info.append(make('p', 'engine-error', 'Choose an inference engine, then set executable_path in config/engines.json.'));
  else if ((engine.capabilities || []).includes('chat') && !engine.model_configured) info.append(make('p', 'engine-error', 'Set model_path to a real model file in config/engines.json before starting.'));
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
      engineList.append(make('div', 'empty', 'No engines configured yet. Create config/engines.json from the example file.'));
      return;
    }
    for (const engine of result.data) engineList.append(engineCard(engine));
  } catch (error) {
    engineError.textContent = error.message;
    engineList.replaceChildren(make('div', 'empty', 'Could not load engine status.'));
  }
}

async function controlEngine(id, action, button) {
  button.disabled = true;
  engineError.textContent = '';
  try {
    await api(`/v1/engines/${encodeURIComponent(id)}/${action}`, { method: 'POST', body: '{}' });
    await refreshEngines();
  } catch (error) {
    engineError.textContent = error.message;
    await refreshEngines();
  } finally {
    button.disabled = false;
  }
}

document.querySelector('#refresh').addEventListener('click', async () => {
  await Promise.all([refreshHealth(), refreshEngines()]);
});

document.querySelector('#chat-form').addEventListener('submit', async (event) => {
  event.preventDefault();
  chatError.textContent = '';
  answerWrap.hidden = true;
  const message = document.querySelector('#message').value.trim();
  if (!message) return;
  const send = document.querySelector('#send');
  send.disabled = true;
  send.textContent = 'Waiting…';
  try {
    const result = await api('/v1/chat/completions', {
      method: 'POST',
      body: JSON.stringify({ model: 'local-model', messages: [{ role: 'user', content: message }], stream: false }),
    });
    const content = result?.choices?.[0]?.message?.content;
    answer.textContent = typeof content === 'string' ? content : JSON.stringify(result, null, 2);
    answerWrap.hidden = false;
  } catch (error) {
    chatError.textContent = error.message;
  } finally {
    send.disabled = false;
    send.textContent = 'Send message';
  }
});

refreshHealth();
refreshEngines();
checkForUpdates();
setInterval(refreshHealth, 5000);
setInterval(refreshEngines, 5000);
