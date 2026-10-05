// Garibaldka online server (Cloudflare Workers + one Durable Object).
//
// The server knows nothing about the rules of the game. It
//   * keeps nicknames unique (a nick belongs to whoever registered it first, proven by a secret the game keeps),
//   * pairs two players in a room with a 4-letter code,
//   * relays every game message of one player to the other one unchanged,
//   * keeps the statistics of the meetings (a result counts only if BOTH players report the same outcome).
//
// Protocol: WebSocket text frames. A frame that starts with '#' is a command for/from the server,
// anything else is a game message and is passed to the opponent as it is.
//
// client -> server
//   #AUTH <version> <nick> <secret> [<invite>]   log in / register the nick
//   #CREATE                                      open a room (you are the host)
//   #JOIN <CODE>                                 join a room (you are the guest)
//   #LEAVE                                       leave the room
//   #RESULT <game> <W|L|D>                       outcome of game number <game> as seen by this player
//   #STATS                                       ask for own totals
//   #PING                                        keep-alive (answered by the platform with #PONG)
// server -> client
//   #OK <nick> <wins> <losses> <draws>           logged in
//   #ERR <code> <text>                           refused (code: invite, nick, taken, auth, noroom, full, busy)
//   #ROOM <CODE>                                 your room is open
//   #PAIRED <opponent nick> <host|guest>         the opponent is there (you may now exchange game messages)
//   #H2H <wins> <losses> <draws>                 your record against this opponent
//   #STATS <wins> <losses> <draws>               your totals (after a counted result)
//   #PEERLEFT                                    the opponent left / disconnected, the room is closed
//   #KICK <text>                                 you logged in somewhere else
import { DurableObject } from "cloudflare:workers";

const CODE_CHARS = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";   // no 0/O, 1/I
const NICK_RE = /^[\p{L}\p{N}_-]{3,16}$/u;
const MAX_PLAYERS = 500;                                   // a server for friends: stops a flood of registrations
const MAX_FRAME = 2000;

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.pathname === "/ws") {
      if (request.headers.get("Upgrade") !== "websocket") return new Response("WebSocket expected", { status: 426 });
      return env.HUB.get(env.HUB.idFromName("main")).fetch(request);
    }
    return new Response("Garibaldka server: OK\n", { headers: { "content-type": "text/plain; charset=utf-8" } });
  },
};

