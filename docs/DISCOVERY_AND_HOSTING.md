# Discovery and player-hosted sessions

Meat2D supports four ways to find and join an authoritative session:

| Path | Intended use | Join endpoint |
| --- | --- | --- |
| Local/direct | Host and client on one computer | `127.0.0.1:27182` |
| LAN browser | Computers on one local network | Automatic UDP discovery |
| Direct internet | VPS, open firewall, or forwarded router port | Hostname/IP and UDP port |
| Public browser | Internet-hosted or player-hosted server | Directory listing plus NAT punch |

The game server remains authoritative and self-hosted in every case. A public
directory only maintains short-lived listings and introduces UDP peers. It
does not run the simulation, receive gameplay packets, or own a session.

## Ports

- `27182/UDP`: authoritative game traffic
- `27183/UDP`: LAN discovery queries
- `27184/UDP`: public directory registration, listing, and introductions

All three are configurable. A dedicated internet server normally needs its
gameplay UDP port allowed through the host firewall. A public directory needs
its directory UDP port allowed.

## Same-computer and direct joins

Start a server:

```bash
meat2d_server --listen --port 27182
```

Add `--persist <dir>` to save/reload the world across restarts, and
`--parallel [workers]` to multithread the simulation tick — see
[Persistence and streaming](../README.md#persistence-and-streaming) and
[Parallel simulation](../README.md#parallel-simulation).

The hosting player can join their own process:

```bash
meat2d_sandbox --connect 127.0.0.1 --port 27182 --name Host
```

Other players can use the host's DNS name or IP with the same direct-connect
flow. For a home router, forward the chosen UDP gameplay port when automatic
NAT traversal is unavailable.

For development, the graphical editor exposes the same paths without manual
commands. Its **Multiplayer** tab can start the bundled authoritative server,
join it locally, join a direct endpoint, or launch a selected LAN/public
listing. Public advertisement uses the directory endpoint entered in the same
tab. These editor-owned living-lab processes are stopped when the editor
closes; shipped games provide their own UI on top of the same `Meat2D::Net`
APIs.

## LAN server browser

The dedicated-server app advertises on LAN by default. Disable it with
`--no-lan`, or move discovery to another port with `--discovery-port`.

```bash
meat2d_server --listen --name "Workshop World"
meat2d_remote --list-lan
```

A browser sends both a limited broadcast query and a loopback query. Servers
reply directly to the requesting endpoint with bounded session metadata and
the gameplay port. No permanent multicast group or central service is needed.

Applications use `LanServerAdvertiser` and `LanServerBrowser` from
`Meat2D::Net`. A query ID rejects stale replies, build IDs filter incompatible
games, and duplicate advertisements are collapsed by server ID.

## Self-hosted public directory

Run the directory on a public machine:

```bash
meat2d_directory --port 27184 --max-servers 1024 --lease-seconds 15
```

Point a game server at it:

```bash
meat2d_server --listen \
  --name "Public Elements Lab" \
  --public-directory directory.example.com \
  --directory-port 27184
```

Browse and join:

```bash
meat2d_remote --list-public \
  --directory directory.example.com --directory-port 27184

meat2d_remote --server-id 123456789 \
  --directory directory.example.com --directory-port 27184

meat2d_sandbox --server-id 123456789 \
  --directory directory.example.com --directory-port 27184 --name Player
```

The repository does not hard-code an operated public directory. A game can
deploy one, choose a community directory, or expose a directory setting to its
players.

### Registration and listing

Public heartbeats leave from the same UDP socket used for gameplay. The
directory therefore records the source endpoint observed on the network and
ignores the address claimed inside the registration. This is important for NAT
mapping and prevents a server from listing an unrelated victim address.

Listings are:

- paginated into MTU-safe responses;
- filtered by game build ID;
- capped by the directory configuration;
- removed when their short lease is not refreshed;
- protected from accidental server-ID replacement by a per-process
  registration secret.

Names, modes, map names, player counts, endpoints, flags, and page sizes are
strictly bounded before storage or display.

### NAT introduction

When a player selects a public listing:

1. The client sends a join request from its future gameplay socket.
2. The directory sends each peer the endpoint it observed for the other.
3. Both peers send a small hole-punch datagram.
4. The normal reliable Hello/Welcome handshake proceeds directly between the
   client and authoritative server.

Gameplay never passes through the directory.

UDP hole punching works with many consumer NATs, but no peer-to-peer design can
guarantee it through every symmetric NAT, carrier-grade NAT, enterprise
firewall, or network that blocks UDP. Those cases currently need one of:

- a forwarded/open UDP gameplay port;
- a server on a public VPS or other reachable host;
- an overlay network chosen by the players.

A permissioned gameplay relay is a future fallback. The engine reports this
limitation rather than silently routing simulation traffic through an
untrusted directory.

## Security boundary

Discovery is not identity, encryption, or an anti-abuse service. The current
layer includes bounds checks, endpoint validation, expiring leases, a listing
capacity, per-update packet budgets, and registration-secret matching.
Internet games should still add:

- authenticated encryption for session traffic;
- account or invitation identity where required;
- per-address rate limits and operational DDoS protection;
- moderation and signed server metadata for a curated public browser;
- relay authorization and quotas before enabling a relay.

See [NETWORKING.md](NETWORKING.md) for the authoritative gameplay protocol.
