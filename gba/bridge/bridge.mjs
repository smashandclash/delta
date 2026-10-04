#!/usr/bin/env node
// The Smash&Clash SDK bridge for the Game Boy Advance client.
//
// The GBA has no network. Its ROM asks for SDK calls through a mailbox in its RAM; the
// mGBA script (smashandclash.lua) carries each request here over a local socket, and this
// makes the call with the official SDK, @smashandclash/sdk, then sends back what the SDK
// returned (the API's JSON shapes, which the ROM reads with the same C code the DS and
// PSP clients use).
//
//   npm install && node bridge.mjs            (listens on 127.0.0.1:8765)
//
// Wire format (both ways): a header line, then exactly <length> bytes of JSON.
//   ROM -> SDK:  REQ <seq> <length>\n{"op":"game.play","method":"POST","path":"/api/v1/games/g_…/moves","token":"pt_…","body":{"move":"Pengu@C2"}}
//   SDK -> ROM:  RES <seq> <status> <length>\n<the JSON the SDK returned, or {error, hint, code}>

import net from 'node:net';
import { SmashAndClash, SmashAndClashError } from '@smashandclash/sdk';

const PORT = Number(process.env.SNC_BRIDGE_PORT || 8765);
const HOST = process.env.SNC_BRIDGE_HOST || '127.0.0.1';
// `client` names this app to the API (SDK 0.2.2+), after the SDK's own name in the X-SDK header
const sc = new SmashAndClash({ client: 'smashandclash-gba-bridge/1.0.0', ...(process.env.SNC_BASE_URL ? { baseUrl: process.env.SNC_BASE_URL } : {}) });
const games = new Map(); // game id -> the SDK's Game (it holds your seat's token)

const log = (...a) => console.log(new Date().toISOString().slice(11, 19), ...a);

// The game a request is about: the one we already hold, or resumed from its token.
async function gameFor(id, token) {
	const held = games.get(id);
	if (held && held.playerToken === token) return held;
	const g = await sc.games.resume(id, token);
	games.set(id, g);
	return g;
}

const started = (g) => {
	games.set(g.id, g);
	return { game: g.state, playerToken: g.playerToken, ...(g.inviteUrl ? { inviteUrl: g.inviteUrl } : {}) };
};

// One SDK call per request. `path` carries the game id and the query the call needs.
async function run(req) {
	const body = req.body ?? {};
	const url = new URL(req.path ?? '/', 'http://x');
	const id = decodeURIComponent(url.pathname.split('/')[4] ?? '');
	switch (req.op) {
		case 'games.startHouse':
			return started(await sc.games.startHouse({ name: body.name, ruleset: body.ruleset, strength: body.strength, as: body.as }));
		case 'games.quickMatch':
			return started(await sc.games.quickMatch({ name: body.name, ruleset: body.ruleset, as: body.as, opponent: body.opponent }));
		case 'games.createDuel':
			return started(await sc.games.createDuel({ name: body.name, ruleset: body.ruleset, as: body.as, opponent: body.opponent }));
		case 'games.joinDuel':
			return started(await sc.games.joinDuel(body.code, { name: body.name, as: body.as }));
		case 'games.resume': {
			const held = games.get(id);
			if (held && held.playerToken === req.token) return held.refresh();
			return (await gameFor(id, req.token)).state; // resume() has just fetched it
		}
		case 'game.play':
			return (await gameFor(id, req.token)).play(body.move);
		case 'game.waitForTurn':
			return (await gameFor(id, req.token)).waitForTurn(Number(url.searchParams.get('timeout') || 20));
		case 'game.resign':
			return (await gameFor(id, req.token)).resign();
		case 'game.sync':
			return (await gameFor(id, req.token)).sync({ since: Number(url.searchParams.get('since') || 0) });
		case 'games.review':
			return sc.games.review(id);
		case 'rules':
			return { rules: await sc.rules() };
		default:
			throw new SmashAndClashError(400, 'bad_request', `The bridge does not know the SDK call "${req.op}".`);
	}
}

function answer(sock, seq, status, data) {
	const json = Buffer.from(JSON.stringify(data), 'utf8');
	sock.write(`RES ${seq} ${status} ${json.length}\n`);
	sock.write(json);
}

const server = net.createServer((sock) => {
	log('mGBA connected');
	let buf = Buffer.alloc(0);
	sock.on('data', (chunk) => {
		buf = Buffer.concat([buf, chunk]);
		for (;;) {
			const nl = buf.indexOf(10);
			if (nl < 0) return;
			const m = /^REQ (\d+) (\d+)$/.exec(buf.subarray(0, nl).toString());
			if (!m) {
				buf = buf.subarray(nl + 1); // not ours: skip the line
				continue;
			}
			const len = Number(m[2]);
			if (buf.length < nl + 1 + len) return; // the rest is on its way
			const seq = Number(m[1]);
			const text = buf.subarray(nl + 1, nl + 1 + len).toString('utf8');
			buf = buf.subarray(nl + 1 + len);
			let req;
			try {
				req = JSON.parse(text);
			} catch {
				answer(sock, seq, 400, { error: 'The request did not parse.' });
				continue;
			}
			// every request runs on its own: a long wait never holds up a resign
			const t0 = Date.now();
			run(req).then(
				(data) => {
					log(`${req.op} ${data?.status ?? data?.game?.status ?? ''} (${Date.now() - t0} ms)`);
					answer(sock, seq, 200, data);
				},
				(e) => {
					const status = e instanceof SmashAndClashError ? e.status : 502;
					log(`${req.op} failed: ${e.message}`);
					answer(sock, seq, status, { error: e.message, ...(e.hint ? { hint: e.hint } : {}), code: e.code ?? 'error' });
				},
			);
		}
	});
	sock.on('close', () => log('mGBA disconnected'));
	sock.on('error', () => {});
});

server.listen(PORT, HOST, () => {
	log(`Smash&Clash SDK bridge on ${HOST}:${PORT}. In mGBA: Tools > Scripting > File > Load script > smashandclash.lua`);
});
