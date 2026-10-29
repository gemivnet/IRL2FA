# Formal verification of the pairing protocol

The IR pairing protocol ([pairing-protocol.md](pairing-protocol.md)) has a
symbolic model in [`formal/pairing.spthy`](../formal/pairing.spthy), proved
with the [Tamarin prover](https://tamarin-prover.com) on every push by
[`.github/workflows/formal.yml`](../.github/workflows/formal.yml).

The model proves that an active attacker on the IR link learns a contact key
only if the two six-symbol strings collide or one of the two devices is
compromised. It also proves that the attacker has to commit to its key for
each device before it can see that device's key. The 1 in 15,625 bound
follows from that ordering by a short probabilistic argument (below). Tamarin
cannot reason about probabilities, so this last step is argued by hand, not
machine-checked.

## Running it

```sh
formal/run.sh                 # Docker; about 2 minutes on 4 cores
NO_DOCKER=1 formal/run.sh     # tamarin-prover 1.12.0 and maude 3.5.1 on PATH
TIMEOUT=600 formal/run.sh     # seconds allowed per prover run (default 300)
```

The image is built from [`formal/Dockerfile`](../formal/Dockerfile): Ubuntu
24.04 pinned by digest, with the Tamarin 1.12.0 and Maude 3.5.1 release
binaries pinned by SHA-256. `run.sh` runs the model and the three broken
variants as four parallel prover runs, and fails unless:

- every lemma of the model is verified, and the model has exactly the lemmas
  `run.sh` lists (so a lemma cannot be dropped or renamed unnoticed);
- each deliberately broken variant (see [below](#broken-variants)) still
  completes an honest pairing, and the property that the removed check
  protects is falsified;
- no prover run exceeds the timeout, and Tamarin's wellformedness checks pass.

The full prover output lands in `formal/build/` (ignored by git; CI uploads
it as the `tamarin-output` artifact). To look at a proof or an attack trace
in Tamarin's web UI:

```sh
docker run --rm -it -p 3001:3001 -v "$PWD/formal:/w" -w /w \
    irl2fa-tamarin:1.12.0-maude3.5.1 \
    tamarin-prover interactive --interface='*4' . # then open http://localhost:3001
```

(The image has no Graphviz, so the web UI draws graphs only if you add it.)

## The model

### Attacker

A Dolev-Yao attacker **is** the IR link. Every frame a device sends goes to
the attacker. Every frame a device receives comes from the attacker, who can
drop, delay, reorder, replay and inject frames, and can run any number of
sessions with any device at the same time. Retransmission, CRC framing and
frame loss therefore need no modelling: the attacker can already do anything
a lossy, noisy link can. The attacker can also compromise a session, which
reveals its X25519 secret, or a stored contact, which reveals its key. The
security lemmas exclude the devices involved from compromise.

### Cryptography

| C code | Model | Assumption |
|-|-|-|
| X25519 | Tamarin's `diffie-hellman` builtin: `pk = 'g'^sk`, `shared = pkp^sk` | CDH is hard; a public key the attacker sends can be any term |
| SHA-256 commitment | `h/1`, a free function with no inverse | binding and hiding (SHA-256 as a random oracle) |
| HKDF-SHA256, 40-byte okm | `hkdf/3` (salt, ikm, info) | HKDF is a PRF/random oracle |
| `okm[0..31]`, `okm[32..35]` | `keypart/1`, `saspart/1` | independent outputs |
| first 8 bytes of a commitment | `trunc/1` | none; used only to address a REVEAL (see [Not proven](#what-is-not-proven)) |

Constant strings keep their meaning with underscores: `'IRL2FA_commit_v1'`,
`'IRL2FA_pair_v1'` and `'key_sas'` stand for `"IRL2FA commit v1"`,
`"IRL2FA pair v1"` and `"key+sas\0"`.

### Mapping to the code

References are to [`firmware/core/pairing.c`](../firmware/core/pairing.c)
and [`firmware/core/app.c`](../firmware/core/app.c) at commit `e2baf33`.

| Rule | Code | What it models |
|-|-|-|
| `Pair_Start` | `pairing_start` (pairing.c 43-51), `make_commit` (32-41), `start_pairing` (app.c 600-610) | fresh 32-byte secret `~sk` and 16-byte nonce `~n` from the TRNG; `pk = X25519_base(sk)`; `c = SHA-256("IRL2FA commit v1" ‖ pk ‖ n)`; state SEARCHING; `pairing_tx` sends `COMMIT(c)` |
| `Recv_Commit` | `handle_frame`, `T_COMMIT` (94-102); `pairing_tx` (REVEALING case) | ignores its own commitment (`NotEq(cp, c)`, line 95); in SEARCHING takes the **first** other COMMIT as `peer_commit` and moves to REVEALING (96-99); from then on `pairing_tx` sends `REVEAL(pk, n, trunc(peer_commit))`. A COMMIT in any other state is ignored: there is no rule for it |
| `Recv_Reveal_Low`, `Recv_Reveal_High` | `handle_frame`, `T_REVEAL` (103-118), `finish` (58-87) | the REVEAL must carry `trunc(c)`, i.e. be addressed to this device's commitment (108); the state must be REVEALING (109), so the device already holds the peer's COMMIT; it must not be its own echo (`NotEq(pkp, pk)`, 104); `make_commit(pkp, np)` must equal `peer_commit` (110-112; the model pattern-matches `peer_commit = h(<label, pkp, np>)`). Then `shared = X25519(sk, pkp)` (61), role from `memcmp` (64), `okm = HKDF(salt, shared, pk_low ‖ pk_high ‖ "key+sas\0")` (65-73), `key = okm[0..31]` (74), SAS from `okm[32..35]` (76-82), state DONE (86) |
| `Recv_Reveal_Bad` | 112-114 | a REVEAL addressed to us that does not open `peer_commit` ends the session in FAILED |
| restriction `Order_antisymmetric` | `memcmp(pub, peer_pub, 32) < 0` (64) | two rules, one per role, each logging `Order(pk_low, pk_high)`. The restriction makes the order antisymmetric and irreflexive, which is all the protocol needs: two devices with the same pair of keys agree on which is low. Transitivity is left out, which only allows more traces |
| `Humans_Match`, `Humans_Collision` | `draw_sas` (app.c 636-650), the people | both people look at both screens. That is an authentic out-of-band channel: the attacker can choose which two sessions are side by side, but cannot change what a screen shows. `Shown(tid, sas)` is a linear fact, so each session's SAS is compared at most once |
| `Accept` | `SCR_SAS`, centre button (app.c 1088-1090), `T_CONTACT_NAME` (819-833) | the owner presses ● ("they match"); the key and role are stored as a contact. Pressing ← ("they differ") or cancelling simply never fires `Accept` |
| `Compromise_Session`, `Compromise_Contact` | (not in the code) | an attacker who reads a device's memory during pairing or its stored contact key |

Things in the code that the model leaves out on purpose:

- **Retransmission and `DONE`** (`pairing_tx`, both COMMIT and REVEAL are
  resent forever, even after DONE). Each value is sent once in the model; the
  attacker remembers it and can replay it any number of times.
- **Frames, CRC, length checks** (`pairing_rx`, `frame`). Only the attacker
  can corrupt a frame, and the CRC is not a security mechanism.
- **Wiping** (`crypto_wipe` in `finish`, `pairing_wipe`). Modelled only
  through the compromise rules being excluded from the lemmas.

### The weak SAS

In a symbolic model, `saspart(x) = saspart(y)` only when `x = y`: the SAS
would act as a 256-bit value and no man in the middle could ever pass the
comparison. That idealisation would hide exactly the attack the commitments
are there to stop. So the model has two ways for the people to accept:

- `Humans_Match`: the two SAS values are equal (genuinely the same key
  material), logged as `Match(tA, tB)`.
- `Humans_Collision`: the two SAS values are **different** terms but the
  attacker wins anyway, as if the six symbols happened to agree, logged as
  `Collision(tA, tB, sA, sB)`.

`Humans_Collision` over-approximates: the attacker wins every comparison it
chooses to. All the lemmas still hold, so they hold for the real, much weaker,
attacker. The probability that a `Collision` actually happens is then a
separate, quantitative question, answered from the ordering facts that
`commit_before_reveal` and `collision_inputs_fixed` prove.

## What is proven

All lemmas are proved for an unbounded number of sessions and devices.

| Lemma | Kind | Statement |
|-|-|-|
| `executable_honest_pairing` | exists-trace | two honest devices complete a pairing, the SAS matches, and both owners accept the same key; no compromise, no collision. The model is not vacuous. |
| `executable_mitm_with_collision` | exists-trace | with a SAS collision, a man in the middle ends up knowing a key an owner accepted. The attack the SAS has to stop really exists in the model. |
| `key_secrecy` | all-traces | **(a)** if A's owner accepts key `k` after a genuine SAS match with session B, and neither A nor B is compromised, the attacker never knows `k`. |
| `mitm_requires_sas_collision` | all-traces | if the attacker knows a key that A's owner accepted, then A's comparison was a SAS collision, or A was compromised, or the session A matched was compromised. |
| `injective_agreement` | all-traces | **(b)** if A's owner accepts `k` with public keys `(pkA, pkB)` and the comparison was not a collision, there is a session B ≠ A that finished with the same `k` and the same keys in mirror order `(pkB, pkA)`, and no third session holds `k`. |
| `one_comparison_per_session` | all-traces | every session takes part in at most one comparison, so each collision attempt costs one comparison by people. |
| `commit_before_reveal` | all-traces | **(c)** if an uncompromised session with public key `pk` finishes with peer key `pkp`, then: the commitment `cp` it holds equals `h("IRL2FA commit v1", pkp, np)` for some `np`; the attacker already knew `cp` before the device took it; and the device took `cp` before the attacker could know `pk`. |
| `collision_inputs_fixed` | all-traces | **(c)** for every SAS collision, the same holds for the session on each side. The `Collision` event is logged in both directions, so the lemma covers both devices. |

**(b) and the other owner.** "B finished with the same key" is the strongest
agreement the protocol can give: each owner presses ● on their own, so A's
owner accepting does not mean B's owner did. If B's owner presses ←, A holds
a contact that B does not; that is a usability problem, not a security one.

### Broken variants

`pairing.spthy` contains three deliberately broken variants, switched on by
preprocessor flags. For each one `run.sh` checks that an honest pairing still
completes and that the expected lemma is **falsified**: Tamarin finds the
attack.

| Flag | What is broken | Expected to fail |
|-|-|-|
| `NO_COMMIT_CHECK` | the device accepts a REVEAL without checking it against `peer_commit` (as if lines 110-115 were removed) | `commit_before_reveal`, `collision_inputs_fixed`: the attacker picks its key after seeing the device's key |
| `REVEAL_EARLY` | the device sends its key and nonce while still SEARCHING, before it holds the peer's COMMIT | `commit_before_reveal`, `collision_inputs_fixed`: the attacker sees the device's key before it commits |
| `NO_SAS_CHECK` | the owner presses ● without comparing | `mitm_requires_sas_collision`: a plain man in the middle wins (proved with Tamarin's `--heuristic=c`; the default heuristic does not find this attack within minutes) |

In both of the first two variants, the attacker may choose its key after it
knows the device's public key. It can then grind: try keys offline until the
two SAS values agree, about 15,625 tries on average and well under a second
of computation. That is the attack the commitment exists to prevent.

## From the ordering to 1 in 15,625

Take a man in the middle that runs session A' with device A and session B'
with device B. A's owner sees `SAS_A = SAS(HKDF(X25519(a, m₁), pkA, m₁-key))`,
and B's owner sees `SAS_B = SAS(HKDF(X25519(b, m₂), pkB, m₂-key))`, where `m₁`
and `m₂` are the attacker's keys. (If the attacker relays a real device's
key unchanged it does not learn that side's shared secret; secrecy then holds
as in `key_secrecy`.)

`collision_inputs_fixed` gives, for each side: the attacker's key `m₁` is fixed
by a commitment that the attacker sent, and A accepted, before the attacker
could know `pkA`, and A checks the later reveal against it. With SHA-256
binding, the attacker cannot open that commitment to a different key later.
So when `pkA` is revealed, every input to `SAS_A` except the fresh `pkA` (and
the secret `a`) is already fixed. The same holds for B, `m₂` and `pkB`.

The attacker can choose the order. Say A finishes first, so the attacker
knows `SAS_A` exactly. It may then pick `m₂` with full knowledge of `SAS_A`,
but `m₂` is fixed before `pkB` is known. `pkB` is a fresh X25519 key from B's
TRNG, so `HKDF(X25519(b, m₂), pkB ‖ ...)` is a random-oracle (or PRF) output on
a fresh input. Its SAS digits are uniform and independent of everything the
attacker chose. Hence:

```
Pr[SAS_B = SAS_A] ≤ max over v of Pr[SAS_B = v] = ⌈2³² / 15625⌉ / 2³² ≈ 1.0000003 / 15,625
```

The 0.00003 % excess comes from reducing a 32-bit value mod 5⁶
(2³² mod 15,625 = 14,171), which is negligible. The same argument applies
symmetrically if B finishes first, or if the two sessions interleave.

Every attempt needs a fresh session on at least one device, since a session
cannot be reopened (`one_comparison_per_session`, linear state). A session is
started only by a person choosing *Pair* (or ● after *Pairing failed*). So an
attacker who gets `t` attempts succeeds with probability at most about
`t / 15,625`, on top of the negligible probabilities of breaking SHA-256,
HKDF or X25519.

The assumptions behind this: SHA-256 commitments are binding and hiding (in
the random-oracle model; the 16-byte nonce makes hiding hold even for a
low-entropy input, though `pk` already has ~252 bits), HKDF-SHA256 is a PRF
or random oracle, X25519 CDH is hard, and both TRNG outputs are fresh and
uniform.

## What is not proven

- **The 1 in 15,625 bound itself.** It is argued above, not mechanised.
  Tamarin proves the ordering facts it rests on.
- **Computational soundness.** This is a symbolic (Dolev-Yao) proof: hash,
  HKDF and DH are perfect black boxes. It says nothing about weaknesses in
  SHA-256, HKDF or X25519, or about how the model's terms map to bytes.
- **8-byte truncation of the address.** `trunc()` is a free function, so
  two different commitments never get the same address in the model. In C,
  only 8 bytes are compared. The address only decides whether a device
  *looks at* a REVEAL; the full 32-byte commitment check is what the security
  depends on, and a collision on the 8-byte address can at worst make a
  device check, and reject, a REVEAL meant for another device (FAILED: a denial
  of service the attacker can cause anyway).
- **X25519 edge cases.** Every attacker key is a DH term. In reality the
  attacker can send a low-order point (Monocypher's `crypto_x25519` then
  returns an all-zero shared secret) or a non-canonical encoding of a valid
  point. Neither helps: the attacker already knows the shared secret of every
  session where it supplied its own key, and the SAS still depends on the
  honest device's fresh public key through the HKDF info string.
- **The physical layer.** IR range, reflections, a long-range IR transmitter,
  timing, jamming. A denial of service is always possible.
- **Side channels.** Timing, power and EM leakage of the X25519 and SHA-256
  implementations, or of `memcmp` on the 8-byte address (not secret).
- **Human error.** The model assumes the two people compare all six symbols
  correctly, compare the two devices they are actually holding, and press ←
  on a mismatch. A careless comparison or a swapped device breaks this.
- **The rest of the firmware.** Contact words, key storage, PIN, UI flow
  beyond `SCR_SAS`. Only the pairing exchange is modelled.
- **Transitivity of the key order.** Not needed by any lemma; leaving it out
  only adds traces.

## Findings

No flaw was found in the cryptographic core of `pairing.c`: the
commit-before-reveal order holds on both sides, and the commitment, the
addressing and the key derivation match the documented design.

One point weakens a claim in [pairing-protocol.md](pairing-protocol.md):
"every attempt is visible as a failed pairing".

**A man in the middle can abort silently.** Once the attacker has finished
with one device (say A, now showing its SAS) and received B's REVEAL, it
knows both SAS values before anyone compares them. If they differ, it simply
never sends its REVEAL to B. B then stays on *Found it. Exchanging keys...*
indefinitely: REVEALING has no timeout, and pairing traffic keeps the device
awake. People see a pairing that hangs, which looks like an IR glitch, not a
mismatched SAS. If B's owner cancels and retries while A's owner keeps the
SAS screen open, every retry is a fresh 1 in 15,625 chance against the same
`SAS_A`. The per-attempt bound holds. But the people see failed attempts as
"it hung, try again", not as a sign of an attack, so the number of attempts
the attacker gets is limited only by their patience.

Suggested fix (firmware and UI, no protocol change):

1. Give REVEALING a timeout of a few seconds after the peer's COMMIT, and
   show it as **Pairing failed** (`PAIR_FAILED`), not a silent hang.
2. When either device fails, make both start over. For example, the SAS
   screen could say "If the other device did not show symbols, press ← and
   start again on both"; a device in DONE should not stay on the SAS screen
   while the other one restarts.
3. Optionally count failed or abandoned pairings since the last success and
   warn after two or three ("Pairing keeps failing. Someone nearby may be
   interfering."), and say so in the user docs.
