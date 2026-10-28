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
  const assets = [
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
  for (const file of assets.slice(0, 4).map((asset) => asset.name)) {
    assert(rows.some((row) => row.hrefs.some((href) => href.endsWith(`/${file}`))), `missing asset link: ${file}`);
  }
  assert.equal(elements.get('#latest-version').textContent, 'v0.1.0-alpha');
  assert.equal(elements.get('#site-updated').textContent, 'Product manifest · 0.1.0-alpha');
  assert.equal(grid.afterChildren.length, 1, 'checksum link should be rendered below the platform downloads');
  assert(collectText(grid.afterChildren[0]).includes('SHA256SUMS.txt'));
  process.stdout.write('Site smoke test passed: four platform assets, live version, and checksum link.\n');
}

main().catch((error) => {
  process.stderr.write(`${error.stack || error}\n`);
  process.exitCode = 1;
});
