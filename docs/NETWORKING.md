# Authoritative networking

Meat2D's first multiplayer layer is a dependency-free C++20 UDP stack for two
to eight players. It is built into the installable `Meat2D::Net` target and
shares the deterministic command stream used by embodied AI.

## Runtime flow

```text
client input
    │ reliable UDP + session token
    ▼
server validation ──► target-tick command queue ──► LivingSimulation::step()
                                                        │
                       snapshots + interested chunks ◄──┘
                                   │
                                   ▼
                         replicated client World
```

`AuthoritativeServer::update()` receives datagrams, validates and orders inputs,
steps the living simulation once, emits snapshots and at most one changed
interested chunk per client, retransmits overdue reliable packets, and removes
timed-out clients.

`AuthoritativeClient::update()` receives packets, advances reliability state,
reassembles chunk messages, applies them to a read-only replicated-world view,
and sends acknowledgements or periodic keepalives.

## Wire contract

- Protocol magic: `M2DN`
- Protocol version: 1
- Default UDP port: 27182
- Maximum clients: 8
- Maximum datagram: 1,200 bytes
- Encoded packet header: 28 bytes
- Acknowledgement history: latest sequence plus 32 previous bits
- Chunk cell wire state: material, variant, state, temperature, and X/Y velocity

Every integer is encoded little-endian field by field. C++ padding, update
epochs, pointers, container state, and floating-point values never enter the
wire format.

## Reliability

All packets carry a wrapping 32-bit sequence and the receiver's latest
acknowledgement window. Reliable control, input, and chunk-fragment packets are
retained and resent after a configurable update interval. Receivers reject
duplicates while still returning acknowledgement information. Tests cover
dropped initial delivery, repeated retransmission, duplicates, out-of-order
delivery, and sequence wraparound.

Snapshots are intentionally unreliable and periodic. They carry server tick,
combined authoritative hash, agent count, organism population, and active
chunk count.

## Chunk replication

Each client reports a focus point. The server maps it to a square radius in
64×64 chunks and compares each interested chunk's revision with the last queued
revision for that client.

A chunk delta run-length encodes all 4,096 cells while omitting the ephemeral
update epoch. The worst-case encoded message is fragmented into payloads that
keep the complete UDP datagram at or below 1,200 bytes. Reassembly accepts
fragments in any order, suppresses duplicates, caps memory and fragment count,
and expires abandoned messages.

The current client mirror receives complete chunks. Dirty-rectangle deltas and
entity/organism visual replication are later bandwidth improvements.

## Session and validation boundary

The reliable Hello includes a client nonce and bounded player name. Welcome
returns the nonce, world configuration, assigned client ID, and a server-secret
derived session token. Every input must:

- come from the endpoint owning that client slot;
- carry its session token;
- have a newer input sequence;
- target no more than eight ticks into the future;
- fit the per-client input budget and maximum brush radius;
- reference valid in-bounds coordinates and stable material IDs.

This prevents accidental cross-session input and raises the bar for spoofing,
but it is not encryption or account authentication. Internet-facing games
should add an authenticated encryption layer, identity service, denial-of-
service controls, and NAT traversal appropriate to their deployment.

## Run locally

```bash
meat2d_server --listen --port 27182
meat2d_sandbox --connect localhost --port 27182 --name Player1
meat2d_remote --host localhost --port 27182
```

Without `--listen`, `meat2d_server --ticks N` retains its fast deterministic
headless benchmark mode.
