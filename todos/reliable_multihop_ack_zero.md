# TODO: Reliable/group multi-hop delivery is ~0% at scale

Status: ✅ DONE for reliable unicast (2026-09-23, commit `54cf983`): 25-node
reliable PDR 0/14 → 14/14. Root cause and fix:
`docs/design/reliable_delivery_at_scale.md`. Reliable GROUP ACK completeness
(~28% at 25 nodes) remains open as TODO-016.
## Symptom
In the 25-node mixed-traffic stress test (`test_network_stress`), reliable-unicast
and group-ACK delivery over MULTIPLE hops is ~0% — and this holds **even under
flat allocation** (allocation held constant), so it is NOT caused by data-slot
allocation. Local 1-hop non-reliable delivery works (75–88%).

## Evidence
- 10-node flat base=2: reliable PDR 50% (marginal), group 56%.
- 25-node flat base=2: reliable 0%, group ~30%.
- Superframe is very large at scale: 62,500 ms at 25 nodes (frame=125 slots).
- The reliable-pending leak assert fires → reliable messages stay PENDING (never
  resolve) rather than cleanly failing → ACKs are not returning.

## Hypotheses to investigate (in order)
1. **ACK-return path**: the ACK is a routed DataMessage back to the origin. At 6
   hops with a 62.5 s superframe, does the return route exist / stay valid, and
   does the ACK get forwarded each hop? Check `ReliableMessaging` ACK send +
   `ForwardDataMessage` for the ACK, and whether the origin route is present at
   ACK time.
2. **Reliable timeout sizing**: `ComputeReliableTimeout = (2*hops+1)*superframe`.
   At 6 hops × 62.5 s that is ~13× 62.5 s ≈ 812 s; with the test's
   `timeout_override = superframe*3` (~187 s) it may expire long before a 6-hop
   round trip completes. Verify the override vs the natural formula; the test may
   be under-timing reliable sends.
3. **Group window** (`window_ms`) similarly may be far too short for 6-hop flood +
   ACK collection at 62.5 s frames.
4. **Routing convergence at 25 nodes**: routing did not fully stabilize at 25
   nodes in earlier runs; multi-hop routes to distant destinations may be missing
   or flapping when the reliable send happens.

## Where to look
- `src/protocols/lora_mesh/services/reliable_messaging.{hpp,cpp}` (ACK send,
  ComputeReliableTimeout, group window)
- `src/protocols/reliability/reliable_delivery.{hpp,cpp}` (retry/timeout state)
- `src/protocols/lora_mesh/services/network_service.cpp` (`ForwardDataMessage`,
  route lookup for the ACK return)
- Test: `test/protocols/lora_mesh/services/test_network_stress/` (reliable/group
  send cadence + timeout_override); currently the stress test does NOT gate on
  reliable/group PDR for this reason.

## Related
See `docs/design/load_aware_slot_allocation.md` (§ "Known-separate issue").
