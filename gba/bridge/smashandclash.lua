-- Smash&Clash for the Game Boy Advance: the mGBA side of the SDK bridge.
--
-- The ROM keeps a mailbox in its RAM (it starts with "SNC-GBA-BRIDGE1"). Every frame this
-- script beats the mailbox's heartbeat, carries a new request to the SDK bridge
-- (bridge.mjs, on @smashandclash/sdk) over a local socket, and writes the answer back
-- where the ROM asked for it. It never reads the JSON: it moves bytes.
--
-- mGBA 0.10 or later: start the bridge (node bridge.mjs), open the game, then
-- Tools > Scripting... > File > Load script > smashandclash.lua

local HOST, PORT = "127.0.0.1", 8765
local EWRAM, EWRAM_SIZE = 0x02000000, 0x40000
local MAGIC = "SNC-GBA-BRIDGE1"
-- the mailbox: little-endian words after the 16-byte magic
local REQ_SEQ, REQ_LEN, RESP_SEQ, RESP_LEN, RESP_STATUS, BEAT, CONNECTED, RESP_CAP, RESP_ADDR, REQ = 16, 20, 24, 28, 32, 36, 40, 44, 48, 64
local REQ_CAP = 1024

local box = nil       -- the mailbox's address
local sock = nil
local sent = 0        -- the last request sent
local inbuf = ""
local frame = 0
local next_try = 0
local silent = 0      -- frames the socket said "readable" but gave nothing (a closed socket)

local function find_box()
  local at = string.find(emu:readRange(EWRAM, EWRAM_SIZE), MAGIC, 1, true)
  if at then
    box = EWRAM + at - 1
    local seq = emu:read32(box + REQ_SEQ)
    sent = (emu:read32(box + RESP_SEQ) == seq) and seq or -1  -- a request still unanswered goes out now
    console:log(string.format("Smash&Clash: found the game's mailbox at 0x%08X", box))
  end
end

local function connect()
  local s = socket.connect(HOST, PORT)
  if s then
    sock, inbuf, silent = s, "", 0
    console:log("Smash&Clash: connected to the SDK bridge on " .. HOST .. ":" .. PORT)
  end
end

local function drop(why)
  if sock then console:warn("Smash&Clash: lost the SDK bridge (" .. why .. ")") end
  sock = nil
  sent = box and emu:read32(box + REQ_SEQ) or 0  -- a request in flight is lost: the ROM times out and tries again
end

-- An answer for the request the game is waiting on: the bytes, then their length and
-- status, then the sequence number last (the ROM watches that).
local function deliver(seq, status, body)
  if not box or seq ~= emu:read32(box + REQ_SEQ) then return end  -- nobody waits for it any more
  local addr, cap = emu:read32(box + RESP_ADDR), emu:read32(box + RESP_CAP)
  if #body <= cap then
    for i = 1, #body do emu:write8(addr + i - 1, string.byte(body, i)) end
  end
  emu:write32(box + RESP_LEN, #body)  -- more than cap: the ROM says it was too big
  emu:write32(box + RESP_STATUS, status)
  emu:write32(box + RESP_SEQ, seq)
end

local function pump()
  local readable = sock:hasdata()
  local got = false
  while sock and sock:hasdata() do
    local chunk = sock:receive(4096)
    if not chunk or #chunk == 0 then break end
    inbuf, got = inbuf .. chunk, true
  end
  if readable and not got then
    silent = silent + 1
    if silent > 30 then drop("closed") return end
  else
    silent = 0
  end
  while true do
    local nl = string.find(inbuf, "\n", 1, true)
    if not nl then return end
    local seq, status, len = string.match(string.sub(inbuf, 1, nl - 1), "^RES (%d+) (%d+) (%d+)$")
    if not seq then
      inbuf = string.sub(inbuf, nl + 1)  -- not ours: skip the line
    else
      len = tonumber(len)
      if #inbuf < nl + len then return end  -- the rest is on its way
      deliver(tonumber(seq), tonumber(status), string.sub(inbuf, nl + 1, nl + len))
      inbuf = string.sub(inbuf, nl + 1 + len)
    end
  end
end

callbacks:add("frame", function()
  frame = frame + 1
  if not box or emu:readRange(box, #MAGIC) ~= MAGIC then  -- not found yet, or the game restarted
    box = nil
    if frame % 30 == 0 then find_box() end
    if not box then return end
  end
  emu:write32(box + BEAT, frame)
  if not sock and frame >= next_try then
    next_try = frame + 120
    connect()
  end
  emu:write32(box + CONNECTED, sock and 1 or 0)
  if not sock then return end
  local seq = emu:read32(box + REQ_SEQ)
  if seq ~= sent then
    local len = math.min(emu:read32(box + REQ_LEN), REQ_CAP)
    local req = emu:readRange(box + REQ, len)
    local ok = sock:send(string.format("REQ %d %d\n", seq, #req) .. req)
    if not ok then drop("send failed") return end
    sent = seq
  end
  local ok, err = pcall(pump)
  if not ok then drop(tostring(err)) end
end)

console:log("Smash&Clash: bridge script loaded. Waiting for the game and the SDK bridge (node bridge.mjs).")
