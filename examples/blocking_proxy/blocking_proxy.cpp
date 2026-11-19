// Blocking-I/O reverse proxy baseline (Member 2).
//
// Purpose: the simplest correct proxy — one blocking accept loop, one thread per client
// (or fully sequential) — so that the epoll-based AegisProxy has a measured baseline
// to compare against in benchmarks/README.md. It must NOT use aegis_network.
//
// TODO(Member 2):
//   1. Parse --listen PORT and --backend HOST:PORT (single backend is enough).
//   2. socket/bind/listen; loop: accept (blocking).
//   3. Per client (sequentially first; std::thread per connection as a second variant):
//        read the request until the end of headers (+ Content-Length body) using
//        http::HttpParser once it exists, or a minimal "\r\n\r\n" scan before that;
//        connect to the backend (blocking), send the bytes, read the response until the
//        backend closes or Content-Length is satisfied, send it to the client, close.
//   4. Record requests/s and p50/p99 latency with wrk against both proxies and the
//      backend directly; put the numbers and the exact commands in benchmarks/README.md.
//
// Until then the program exits with status 2 so nobody mistakes it for a result.
#include "aegisproxy/common/result.hpp"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

struct Options {
  std::uint16_t listen_port{8081};
  std::string backend_host{"127.0.0.1"};
  std::uint16_t backend_port{9001};
};

aegis::Result<Options> parse(int argc, char** argv) {
  Options o;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--listen" && i + 1 < argc) {
      o.listen_port = static_cast<std::uint16_t>(std::stoi(argv[++i]));
    } else if (arg == "--backend" && i + 1 < argc) {
      const std::string hp = argv[++i];
      const auto colon = hp.find(':');
      if (colon == std::string::npos) {
        return aegis::Error{aegis::ErrorCode::InvalidArgument, "--backend expects HOST:PORT"};
      }
      o.backend_host = hp.substr(0, colon);
      o.backend_port = static_cast<std::uint16_t>(std::stoi(hp.substr(colon + 1)));
    } else {
      return aegis::Error{aegis::ErrorCode::InvalidArgument, "unknown argument '" + arg + "'"};
    }
  }
  return o;
}

}  // namespace

int main(int argc, char** argv) {
  auto opts = parse(argc, argv);
  if (!opts) {
    std::fprintf(stderr, "error: %s\nusage: %s [--listen PORT] [--backend HOST:PORT]\n",
                 opts.error().message.c_str(), argv[0]);
    return 64;
  }
  std::fprintf(stderr,
               "blocking_proxy_baseline: not implemented yet (TODO Member 2). "
               "Would listen on :%u and forward to %s:%u.\n",
               static_cast<unsigned>(opts->listen_port), opts->backend_host.c_str(),
               static_cast<unsigned>(opts->backend_port));
  return 2;
}
