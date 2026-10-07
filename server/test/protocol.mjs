// Protocol test of the Garibaldka server. Start the server first (npm run dev), then:  node test/protocol.mjs
//   WS_URL=ws://localhost:8787/ws   INVITE=<code>   (INVITE only if the server was started with INVITE_CODE)
const URL_ = process.env.WS_URL || "ws://localhost:8787/ws";
const INVITE = process.env.INVITE || "";
let failed = 0;
const check = (cond, msg) => { console.log((cond ? "ok:   " : "FAIL: ") + msg); if (!cond) failed++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const rnd = () => Math.random().toString(16).slice(2, 10).padEnd(8, "0");
const secretOf = () => rnd() + rnd() + rnd() + rnd();            // 32 hex chars
const suffix = rnd().slice(0, 4);                                  // makes the nicks unique for every run

class Client {
  constructor() { this.q = []; this.ws = new WebSocket(URL_); this.closed = false; }
  async open() {
    await new Promise((res, rej) => { this.ws.onopen = res; this.ws.onerror = () => rej(new Error("cannot connect to " + URL_)); });
    this.ws.onmessage = (e) => this.q.push(String(e.data));
    this.ws.onclose = () => { this.closed = true; };
    return this;
  }
  send(t) { this.ws.send(t); }
  async next(prefix, ms = 1500) {                                   // first queued message starting with `prefix`
    const t0 = Date.now();
    for (;;) {
      const i = this.q.findIndex((m) => m.startsWith(prefix));
      if (i >= 0) return this.q.splice(i, 1)[0];
      if (Date.now() - t0 > ms) return null;
      await sleep(20);
    }
  }
  async none(prefix, ms = 400) { await sleep(ms); return !this.q.some((m) => m.startsWith(prefix)); }
  close() { try { this.ws.close(); } catch { /* */ } }
}
const login = async (nick, secret) => {
  const c = await new Client().open();
  c.send(`#AUTH 1.2.1 ${nick} ${secret}${INVITE ? " " + INVITE : ""}`);
  return c;
};

const secA = secretOf(), secB = secretOf(), secOther = secretOf();
const nickA = "Ala" + suffix, nickB = "Bob" + suffix;

// ---- nicknames
let a = await login(nickA, secA);
check((await a.next("#OK"))?.startsWith("#OK " + nickA + " 0 0 0"), "a new nick is registered and logged in with an empty record");
const intruder = await login(nickA.toUpperCase(), secOther);
check((await intruder.next("#ERR taken")) !== null, "the same nick (any letter case) with another secret is refused: nicks are unique");
intruder.close();
const bad = await login("x y", secOther);
check((await bad.next("#ERR")) !== null, "a nick with a space / too short is refused");
bad.close();
const a2 = await login(nickA, secA);
check((await a2.next("#OK")) !== null, "the same player (same secret) can log in again");
check((await a.next("#KICK")) !== null, "...and the older connection of that nick is dropped");
a.close(); a = a2;
const b = await login(nickB, secB);
await b.next("#OK");

// ---- rooms and relay
b.send("#JOIN ZZZZ");
check((await b.next("#ERR noroom")) !== null, "joining a room that does not exist is refused");
a.send("#CREATE");
const room = (await a.next("#ROOM "))?.split(" ")[1];
check(/^[A-Z2-9]{4}$/.test(room || ""), "creating a room gives a 4-letter code: " + room);
b.send("#JOIN " + room.toLowerCase());
check((await a.next("#PAIRED " + nickB + " host")) !== null, "the host is told who joined (code is case-insensitive)");
check((await b.next("#PAIRED " + nickA + " guest")) !== null, "the guest is told who the host is");
check((await a.next("#H2H 0 0 0")) !== null && (await b.next("#H2H 0 0 0")) !== null, "both get their (empty) record against each other");
const third = await login("Cez" + suffix, secretOf());
await third.next("#OK");
third.send("#JOIN " + room);
check((await third.next("#ERR full")) !== null, "a third player cannot join a full room");
third.close();
a.send("M 3 12"); a.send("X 1c64bf8a1a784358");
check((await b.next("M 3 12")) !== null && (await b.next("X 1c64bf8a1a784358")) !== null, "game messages are relayed to the opponent unchanged and in order");
b.send("CHAT Cześć, gramy? ąęśćżź");
check((await a.next("CHAT Cześć, gramy? ąęśćżź")) !== null, "chat text with Polish letters is relayed intact");

// ---- results and statistics (counted only when both players agree)
a.send("#RESULT 1 W"); b.send("#RESULT 1 L");
const sa = await a.next("#STATS"), sb = await b.next("#STATS");
check(sa === "#STATS 1 0 0" && sb === "#STATS 0 1 0", "consistent results (W / L) are counted: " + sa + " / " + sb);
check((await a.next("#H2H 1 0 0")) !== null && (await b.next("#H2H 0 1 0")) !== null, "the record against the opponent is updated for both");
a.send("#RESULT 2 W"); b.send("#RESULT 2 W");
check(await a.none("#STATS") && await b.none("#STATS"), "contradicting reports (W / W) are NOT counted");
a.send("#RESULT 3 D"); b.send("#RESULT 3 D");
check((await a.next("#STATS 1 0 1")) !== null, "a draw is counted for both");
a.send("#RESULT 4 W"); b.send("#RESULT 4 L"); a.send("#RESULT 4 W"); // a duplicate report must not count twice
await sleep(300);
a.send("#STATS");
check((await a.next("#STATS 2 0 1")) !== null, "totals are persistent and not inflated by duplicate reports");

// ---- leaving
b.close();
check((await a.next("#PEERLEFT")) !== null, "when the opponent disconnects the other player is told");
a.send("#CREATE");
check((await a.next("#ROOM ")) !== null, "...and can open a new room");
a.close();

// ---- the record survives a new login
const again = await login(nickA, secA);
check((await again.next("#OK " + nickA + " 2 0 1")) !== null, "the record is kept after logging in again");
again.close();

console.log(failed ? `\n${failed} FAILED` : "\nall passed");
process.exit(failed ? 1 : 0);
