#!/usr/bin/env node
'use strict';

// Static contract for the Catalog v2 presentation. This deliberately checks
// that task taxonomy and the model/engine distinction remain data-driven.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const html = fs.readFileSync(path.join(root, 'web/index.html'), 'utf8');
const js = fs.readFileSync(path.join(root, 'web/app.js'), 'utf8');
const manifest = JSON.parse(fs.readFileSync(path.join(root, 'catalog/manifest-v1.json'), 'utf8'));
const en = JSON.parse(fs.readFileSync(path.join(root, 'web/locales/en.json'), 'utf8'));
const tr = JSON.parse(fs.readFileSync(path.join(root, 'web/locales/tr.json'), 'utf8'));

const mustContain = (source, fragment, description) => {
  assert.ok(source.includes(fragment), `Missing ${description}: ${fragment}`);
};

mustContain(html, 'id="catalog-task-filter"', 'dynamic model-task filter');
mustContain(html, 'data-catalog-filter="model"', 'model-only filter');
mustContain(html, 'data-catalog-filter="engine"', 'engine-only filter');
mustContain(js, 'function catalogTaskGroup(item)', 'metadata-driven task grouping');
mustContain(js, "appendSection('models.groupModels'", 'separate model section');
mustContain(js, "appendSection('models.groupEngines'", 'separate engine section');
mustContain(js, 'catalogLicenseAcknowledgements', 'license acknowledgement preservation');
mustContain(js, 'catalog-compact-action', 'compact installed-component action');
assert.ok(!js.includes('Boolean(item.installed) || Boolean(queuedJob)'), 'Installed entries must not render a large disabled install button');
mustContain(js, 'translation(\'models.selectForChat\'', 'installed model profile selection action');
mustContain(js, 'translation(\'models.selectEngine\'', 'installed engine profile selection action');
mustContain(js, 'translation(\'models.viewActivity\'', 'queued install Activity action');

const modelEntries = manifest.components.filter((entry) => entry.kind === 'model');
assert.ok(modelEntries.length > 0, 'Manifest must include at least one model for the Models view');
for (const entry of modelEntries) {
  const metadata = JSON.parse(entry.metadata_json);
  assert.ok(metadata.model_profile, `${entry.id} is missing model_profile metadata`);
  assert.ok(metadata.model_profile.task_group, `${entry.id} is missing task_group`);
  assert.ok(metadata.model_profile.quantization, `${entry.id} is missing quantization`);
  assert.ok(metadata.model_profile.format, `${entry.id} is missing model format`);
}
for (const entry of manifest.components.filter((item) => item.kind === 'engine')) {
  const metadata = JSON.parse(entry.metadata_json);
  assert.equal(metadata.model_profile, undefined, `${entry.id} must not be presented as model weights`);
}

const requiredKeys = [
  'models.groupModels', 'models.groupModelsDescription', 'models.groupEngines',
  'models.groupEnginesDescription', 'models.taskFilter', 'models.taskAll',
  'models.taskChat', 'models.taskVision', 'models.taskEmbeddings', 'models.taskSpeech',
  'models.taskImageGeneration', 'models.taskVideo', 'models.taskAudio',
  'models.engineRuntime', 'models.modelWeights', 'models.details', 'models.modelProfile',
  'models.selectedForChat', 'models.alreadySelected', 'models.selectForChat',
  'models.selectEngine', 'models.profileDrafted', 'models.installEngine',
  'models.installModel', 'models.viewActivity',
];
for (const key of requiredKeys) {
  assert.ok(Object.hasOwn(en, key), `English locale missing ${key}`);
  assert.ok(Object.hasOwn(tr, key), `Turkish locale missing ${key}`);
}

console.log(`Models UI contract passed: ${modelEntries.length} model profiles, ${manifest.components.filter((item) => item.kind === 'engine').length} engine runtimes, ${requiredKeys.length} EN/TR keys.`);
