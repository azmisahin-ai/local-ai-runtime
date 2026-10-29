#!/usr/bin/env node
'use strict';

// Exercise the static site's release renderer without a browser or network access.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

class MockElement {
  constructor(tagName = '', textContent = '') {
    this.tagName = tagName;
    this.textContent = textContent;
    this.className = '';
    this.children = [];
    this.href = '';
    this.afterChildren = [];
    this.target = '';
    this.rel = '';
  }
  append(...nodes) { this.children.push(...nodes); }
  replaceChildren(...nodes) { this.children = [...nodes]; }
  after(...nodes) { this.afterChildren.push(...nodes); }
  remove() { this.removed = true; }
}

function collectText(node) {
  return `${node.textContent || ''}${(node.children || []).map(collectText).join('')}`;
}

async function main() {
  const root = path.resolve(__dirname, '..');
  const product = JSON.parse(fs.readFileSync(path.join(root, 'site/product.json'), 'utf8'));
  const apiUrl = product.releasesApi;
  const siteHtml = fs.readFileSync(path.join(root, 'site/index.html'), 'utf8');
  const dashboardHtml = fs.readFileSync(path.join(root, 'web/index.html'), 'utf8');
  const dashboardScript = fs.readFileSync(path.join(root, 'web/app.js'), 'utf8');
  const windowsLauncher = fs.readFileSync(path.join(root, 'scripts/Start-LocalAIRuntime.ps1'), 'utf8');
  const assets = [
    'local-ai-runtime-windows-x64-setup.exe',
    'local-ai-runtime-windows-x64.zip',
    'local-ai-runtime-linux-x64.tar.gz',
    'local-ai-runtime-macos-x64.tar.gz',
    'local-ai-runtime-macos-arm64.tar.gz',
    'SHA256SUMS.txt',
  ].map((name) => ({
    name,
    browser_download_url: `https://example.invalid/releases/v0.1.0-alpha/${name}`,
  }));
  const release = {
    tag_name: 'v0.1.0-alpha',
    name: 'Local AI Runtime v0.1.0-alpha',
    html_url: 'https://example.invalid/releases/tag/v0.1.0-alpha',
    published_at: '2026-10-29T00:00:00Z',
    prerelease: true,
    draft: false,
    assets,
    body: '',
  };
  const selectors = [
    '#feature-grid', '#download-grid', '#latest-version', '#latest-meta',
    '#project-status', '#site-updated', '.checksum-download',
  ];
  const elements = new Map(selectors.map((selector) => [selector, selector.startsWith('.') ? null : new MockElement('div')]));
  const document = {
    querySelector(selector) { return elements.get(selector) ?? null; },
    createElement(tagName) { return new MockElement(tagName); },
    createTextNode(text) { return new MockElement('#text', text); },
  };
  const context = {
    document,
    console: { warn() {}, error() {}, log() {} },
    fetch: async (url) => {
      if (url === './product.json') return { ok: true, json: async () => product };
      if (url === apiUrl) return { ok: true, json: async () => [release] };
      throw new Error(`Unexpected test URL: ${url}`);
    },
    Date,
    String,
    Number,
    Math,
    Array,
    Object,
    Promise,
  };

  vm.runInNewContext(fs.readFileSync(path.join(root, 'site/app.js'), 'utf8'), context, {
    filename: 'site/app.js',
  });
  // The site's bootstrap is async and deliberately has no exported test API.
  await new Promise((resolve) => setTimeout(resolve, 20));

  const grid = elements.get('#download-grid');
  assert.equal(grid.children.length, 4, 'release page should show four platform downloads');
  const rows = grid.children.map((row) => ({
    text: collectText(row),
    hrefs: row.children.flatMap((node) => [node, ...(node.children || [])])
      .filter((node) => node.tagName === 'a')
      .map((node) => node.href),
  }));
  for (const label of ['Windows x64', 'Linux x64', 'macOS Intel (x64)', 'macOS Apple Silicon (arm64)']) {
    assert(rows.some((row) => row.text.includes(label)), `missing platform download: ${label}`);
  }
  for (const file of ['local-ai-runtime-windows-x64-setup.exe', 'local-ai-runtime-linux-x64.tar.gz', 'local-ai-runtime-macos-x64.tar.gz', 'local-ai-runtime-macos-arm64.tar.gz']) {
    assert(rows.some((row) => row.hrefs.some((href) => href.endsWith(`/${file}`))), `missing asset link: ${file}`);
  }
  const windowsRow = rows.find((row) => row.text.includes('Windows x64'));
  assert(windowsRow.hrefs.some((href) => href.endsWith('/local-ai-runtime-windows-x64-setup.exe')), 'Windows must prefer the installer when a release contains it');
  assert.equal(elements.get('#latest-version').textContent, 'v0.1.0-alpha');
  assert.equal(elements.get('#site-updated').textContent, 'Project data · live');
  assert.equal(grid.afterChildren.length, 1, 'checksum link should be rendered below the platform downloads');
  assert(product.features.some((feature) => feature.name === 'Portable package launch (alpha)' && feature.status === 'preview'), 'product manifest should accurately describe the current portable launch flow');
  assert(product.features.some((feature) => feature.name === 'Windows desktop installer' && feature.status === 'preview'), 'product manifest should expose the CI-built Windows desktop installer as preview');
  assert(product.features.some((feature) => feature.name === 'Local device inventory' && feature.status === 'preview'), 'product manifest should expose local device inventory as preview');
  assert(product.features.some((feature) => feature.name === 'Hardware-aware model setup' && feature.status === 'planned'), 'product manifest must not claim recommendations are already available');
  assert(siteHtml.includes('From download to a local answer.'), 'site should explain a user-first first-run path');
  assert(siteHtml.includes('When the selected Windows release includes the desktop setup installer'), 'site should explain installer and portable fallback selection');
  assert(siteHtml.includes('Start-LocalAIRuntime.cmd') && siteHtml.includes('Start-LocalAIRuntime.command'), 'site should explain the platform launchers');
  assert(siteHtml.includes('Moving from the portable archive:'), 'site should explain migration from a portable package to the installed desktop app');
  assert(dashboardHtml.includes('id="check-updates"'), 'dashboard should expose a manual update check');
  assert(dashboardHtml.includes('id="device-panel"') && dashboardHtml.includes('Your device &amp; model fit'), 'dashboard should expose a local hardware inventory panel');
  assert(dashboardScript.includes('/v1/system/hardware') && dashboardScript.includes('GPU execution not verified'), 'dashboard should distinguish detected hardware from verified GPU execution');
  assert(dashboardScript.includes('renderHardwareRecommendations') && dashboardScript.includes('info.recommendations'), 'dashboard should render structured hardware-aware starter recommendations');
  assert(dashboardScript.includes('GPU candidate · not verified'), 'dashboard must label GPU recommendations as candidates, not supported profiles');
  assert(dashboardScript.includes('renderHardwareRecommendations') && dashboardScript.includes('info.recommendations'), 'dashboard must render the API recommendation cards');
  assert(dashboardHtml.includes('id="gpu-observation-run"') && dashboardHtml.includes('id="gpu-observation-status"'), 'dashboard should expose an opt-in GPU observation control with a visible result area');
  assert(dashboardScript.includes('/v1/diagnostics/gpu-observation') && dashboardScript.includes('ui.gpu_observation.completed'), 'GPU observation should run through the local API and record privacy-safe summary metrics');
  assert(windowsLauncher.includes('function Test-PortCanBind') && windowsLauncher.includes('launcher.port_owner_unresolved_but_bindable'), 'Windows launcher should reuse a configured port when a direct bind probe succeeds despite an unresolved listener owner');
  assert(dashboardHtml.includes('Chat with your model'), 'dashboard should present chat as a user task, not a developer test');
  assert(dashboardScript.includes('setInterval(() => { if (Date.now() - lastUpdateCheckAt > 6 * 60 * 60 * 1000) checkForUpdates(); }'), 'dashboard should refresh update metadata periodically');
  assert(collectText(grid.afterChildren[0]).includes('SHA256SUMS.txt'));
  process.stdout.write('Site smoke test passed: four platform assets, checksums, and user-first launch/update flow.\n');
}

main().catch((error) => {
  process.stderr.write(`${error.stack || error}\n`);
  process.exitCode = 1;
});
