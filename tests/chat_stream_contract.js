#!/usr/bin/env node
'use strict';

// Exercise the actual SSE parser functions from the dashboard without a
// browser, runtime process, network access, or model installation.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const root = path.resolve(__dirname, '..');
const appSource = fs.readFileSync(path.join(root, 'web/app.js'), 'utf8');
const start = appSource.indexOf('function consumeSseBlock(');
const end = appSource.indexOf('\nasync function parseChatError', start);
assert.ok(start >= 0 && end > start, 'chat SSE parser functions must remain available for contract tests');

const sandbox = {
  TextDecoder,
  Uint8Array,
  translation: (key, fallback) => fallback || key,
};
vm.createContext(sandbox);
vm.runInContext(
  `${appSource.slice(start, end)}\nglobalThis.chatStreamTest = { consumeSseBlock, readChatStream };`,
  sandbox,
  { filename: 'web/app.js#chat-stream' },
);

function makeResponse(chunks) {
  const encoded = chunks.map((chunk) => typeof chunk === 'string' ? new TextEncoder().encode(chunk) : chunk);
  let index = 0;
  return {
    body: {
      getReader() {
        return {
          async read() {
            if (index < encoded.length) return { value: encoded[index++], done: false };
            return { value: undefined, done: true };
          },
        };
      },
    },
  };
}

async function main() {
  const parser = sandbox.chatStreamTest;
  const received = [];
  const ordinaryStream = [
    'data: {"choices":[{"delta":{"role":"assistant"}}]}\r\n\r\n',
    'data: {"choices":[{"delta":{"content":"Merhaba "}}]}\r\n\r\n',
    'data: {"choices":[{"delta":{"content":"dünya!"}}]}\r\n\r\n',
    'data: [DONE]\r\n\r\n',
  ].join('');
  const ordinaryBytes = new TextEncoder().encode(ordinaryStream);
  const ordinaryResult = await parser.readChatStream(makeResponse([
    ordinaryBytes.slice(0, 23),
    ordinaryBytes.slice(23, 79),
    ordinaryBytes.slice(79),
  ]), (text) => received.push(text));
  assert.equal(received.join(''), 'Merhaba dünya!');
  assert.equal(ordinaryResult.reasoningChars, 0);

  const hiddenReasoning = [];
  const reasoningText = 'private model reasoning that must not be shown';
  const reasoningStream = [
    `data: ${JSON.stringify({ choices: [{ delta: { reasoning_content: reasoningText } }] })}\n\n`,
    'data: [DONE]\n\n',
  ];
  const reasoningResult = await parser.readChatStream(makeResponse(reasoningStream), (text) => hiddenReasoning.push(text));
  assert.equal(hiddenReasoning.join(''), '', 'reasoning_content must never be rendered as the user-facing answer');
  assert.equal(reasoningResult.reasoningChars, reasoningText.length, 'reasoning presence should be tracked without retaining its content');

  const mixedContent = [];
  const mixedResult = await parser.readChatStream(makeResponse([
    `data: ${JSON.stringify({ choices: [{ delta: { reasoning_content: 'internal' } }] })}\n\n`,
    `data: ${JSON.stringify({ choices: [{ delta: { content: 'Final answer.' } }] })}\n\n`,
    'data: [DONE]\n\n',
  ]), (text) => mixedContent.push(text));
  assert.equal(mixedContent.join(''), 'Final answer.');
  assert.equal(mixedResult.reasoningChars, 'internal'.length);

  const reasoningAlias = [];
  const aliasResult = await parser.readChatStream(makeResponse([
    `data: ${JSON.stringify({ choices: [{ delta: { reasoning: 'internal only' } }] })}\n\n`,
    'data: [DONE]\n\n',
  ]), (text) => reasoningAlias.push(text));
  assert.equal(reasoningAlias.length, 0);
  assert.equal(aliasResult.reasoningChars, 'internal only'.length);

  await assert.rejects(
    parser.readChatStream(makeResponse(['data: {"choices":[]}\n\n']), () => {}),
    /stream ended before completion/i,
    'an incomplete stream must not be treated as successful',
  );
  await assert.rejects(
    parser.readChatStream(makeResponse(['data: {broken json}\n\n', 'data: [DONE]\n\n']), () => {}),
    /invalid streaming response/i,
  );

  assert.ok(appSource.includes('chat_template_kwargs: { enable_thinking: false }'),
    'normal Chat should explicitly disable Qwen-style thinking mode at request level');
  assert.ok(appSource.includes('if (!assistantMessage.content.trim())'),
    'an empty visible answer must fail before the request is marked successful');
  assert.ok(appSource.includes('reasoning_chars=${reasoningChars}'),
    'failure diagnostics should record reasoning-channel length without storing content');
  assert.ok(!appSource.includes('appendAssistantDelta(conversation, assistantMessage, reasoning)'),
    'private reasoning must not be appended to the visible assistant message');

  console.log('Chat streaming contract passed: visible answer, hidden reasoning, incomplete stream and empty-response handling.');
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
