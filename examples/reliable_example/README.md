# LoRaMesher Reliable Messaging Example

Shows how to send acknowledged (reliable) unicast messages and follow each one until it is delivered or fails.

## What This Example Does

1. **Starts** a mesh node (same Builder flow as `simple_example`)
2. **Registers** two callbacks:
   - `SetDataCallbackEx` — received messages with their metadata (source, sequence number, hops travelled)
   - `SetDeliveryCallback` — the outcome of each reliable send
3. **Sends** `"Reliable hello #N"` with `SendReliable()` to the next peer in the routing table, one message at a time (see [Traffic Budget](#traffic-budget))
4. **Prints** whether each message was delivered (with the round-trip time) or failed

Flash it on two or more boards.

## The Reliable API

```cpp
ReliableOptions options;
options.max_retries = 3;  // Retransmissions after the first attempt
options.timeout_ms = 0;   // 0 = derived from hop count and superframe length

LoraMesher::MessageId id = mesher->SendReliable(dest, payload, options);
if (id.source == 0) {
    // Rejected: not in a network, payload too large, or too many messages pending
}
```

`SendReliable()` returns at once. The destination acknowledges the message; the sender retransmits until it is acknowledged or the retries run out, and then reports the outcome:

```cpp
mesher->SetDeliveryCallback([](const LoraMesher::DeliveryResult& result) {
    // result.id      = the MessageId returned by SendReliable()
    // result.outcome = Outcome::Delivered or Outcome::Failed
    // result.by      = acknowledging node (Delivered)
    // result.rtt_ms  = round-trip time (Delivered)
});
```

A retransmitted message is delivered to the destination's application only once.

## Receiving With Metadata

`SetDataCallbackEx()` reports the same messages as `SetDataCallback()`, as one `ReceivedData`:

| Field | Meaning |
|-------|---------|
| `source` | Originating node |
| `dest` | This node, a group address or broadcast |
| `seq` | Sender's sequence number |
| `hops` | Hops travelled (1 = direct neighbour) |
| `payload` | Payload bytes, valid only during the callback |

Both callbacks run on the protocol task. Keep them short or hand the data to your own task (see `examples/queued_receive_example`).

## Traffic Budget

A fixed send interval that works for two nodes side by side breaks as the network grows. Keep these limits in mind:

- **Slots are shared.** Each node transmits in its own data slots, `GetDataSlotsPerSuperframe()` per superframe (2 by default). The same slots carry the node's own messages, the messages it forwards for others and the ACKs it returns. A superframe lasts several seconds and gets longer with more nodes and higher spreading factors.
- **A reliable message is not one packet.** Over `h` hops it costs about `h` data transmissions plus `h` ACK transmissions, and more for every retry. The nodes in the middle of the path pay for it too.
- **Overload shows up as:**
  - `SendReliable()` returning an id with `source == 0` (at most 8 reliable messages can be pending);
  - TX queues filling up;
  - more `Failed` outcomes, whose retries add even more load.

This example paces itself instead of using a fixed delay. It sends the next message only when all of these hold:

1. the previous message has an outcome (`Delivered` or `Failed`);
2. the TX queue is empty (`GetTxQueueSize() == 0`), so forwarded messages and ACKs have gone out too;
3. at least `kMinSendIntervalMs` has passed.

Then it waits `GetTimeUntilNextDataSlot()` so the message is sent in this node's next slot. Longer paths, retries and busy neighbours all slow it down. An application that sends more often should use the same signals rather than a fixed rate.

## Expected Serial Output

```
Sent "Reliable hello #1" to 0x3adf (seq=0)
Message seq=0 to 0x3adf delivered (acknowledged by 0x3adf, RTT 2140 ms)
Received from 0x3adf (seq=0, hops=1): Reliable hello #1
```

## Hardware and Radio Settings

Pins and radio parameters are set at the top of `src/main.cpp`, as in `simple_example`; see its README for the board table, regional frequencies and troubleshooting.

## Building and Uploading

```bash
pio run -e ttgo-t-beam --target upload
pio device monitor -b 115200
```
