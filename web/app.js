const apiStatus = document.querySelector('#api-status');
const apiDot = document.querySelector('#api-dot');
const engineList = document.querySelector('#engine-list');
const engineError = document.querySelector('#engine-error');
const chatError = document.querySelector('#chat-error');
const answerWrap = document.querySelector('#answer-wrap');
const answer = document.querySelector('#answer');

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
  else if (!engine.executable_configured) info.append(make('p', 'engine-error', 'Edit executable_path in config/engines.json before starting.'));
  const actions = make('div', 'engine-actions');
  const start = make('button', 'button primary', 'Start');
  start.type = 'button';
  start.disabled = engine.status === 'running';
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
setInterval(refreshHealth, 5000);
setInterval(refreshEngines, 5000);