async function sha256Hex(text) {
  const d = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(text));
  return [...new Uint8Array(d)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

export class Hub extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env);
    this.sql = ctx.storage.sql;
    this.sql.exec(`CREATE TABLE IF NOT EXISTS players(
      nick_lc TEXT PRIMARY KEY, nick TEXT NOT NULL, secret_hash TEXT NOT NULL, created INTEGER NOT NULL,
      wins INTEGER NOT NULL DEFAULT 0, losses INTEGER NOT NULL DEFAULT 0, draws INTEGER NOT NULL DEFAULT 0)`);
    this.sql.exec(`CREATE TABLE IF NOT EXISTS h2h(
      a TEXT NOT NULL, b TEXT NOT NULL, a_wins INTEGER NOT NULL DEFAULT 0, b_wins INTEGER NOT NULL DEFAULT 0,
      draws INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(a, b))`);
    this.sql.exec(`CREATE TABLE IF NOT EXISTS pending(
      room TEXT NOT NULL, game INTEGER NOT NULL, nick_lc TEXT NOT NULL, res TEXT NOT NULL, PRIMARY KEY(room, game, nick_lc))`);
    // keep-alive answered without waking the object up
    ctx.setWebSocketAutoResponse(new WebSocketRequestResponsePair("#PING", "#PONG"));
  }

  async fetch(request) {
    const pair = new WebSocketPair();
    const [client, server] = Object.values(pair);
    this.ctx.acceptWebSocket(server);                       // hibernation API: the object may sleep between messages
    server.serializeAttachment({ nick: null, nickLc: null, room: null, role: null });
    return new Response(null, { status: 101, webSocket: client });
  }

  // ---------------------------------------------------------------- helpers
  send(ws, text) { try { ws.send(text); } catch { /* the socket is gone */ } }
  err(ws, code, text) { this.send(ws, `#ERR ${code} ${text}`); }
  att(ws) { return ws.deserializeAttachment() || {}; }
  members(room) {
    const out = [];
    for (const w of this.ctx.getWebSockets()) { const a = this.att(w); if (a.room === room) out.push({ ws: w, a }); }
    return out;
  }
  peerOf(ws, a) { return a.room ? this.members(a.room).find((m) => m.ws !== ws) : undefined; }
  totals(nickLc) {
    const r = this.sql.exec("SELECT wins, losses, draws FROM players WHERE nick_lc = ?", nickLc).toArray()[0];
    return r ? `${r.wins} ${r.losses} ${r.draws}` : "0 0 0";
  }
  h2h(meLc, otherLc) {
    const [a, b] = meLc < otherLc ? [meLc, otherLc] : [otherLc, meLc];
    const r = this.sql.exec("SELECT a_wins, b_wins, draws FROM h2h WHERE a = ? AND b = ?", a, b).toArray()[0];
    if (!r) return "0 0 0";
    return meLc === a ? `${r.a_wins} ${r.b_wins} ${r.draws}` : `${r.b_wins} ${r.a_wins} ${r.draws}`;
  }
  newCode() {
    for (let tries = 0; tries < 50; tries++) {
      let c = "";
      for (let i = 0; i < 4; i++) c += CODE_CHARS[crypto.getRandomValues(new Uint32Array(1))[0] % CODE_CHARS.length];
      if (this.members(c).length === 0) return c;
    }
    return null;
  }
  // the room is closed for everybody who is in it
  closeRoom(ws, notifyPeer) {
    const a = this.att(ws);
    if (!a.room) return;
    const room = a.room;
    for (const m of this.members(room)) {
      if (m.ws !== ws && notifyPeer) this.send(m.ws, "#PEERLEFT");
      m.a.room = null; m.a.role = null;
      try { m.ws.serializeAttachment(m.a); } catch { /* closed */ }
    }
    this.sql.exec("DELETE FROM pending WHERE room = ?", room);
  }

  // ---------------------------------------------------------------- events
  async webSocketMessage(ws, message) {
    if (typeof message !== "string" || message.length > MAX_FRAME) return;
    const a = this.att(ws);
    if (!message.startsWith("#")) {                          // a game message: straight to the opponent
      const peer = this.peerOf(ws, a);
      if (peer) this.send(peer.ws, message);
      return;
    }
    const parts = message.split(" ");
    const cmd = parts[0];
    const args = parts.slice(1);
    try {
      switch (cmd) {
        case "#AUTH": return await this.cmdAuth(ws, a, args);
        case "#CREATE": return this.cmdCreate(ws, a);
        case "#JOIN": return this.cmdJoin(ws, a, args);
        case "#LEAVE": return this.closeRoom(ws, true);
        case "#RESULT": return this.cmdResult(ws, a, args);
        case "#STATS": if (a.nickLc) this.send(ws, `#STATS ${this.totals(a.nickLc)}`); return;
        default: return;
      }
    } catch (e) {
      this.err(ws, "server", "Błąd serwera.");
    }
  }
  webSocketClose(ws) { this.closeRoom(ws, true); }
  webSocketError(ws) { this.closeRoom(ws, true); }

  // ---------------------------------------------------------------- commands
  async cmdAuth(ws, a, args) {
    const [, nick, secret, invite] = args;
    if (this.env.INVITE_CODE && invite !== this.env.INVITE_CODE) return this.err(ws, "invite", "Nieprawidłowe hasło serwera.");
    if (!nick || !NICK_RE.test(nick)) return this.err(ws, "nick", "Nick: 3-16 znaków, litery, cyfry, _ lub -.");
    if (!secret || secret.length < 16 || secret.length > 128) return this.err(ws, "auth", "Błędny klucz gracza.");
    const lc = nick.toLowerCase();
    const hash = await sha256Hex(`${secret}:${lc}`);
    let row = this.sql.exec("SELECT nick, secret_hash FROM players WHERE nick_lc = ?", lc).toArray()[0];
    if (!row) {
      const n = this.sql.exec("SELECT COUNT(*) AS n FROM players").toArray()[0].n;
      if (n >= MAX_PLAYERS) return this.err(ws, "full", "Serwer nie przyjmuje już nowych nicków.");
      this.sql.exec("INSERT INTO players(nick_lc, nick, secret_hash, created) VALUES(?, ?, ?, ?)", lc, nick, hash, Date.now());
      row = { nick, secret_hash: hash };
    } else if (row.secret_hash !== hash) {
      return this.err(ws, "taken", "Ten nick jest już zajęty przez innego gracza.");
    }
    for (const other of this.ctx.getWebSockets()) {           // one connection per nick: the older one is dropped
      if (other === ws) continue;
      const b = this.att(other);
      if (b.nickLc === lc) { this.send(other, "#KICK Zalogowano z innego miejsca."); this.closeRoom(other, true); try { other.close(1000, "replaced"); } catch { /* */ } }
    }
    this.closeRoom(ws, true);
    a.nick = row.nick; a.nickLc = lc; a.room = null; a.role = null;
    ws.serializeAttachment(a);
    this.send(ws, `#OK ${row.nick} ${this.totals(lc)}`);
  }

  cmdCreate(ws, a) {
    if (!a.nickLc) return this.err(ws, "auth", "Najpierw zaloguj się.");
    this.closeRoom(ws, true);
    const code = this.newCode();
    if (!code) return this.err(ws, "busy", "Brak wolnych pokoi, spróbuj za chwilę.");
    a.room = code; a.role = "host"; ws.serializeAttachment(a);
    this.send(ws, `#ROOM ${code}`);
  }

  cmdJoin(ws, a, args) {
    if (!a.nickLc) return this.err(ws, "auth", "Najpierw zaloguj się.");
    const code = (args[0] || "").toUpperCase();
    const mem = this.members(code);
    const host = mem.find((m) => m.a.role === "host");
    if (!code || !host) return this.err(ws, "noroom", "Nie ma takiego pokoju.");
    if (host.ws === ws) return;
    if (mem.length >= 2) return this.err(ws, "full", "Pokój jest już pełny.");
    this.closeRoom(ws, true);
    a.room = code; a.role = "guest"; ws.serializeAttachment(a);
    this.send(ws, `#PAIRED ${host.a.nick} guest`);
    this.send(host.ws, `#PAIRED ${a.nick} host`);
    this.send(ws, `#H2H ${this.h2h(a.nickLc, host.a.nickLc)}`);
    this.send(host.ws, `#H2H ${this.h2h(host.a.nickLc, a.nickLc)}`);
  }

  // A result counts only when both players of the room report the same outcome of the same game.
  cmdResult(ws, a, args) {
    if (!a.room || !a.nickLc) return;
    const game = parseInt(args[0], 10), res = args[1];
    if (!Number.isInteger(game) || !["W", "L", "D"].includes(res)) return;
    this.sql.exec("INSERT OR REPLACE INTO pending(room, game, nick_lc, res) VALUES(?, ?, ?, ?)", a.room, game, a.nickLc, res);
    const rows = this.sql.exec("SELECT nick_lc, res FROM pending WHERE room = ? AND game = ?", a.room, game).toArray();
    if (rows.length < 2) return;
    this.sql.exec("DELETE FROM pending WHERE room = ? AND game = ?", a.room, game);
    const mine = rows.find((r) => r.nick_lc === a.nickLc), theirs = rows.find((r) => r.nick_lc !== a.nickLc);
    if (!mine || !theirs) return;
    const consistent = (mine.res === "W" && theirs.res === "L") || (mine.res === "L" && theirs.res === "W") || (mine.res === "D" && theirs.res === "D");
    if (!consistent) return;                                // the two players disagree: nothing is counted
    const [x, y] = [mine, theirs];
    const bump = (lc, col) => this.sql.exec(`UPDATE players SET ${col} = ${col} + 1 WHERE nick_lc = ?`, lc);
    const lo = x.nick_lc < y.nick_lc ? x.nick_lc : y.nick_lc, hi = x.nick_lc < y.nick_lc ? y.nick_lc : x.nick_lc;
    this.sql.exec("INSERT OR IGNORE INTO h2h(a, b) VALUES(?, ?)", lo, hi);
    if (x.res === "D") {
      bump(x.nick_lc, "draws"); bump(y.nick_lc, "draws");
      this.sql.exec("UPDATE h2h SET draws = draws + 1 WHERE a = ? AND b = ?", lo, hi);
    } else {
      const win = x.res === "W" ? x : y, lose = x.res === "W" ? y : x;
      bump(win.nick_lc, "wins"); bump(lose.nick_lc, "losses");
      this.sql.exec(`UPDATE h2h SET ${win.nick_lc === lo ? "a_wins" : "b_wins"} = ${win.nick_lc === lo ? "a_wins" : "b_wins"} + 1 WHERE a = ? AND b = ?`, lo, hi);
    }
    for (const m of this.members(a.room)) {
      const other = m.a.nickLc === x.nick_lc ? y.nick_lc : x.nick_lc;
      this.send(m.ws, `#STATS ${this.totals(m.a.nickLc)}`);
      this.send(m.ws, `#H2H ${this.h2h(m.a.nickLc, other)}`);
    }
  }
}
