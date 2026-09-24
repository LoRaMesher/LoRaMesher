# TODO: Joiners retry in lockstep and collide (SF8+ formation can time out)

Status: 🔴 OPEN — analysed 2026-09-24, not changed yet (needs its own branch and a
full test sweep; the join sequence is shared by many tests). Priority: P1.

## Symptom
`LinkQualitySlicingTests.DenseMeshLinkQualityStable/SF11` fails network formation in
about 2 of 3 local runs and intermittently in CI ("Network formation failed at SF11").
No node is permanently stuck: the unjoined nodes keep cycling
JOINING → join timeout → FAULT_RECOVERY → DISCOVERY → JOINING, and formation runs out
the test's budget. In one failing run, 74 JOIN_REQUESTs were sent and 7 reached the NM.
The NM answered every request it received.

## Root cause
1. **One join opportunity per superframe, shared by every joiner.** Joiners transmit in
   the first discovery slot only (`SlotScheduler::SetJoiningSlots`). Inside it,
   requests are spread over random subslots (discovery subslot strategy `RANDOM`,
   5 configured), but only as many subslots are used as fit a whole transmission
   (`SubslotScheduler::ComputeTiming`). At SF8–SF12 a JOIN_REQUEST (≈578 ms at SF11 in a
   1450 ms slot) leaves room for only **2** subslots, so 6–7 joiners collide almost
   every time.
2. **The random backoff never runs.** `StartJoining` resets the retry state and sends
   immediately; the single retry goes out 2 superframes later; the join timeout is
   3 superframes (`GetJoinTimeout`). The backoff drawn after the retry (1–3 superframes)
   always outlasts the timeout.
3. **The timeout re-synchronises everyone.** A join timeout goes through FAULT_RECOVERY
   back to DISCOVERY, so all remaining joiners hear the same beacon and restart
   together, with their retry state reset.
4. The superframe grows as nodes join (23 s → 62 s at SF11), so late cycles take the
   longest, while the test budget is sized from the initial superframe.

## Proposed fix (not applied)
- Random initial offset (0..W superframes) before the first JOIN_REQUEST.
- Keep retry/backoff state across a join timeout, or stay in JOINING while the NM's
  beacons are still heard instead of going through FAULT_RECOVERY.
- Make the join timeout cover the backoff window, so exponential backoff actually
  spreads the joiners.
- Optionally let a joiner pick a random DISCOVERY slot (not always the first), keeping
  the response slots free.

## Test-first
A NetworkService-level test where several joiners start JOINING on the same beacon:
assert their JOIN_REQUESTs don't all go out in the same superframes, and that retry
state survives a join timeout. Then run the whole routing, NM-election, join and
role-change suites, since they all depend on join timing.
