# gRPC Python Observability Dashboard

A turnkey observability stack for gRPC services using Docker Compose.
Spins up a gRPC server, load generator, Prometheus, and Grafana with
pre-built dashboards — all with a single command.

## Why Observability for gRPC?

gRPC services communicate over HTTP/2 with binary protobuf payloads, making
traditional HTTP logging insufficient. Without proper instrumentation, teams
often struggle to answer basic operational questions about their services.

### Use Cases

**1. Debugging Latency Issues in Microservices**

When users report slow responses, you need to know *which* RPC method is slow
and *where* in the call chain the delay occurs. This dashboard breaks down
latency by method at p50, p95, and p99 — so you can distinguish between a
universally slow service and one that only degrades under tail conditions.

**2. Capacity Planning and Load Testing**

Before scaling a gRPC service, you need baseline metrics: how many RPCs/sec
can a single instance handle? At what throughput does latency degrade? The
RPC Rate and Latency panels together reveal the service's saturation point,
helping you right-size deployments and set autoscaling thresholds.

**3. SLO Monitoring and Alerting**

If your team commits to an SLO like "99% of RPCs complete under 200ms," this
dashboard gives you the exact data to track compliance. The Error Rate panel
and latency percentiles map directly to SLI/SLO definitions. Prometheus
alerting rules can be layered on top of the same metrics.

**4. Comparing Client vs. Server Perspectives**

gRPC client and server see different latencies — the client includes network
round-trip time, DNS resolution, and connection setup. This dashboard shows
both views side by side, which helps isolate whether a performance issue is
in the network layer or the application logic.

**5. Detecting Silent Failures**

A gRPC service might return errors (UNAVAILABLE, DEADLINE_EXCEEDED, INTERNAL)
that clients silently retry. Without observability, these failures are
invisible. The Error Rate panel surfaces them before they cascade into
user-visible outages.

**6. Onboarding and Learning**

This example serves as a starting point for teams adopting gRPC observability.
It demonstrates the full pipeline — from OpenTelemetry instrumentation in
Python, through Prometheus scraping, to Grafana visualization — without
requiring any cloud infrastructure or vendor accounts.

## Architecture

```
┌──────────────┐     gRPC      ┌──────────────┐
│  grpc-client │──────────────>│  grpc-server  │
│ (load gen)   │               │               │
│  :9465/metrics│              │  :9464/metrics │
└──────┬───────┘               └──────┬────────┘
       │                              │
       │        ┌──────────────┐      │
       └───────>│  Prometheus  │<─────┘
          scrape│   :9090      │scrape
                └──────┬───────┘
                       │
                ┌──────┴───────┐
                │   Grafana    │
                │   :3000      │
                └──────────────┘
```

### How It Works

1. **gRPC Server** registers an `OpenTelemetryPlugin` that automatically
   captures RPC metrics (call count, latency, message sizes) per the
   [gRFC A66](https://github.com/grpc/proposal/blob/master/A66-otel-stats.md)
   specification. A `PrometheusMetricReader` exposes these at `/metrics`.

2. **gRPC Client** (load generator) sends a steady stream of RPCs and
   exports its own client-side metrics on a separate Prometheus endpoint.

3. **Prometheus** scrapes both endpoints every 5 seconds, storing the
   time-series data.

4. **Grafana** queries Prometheus and renders pre-configured dashboard
   panels with no manual setup required.

## Prerequisites

- [Docker](https://docs.docker.com/get-docker/) (v20.10+)
- [Docker Compose](https://docs.docker.com/compose/install/) (v2.0+)

## Quick Start

```bash
cd examples/python/observability/dashboard
docker compose up
```

Then open:
- **Grafana**: http://localhost:3000 (no login required)
- **Prometheus**: http://localhost:9090

The dashboard auto-populates within ~30 seconds as the load generator
sends RPCs to the server.

## Dashboard Panels

| Panel | Description | What to Look For |
|-------|-------------|------------------|
| RPC Rate | Server-side requests/sec by method | Sudden drops indicate server issues; spikes show burst traffic |
| Error Rate | Percentage of failed RPCs | Should stay below your SLO threshold (e.g., < 1%) |
| Server Call Latency | p50, p95, p99 from server | Large gap between p50 and p99 suggests inconsistent performance |
| Client Attempt Latency | p50, p95, p99 from client | Difference from server latency reveals network overhead |
| Message Sizes | Compressed bytes sent/received | Unexpected growth may indicate payload bloat or missing pagination |
| Client Attempts Started | Total attempt count | Useful for correlating with external events or deployments |

## Metrics Reference

These metrics follow the [gRFC A66](https://github.com/grpc/proposal/blob/master/A66-otel-stats.md)
OpenTelemetry stats specification:

**Server metrics** (scraped from `:9464`):
- `grpc.server.call.started` — Number of RPCs started
- `grpc.server.call.duration` — End-to-end call latency (seconds)
- `grpc.server.call.sent_total_compressed_message_size` — Bytes sent
- `grpc.server.call.rcvd_total_compressed_message_size` — Bytes received

**Client metrics** (scraped from `:9465`):
- `grpc.client.attempt.started` — Number of attempts started
- `grpc.client.attempt.duration` — End-to-end attempt latency (seconds)
- `grpc.client.attempt.sent_total_compressed_message_size` — Bytes sent
- `grpc.client.attempt.rcvd_total_compressed_message_size` — Bytes received

## Configuration

**Adjust load generator rate:**

```yaml
# In docker-compose.yaml, change --rps value:
command: python dashboard_greeter_client.py --target grpc-server:50051 --rps 50
```

**Add your own gRPC service:**

1. Instrument your server with `grpc_observability.OpenTelemetryPlugin` and
   a `PrometheusMetricReader` (see `dashboard_greeter_server.py` for the
   pattern).
2. Add a scrape target in `prometheus/prometheus.yml`.
3. The same Grafana dashboard will automatically pick up the new metrics.

**Customize histogram buckets:**

The server and client configure latency and message-size histogram boundaries
matching the gRFC A66 specification. To adjust, modify the `_create_views()`
function in either Python file.

## Adapting for Production

This example is designed for local development and learning. For production:

- Replace anonymous Grafana access with proper authentication.
- Use persistent volumes for Prometheus data retention.
- Add Prometheus alerting rules for SLO violations.
- Consider using the OpenTelemetry Collector as an intermediary between
  your services and Prometheus for more flexible metric routing.
- For Google Cloud environments, see the
  [CSM observability example](../csm/) which integrates with Cloud
  Monitoring.

## Teardown

```bash
docker compose down
```
