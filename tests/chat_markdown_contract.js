#!/usr/bin/env node
'use strict';

// Exercise the assistant Markdown renderer without a browser, runtime process,
// network access, or model installation. The renderer must create safe DOM
// nodes and must never interpret model-provided HTML as markup.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

class MockNode {
  constructor(tagName, textContent = '') {
    this.tagName = tagName;
    this.textContent = textContent;
    this.children = [];
    this.className = '';
    this.href = '';
    this.target = '';
    this.rel = '';
  }
  append(...nodes) { this.children.push(...nodes); }
}

function collectText(node) {
  return `${node.textContent || ''}${(node.children || []).map(collectText).join('')}`;
}

function descendants(node) {
  return [node, ...(node.children || []).flatMap(descendants)];
}

const root = path.resolve(__dirname, '..');
const appSource = fs.readFileSync(path.join(root, 'web/app.js'), 'utf8');
const start = appSource.indexOf('function decodeChatMarkdownEntities(');
const end = appSource.indexOf('\nfunction renderChatMessages()', start);
assert.ok(start >= 0 && end > start, 'chat Markdown renderer must remain available for contract tests');

const document = {
  createDocumentFragment() { return new MockNode('#document-fragment'); },
  createElement(tagName) { return new MockNode(tagName); },
  createTextNode(text) { return new MockNode('#text', String(text)); },
};
const sandbox = { document, URL };
vm.createContext(sandbox);
vm.runInContext(
  `${appSource.slice(start, end)}\nglobalThis.chatMarkdownTest = { renderChatMarkdown };`,
  sandbox,
  { filename: 'web/app.js#chat-markdown' },
);

const render = sandbox.chatMarkdownTest.renderChatMarkdown;

const normalizedModelText = render('Merhaba, ben \\*\\*Qwen\\*\\*, bir yapay zekâ modeliyim.');
assert.equal(collectText(normalizedModelText), 'Merhaba, ben Qwen, bir yapay zekâ modeliyim.');
assert(descendants(normalizedModelText).some((node) => node.tagName === 'strong'),
  'paired escaped emphasis delimiters from local models should render as emphasis');

const formatted = render([
  '# C++ yapay zekâ projeleri',
  '',
  'C++ ile **yüksek performanslı** uygulamalar geliştirilebilir.',
  '',
  '- Makine öğrenimi',
  '- Bilgisayar görüşü',
  '',
  'Kod: `std::vector<int>`',
  '',
  '```cpp',
  'int main() { return 0; }',
  '```',
].join('\n'));
const tags = descendants(formatted).map((node) => String(node.tagName).toLowerCase());
assert(tags.includes('h2'), 'Markdown headings should render with semantic heading elements');
assert(tags.includes('strong'), 'bold Markdown should render');
assert(tags.includes('ul') && tags.filter((tag) => tag === 'li').length === 2, 'unordered lists should render');
assert(tags.includes('code') && tags.includes('pre'), 'inline and fenced code should render');
assert(collectText(formatted).includes('int main() { return 0; }'));

const entities = render('Başlık&#x20;\n\nA &amp; B, &lt;etiket&gt; ve &#x1F680;.');
assert.equal(collectText(entities), 'Başlık A & B, <etiket> ve 🚀.');
assert(!collectText(entities).includes('&#x20;'), 'numeric space entities should render as whitespace');
assert(!descendants(entities).some((node) => String(node.tagName).toLowerCase() === 'etiket'),
  'decoded HTML-like text must remain text and never become an element');

const encodedMarkdown = render('&#42;&#42;entity bold&#42;&#42;');
assert(descendants(encodedMarkdown).some((node) => node.tagName === 'strong'),
  'encoded Markdown delimiters should be decoded before safe Markdown tokenization');

const links = render('[Resmî kaynak](https://example.com/docs) and [unsafe](javascript:alert(1))');
const anchors = descendants(links).filter((node) => String(node.tagName).toLowerCase() === 'a');
assert.equal(anchors.length, 1, 'only a safe HTTPS link should become an anchor');
assert.equal(anchors[0].href, 'https://example.com/docs');
assert.equal(anchors[0].rel, 'noopener noreferrer');
assert(collectText(links).includes('unsafe'), 'an unsafe link target should degrade to plain label text');

const hostile = render('<img src=x onerror=alert(1)> <script>alert(1)</script>');
const hostileTags = descendants(hostile).map((node) => String(node.tagName).toLowerCase());
assert(!hostileTags.includes('img') && !hostileTags.includes('script'), 'raw model HTML must never become executable DOM');
assert(collectText(hostile).includes('<img src=x onerror=alert(1)>'), 'raw HTML should remain visible as escaped text');

const englishLocale = JSON.parse(fs.readFileSync(path.join(root, 'web/locales/en.json'), 'utf8'));
const turkishLocale = JSON.parse(fs.readFileSync(path.join(root, 'web/locales/tr.json'), 'utf8'));
assert(englishLocale['chat.systemPrompt'].includes('do not mix in unrelated words or writing systems'));
assert(englishLocale['chat.systemPrompt'].includes('inline code and code blocks'));
assert(turkishLocale['chat.systemPrompt'].includes('başka dil veya alfabelerden sözcükler karıştırma'));
assert(turkishLocale['chat.systemPrompt'].includes('satır içi kod ve kod bloğu'));

assert(appSource.includes("content.append(renderChatMarkdown(message.content))"),
  'completed assistant messages should use the safe Markdown renderer');
assert(appSource.includes("content.textContent = message.content ||"),
  'user messages and in-progress assistant streams should remain plain text');

console.log('Chat Markdown contract passed: escaped emphasis, paragraphs, headings, lists, code, safe links and raw-HTML isolation.');
