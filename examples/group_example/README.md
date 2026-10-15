# LoRaMesher Group Messaging Example

Shows how to send one message to every member of a group and collect the members' acknowledgements.

## What This Example Does

1. **Starts** a mesh node (same Builder flow as `simple_example`)
2. **Joins** group `0x8001` with `JoinGroup()`
3. **Sends** `"Group hello #N"` to the group with `SendGroup()`, asking members to acknowledge, one message at a time (see [Traffic Budget](#traffic-budget))
4. **Prints** each member's acknowledgement and, when the acknowledgement window closes, how many members answered
5. **Prints** received messages, telling group messages from unicast ones

Flash it on two or more boards; every node is both a member and a sender.

## Groups

- Group addresses range from `kGroupAddressMin` (`0x8000`) to `kGroupAddressMax` (`0xFFFE`); `IsGroupAddress()` checks one.
- A node can belong to up to 8 groups (`JoinGroup` / `LeaveGroup` / `IsMemberOfGroup` / `GetGroups`). Membership is kept when the node re-joins a network.
- Sending to a group does not require being a member.
- A group message is flooded through the mesh; each member delivers it to its application once.

## The Group API

```cpp
mesher->JoinGroup(0x8001);

GroupSendOptions options;
options.request_acks = true;  // Members acknowledge the message
options.window_ms = 8000;     // How long to collect acknowledgements
options.max_retries = 1;      // Re-floods inside the window

LoraMesher::MessageId id = mesher->SendGroup(0x8001, payload, options);
```

With `request_acks = false` the send is best effort and nothing is reported through `SetDeliveryCallback()`.

With `request_acks = true`, `SetDeliveryCallback()` reports:

| Outcome | When | Fields |
|---------|------|--------|
| `Delivered` | Once per member that acknowledges | `by` (member), `rtt_ms`, `ack_count` so far |
| `GroupWindowClosed` | When `window_ms` ends | `ack_count` (members that acknowledged) |

## Receiving Group Messages

```cpp
mesher->SetDataCallbackEx([](const ReceivedData& message) {
    if (IsGroupAddress(message.dest)) {
        // message.dest is the group
    }
});
```

Both callbacks run on the protocol task. Keep them short or hand the data to your own task (see `examples/queued_receive_example`).

## Traffic Budget

Group messages are the most expensive traffic in the mesh:

- **Slots are shared.** Each node transmits in its own data slots, `GetDataSlotsPerSuperframe()` per superframe (2 by default). The same slots carry the node's own messages, the messages it forwards for others and the ACKs it returns.
- **A group message reaches every node.** It is flooded, so it costs about one transmission per node in the network. With `request_acks`, every member also sends an ACK back over its hop distance, and `max_retries` re-floods the message.
- **Overload shows up as:**
  - `SendGroup()` returning an id with `source == 0`;
  - TX queues filling up;
  - fewer members acknowledging (`ack_count`).

This example paces itself instead of using a fixed delay. It sends the next group message only when all of these hold:

1. the previous acknowledgement window has closed (`GroupWindowClosed` or `Failed`);
2. the TX queue is empty (`GetTxQueueSize() == 0`);
3. at least `kMinSendIntervalMs` has passed.

Then it waits `GetTimeUntilNextDataSlot()` before sending. In large networks, keep the group send rate low and use `request_acks` only when you need the confirmation.

## Expected Serial Output

```
Sent "Group hello #1" to group 0x8001 (seq=0)
Group 0x8001 seq=0: member 0x3adf acknowledged (RTT 1830 ms)
Group 0x8001 seq=0: 1 member(s) acknowledged
Group 0x8001 message from 0x3adf (hops=1): Group hello #1
```

## Hardware and Radio Settings

Pins and radio parameters are set at the top of `src/main.cpp`, as in `simple_example`; see its README for the board table, regional frequencies and troubleshooting.

## Building and Uploading

```bash
pio run -e ttgo-t-beam --target upload
pio device monitor -b 115200
```
