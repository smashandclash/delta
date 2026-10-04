# The Smash&Clash SDK bridge for the GBA

The Game Boy Advance has no network. Smash&Clash for the GBA plays online anyway, through the official SDK, [`@smashandclash/sdk`](https://www.npmjs.com/package/@smashandclash/sdk):

```
 the GBA game ──(a mailbox in its RAM)──▶ smashandclash.lua (in mGBA) ──(127.0.0.1:8765)──▶ bridge.mjs ──(@smashandclash/sdk)──▶ smashandclash.in
```

## Run it

You need [mGBA](https://mgba.io) 0.10 or later and [Node.js](https://nodejs.org) 18 or later.

```bash
npm install
node bridge.mjs          # listens on 127.0.0.1:8765
```

Then open `smashandclash.gba` in mGBA and load the script: **Tools → Scripting… → File → Load script… → `smashandclash.lua`**. The game's boot screen shows when both halves are up.

| Variable | Default | What it does |
| --- | --- | --- |
| `SNC_BRIDGE_PORT` | `8765` | the port the bridge listens on (change `PORT` in the Lua script to match) |
| `SNC_BRIDGE_HOST` | `127.0.0.1` | the address it listens on |
| `SNC_BASE_URL` | the SDK's default | another API deployment, for testing |

## What each part does

- **`smashandclash.lua`** finds the game's mailbox in RAM by its magic (`SNC-GBA-BRIDGE1`), beats its heartbeat every frame, sends each new request to the bridge, and writes the answer back where the game asked for it. It moves bytes and never reads the JSON.
- **`bridge.mjs`** makes each call with the SDK, named the way the SDK names it (`games.startHouse`, `game.play`, `game.waitForTurn`, …). It keeps the SDK's `Game` for each game, so a move is `game.play(move)`, the SDK's own; a game it has not seen yet is picked up with `sc.games.resume(id, playerToken)`. Every call runs on its own, so a resign never waits behind a long poll.

The wire format, both ways, is a header line and then exactly that many bytes of JSON:

```
REQ <seq> <length>\n{"op":"game.play","method":"POST","path":"/api/v1/games/g_…/moves","token":"pt_…","body":{"move":"Pengu@C2"}}
RES <seq> <status> <length>\n<what the SDK returned, or {"error","hint","code"}>
```

`method` and `path` name the API request each call stands for (the C core builds them for the DS and PSP, which make the calls over HTTPS themselves); the bridge goes by `op`. The mailbox's layout is in [docs/how-it-works.md](../../docs/how-it-works.md#the-gbas-sdk-bridge).

## The same idea, elsewhere

Anything that can reach a computer but not the internet (a microcontroller on USB serial, a calculator, a toy) can play the same way: hand each SDK call to a small script like this one.
