# LoRaMesher Reliable Messaging Example

Shows how to send acknowledged (reliable) unicast messages and follow each one until it is delivered or fails.

## What This Example Does

1. **Starts** a mesh node (same Builder flow as `simple_example`)
2. **Registers** two callbacks:
   - `SetDataCallbackEx` — received messages with their metadata (source, sequence number, hops travelled)
   - `SetDeliveryCallback` — the outcome of each reliable send
3. **Sends** `"Reliable hello #N"` with `SendReliable()` to the next peer in the routing table every 15 seconds
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
