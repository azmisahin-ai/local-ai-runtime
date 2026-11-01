#!/usr/bin/env node
'use strict';

// Static UI contract for the first user-facing audio workflow. The runtime
// task endpoint remains independently exercised by task_http_smoke.py.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const html = fs.readFileSync(path.join(root, 'web/index.html'), 'utf8');
const js = fs.readFileSync(path.join(root, 'web/app.js'), 'utf8');
const css = fs.readFileSync(path.join(root, 'web/style.css'), 'utf8');
const en = JSON.parse(fs.readFileSync(path.join(root, 'web/locales/en.json'), 'utf8'));
const tr = JSON.parse(fs.readFileSync(path.join(root, 'web/locales/tr.json'), 'utf8'));
const manifest = JSON.parse(fs.readFileSync(path.join(root, 'catalog/manifest-v1.json'), 'utf8'));

for (const fragment of [
  'data-view="audio"',
  'data-view-target="audio"',
  'id="audio-engine-state"',
  'id="audio-model-select"',
  'id="audio-language-select"',
  'id="audio-file-input" type="file" accept=".wav,audio/wav,audio/x-wav"',
  'id="audio-dropzone"',
  'id="audio-transcribe"',
  'id="audio-transcript"',
  'id="audio-copy"',
  'id="audio-download"',
]) assert.ok(html.includes(fragment), `Speech & Audio UI is missing ${fragment}`);

for (const fragment of [
  "function renderAudioWorkspace()",
  "function installedAudioAsrModels()",
  "async function prepareAudioEngine(onEngineStopped = () => {})",
  "function validateSelectedWav(file, bytes)",
  "if (audioFileInput) audioFileInput.value = '';",
  "fetch('/v1/tasks/asr/transcribe'",
  "'Content-Type': 'audio/wav'",
  "'X-LAR-Model': modelId",
  "'X-LAR-Language': language",
  "await waitForAudioEngineReady()",
  "window.confirm(formatMessage('audio.stopOtherEnginesConfirm'",
  "audio.enginesRemainStopped",
  "reportDiagnosticEvent('ui.audio.asr.completed'",
]) assert.ok(js.includes(fragment), `Speech & Audio workflow is missing ${fragment}`);

assert.ok(css.includes('.audio-task-grid') && css.includes('@media (max-width: 780px)'), 'Speech & Audio view should have responsive layout styles');
assert.ok(html.includes('WAV only') || html.includes('audio.dropDescription'), 'Speech & Audio UI should explain the accepted WAV format');
assert.ok(js.includes("item.tasks.includes('asr.transcribe')"), 'The model picker should be capability-driven by installed ASR catalog entries');
assert.ok(js.includes("item.status === 'running'"), 'The UI should check for other running engines before loading ASR');

const asrModel = manifest.components.find((item) => item.id === 'qwen3-asr-0.6b-q8_0');
assert.ok(asrModel, 'The reviewed catalog should include Qwen3-ASR for the first speech workflow');
const modelMetadata = JSON.parse(asrModel.metadata_json);
assert.ok(modelMetadata.tasks.includes('asr.transcribe'), 'The catalogued Qwen model should advertise the ASR task');

assert.deepEqual(Object.keys(en).sort(), Object.keys(tr).sort(), 'English and Turkish locale keys must match');
for (const [key, value] of Object.entries(en)) {
  const placeholders = (text) => [...String(text).matchAll(/\{([a-zA-Z0-9_]+)\}/g)].map((match) => match[1]).sort();
  assert.deepEqual(placeholders(value), placeholders(tr[key]), `Locale placeholders differ for ${key}`);
}
for (const key of Object.keys(en).filter((key) => key.startsWith('audio.') || key === 'nav.audio' || key.startsWith('page.audio.') || key === 'common.preview' || key === 'activity.operation.audioTask')) {
  assert.ok(String(en[key]).trim().length > 0 && String(tr[key]).trim().length > 0, `Audio UI translation is blank: ${key}`);
}

console.log('Speech & Audio workspace contract passed: localized ASR UI, installed-model picker, WAV upload, engine lifecycle and typed local task route.');
