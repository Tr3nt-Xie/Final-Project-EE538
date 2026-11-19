// AegisProxy entry point: configuration, wiring, signal handling, run loop.
//
// Wiring order (ownership flows top-down; nothing holds a pointer to something that
// is destroyed earlier):
//   ProxyConfig → EventLoop → BackendRegistry → LoadBalancingStrategy → HealthMonitor
//   → ConnectionManager → BackendConnector → HttpProxy
// All of these live on main()'s stack / in unique_ptrs declared in this order, so
// destruction happens in reverse and no callback can outlive its target.

#include "aegisproxy/balancer/backend_registry.hpp"
#include "aegisproxy/balancer/strategy_factory.hpp"
#include "aegisproxy/common/config.hpp"
#include "aegisproxy/common/log.hpp"
#include "aegisproxy/health/health_monitor.hpp"
#include "aegisproxy/network/connection_manager.hpp"
#include "aegisproxy/network/event_loop.hpp"
#include "aegisproxy/proxy/backend_connector.hpp"
#include "aegisproxy/proxy/http_proxy.hpp"

#include <csignal>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

#if defined(AEGIS_HAVE_EPOLL)
#include <sys/signalfd.h>
#include <unistd.h>
#endif

namespace {

constexpr std::string_view kLog = "main";

void print_usage(const char* argv0) {
  std::fprintf(stderr,
               "Usage: %s --config <file> [--check-config] [--log-level debug|info|warn|error]\n"
               "       %s --help\n",
               argv0, argv0);
}

struct CliOptions {
  std::string config_path;
  std::string log_level_override;
  bool check_only{false};
  bool help{false};
};

aegis::Result<CliOptions> parse_cli(int argc, char** argv) {
  CliOptions opts;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      opts.help = true;
    } else if (arg == "--check-config") {
      opts.check_only = true;
    } else if (arg == "--config" || arg == "-c") {
      if (i + 1 >= argc)
        return aegis::Error{aegis::ErrorCode::InvalidArgument, "--config needs a path"};
      opts.config_path = argv[++i];
    } else if (arg == "--log-level") {
      if (i + 1 >= argc)
        return aegis::Error{aegis::ErrorCode::InvalidArgument, "--log-level needs a value"};
      opts.log_level_override = argv[++i];
    } else {
      return aegis::Error{aegis::ErrorCode::InvalidArgument,
                          "unknown argument '" + std::string(arg) + "'"};
    }
  }
  if (!opts.help && opts.config_path.empty()) {
    return aegis::Error{aegis::ErrorCode::InvalidArgument, "--config is required"};
  }
  return opts;
}

aegis::log::Level level_from_name(std::string_view name) {
  if (name == "debug")
    return aegis::log::Level::Debug;
  if (name == "warn")
    return aegis::log::Level::Warn;
  if (name == "error")
    return aegis::log::Level::Error;
  return aegis::log::Level::Info;
}

#if defined(AEGIS_HAVE_EPOLL)
/// Routes SIGINT/SIGTERM through a signalfd so that shutdown runs on the loop thread
/// with no global state and no async-signal-safety concerns.
class SignalStopper {
 public:
  static aegis::Result<std::unique_ptr<SignalStopper>> install(aegis::net::EventLoop& loop) {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &mask, nullptr) < 0) {
      return aegis::Error::from_errno("sigprocmask");
    }
    aegis::net::SocketHandle fd{::signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC)};
    if (!fd)
      return aegis::Error::from_errno("signalfd");
    auto stopper = std::unique_ptr<SignalStopper>(new SignalStopper(loop, std::move(fd)));
    auto id = loop.add(stopper->fd_.get(), aegis::net::EventMask::Readable,
                       [raw = stopper.get()](aegis::net::EventMask) { raw->on_signal(); });
    if (!id)
      return id.error();
    stopper->handler_ = *id;
    return stopper;
  }

  ~SignalStopper() {
    if (handler_ != aegis::kInvalidHandlerId)
      (void)loop_.remove(handler_);
  }
  SignalStopper(const SignalStopper&) = delete;
  SignalStopper& operator=(const SignalStopper&) = delete;

 private:
  SignalStopper(aegis::net::EventLoop& loop, aegis::net::SocketHandle fd)
      : loop_(loop), fd_(std::move(fd)) {}

  void on_signal() {
    signalfd_siginfo info{};
    while (::read(fd_.get(), &info, sizeof info) == static_cast<ssize_t>(sizeof info)) {
      aegis::log::info(kLog, std::string("received ") +
                                 strsignal(static_cast<int>(info.ssi_signo)) + ", shutting down");
    }
    loop_.stop();
  }

  aegis::net::EventLoop& loop_;
  aegis::net::SocketHandle fd_;
  aegis::HandlerId handler_{aegis::kInvalidHandlerId};
};
#endif

int run_proxy(const aegis::ProxyConfig& config) {
  using namespace aegis;

  // SIGPIPE would kill the process on a write to a closed socket; we handle EPIPE instead.
  std::signal(SIGPIPE, SIG_IGN);

  auto loop = net::EventLoop::create();
  if (!loop) {
    log::error(kLog, "cannot create event loop: " + loop.error().to_string());
    return 2;
  }

  lb::BackendRegistry registry;
  for (const BackendConfig& b : config.backends) {
    const BackendId id = registry.add(lb::BackendEndpoint{b.name, b.host, b.port, b.weight});
    // TODO(Member 3): HealthMonitor should drive this. Until active probing exists,
    // backends start Unknown and nothing marks them Healthy — forwarding is not
    // implemented yet anyway, so this is not hidden behaviour.
    log::info(kLog, "backend " + std::to_string(id) + " " + b.name + " " + b.host + ":" +
                        std::to_string(b.port) + " weight=" + std::to_string(b.weight));
  }

  auto strategy = lb::make_strategy(config.strategy, registry);
  if (!strategy) {
    log::error(kLog, strategy.error().to_string());
    return 2;
  }
  log::info(kLog, "strategy: " + std::string((*strategy)->name()));

  health::HealthMonitor monitor(
      registry,
      health::HealthConfig{config.health_interval, config.health_timeout, config.healthy_threshold,
                           config.unhealthy_threshold, config.health_path});
  if (Status st = monitor.start(); !st) {
    log::error(kLog, st.error().to_string());
    return 2;
  }
  // TODO(Member 1 + 3): periodic timer on the EventLoop → monitor.tick(now) → probes.
  log::warn(kLog, "health monitoring is not implemented: backends stay in state Unknown");

  net::ConnectionManager connections(
      **loop, net::ConnectionLimits{config.max_connections, config.buffer_limit_bytes});
  if (Status st = connections.listen(config.listen_host, config.listen_port, config.listen_backlog);
      !st) {
    log::error(kLog, "listen failed: " + st.error().to_string());
    return 2;
  }

  proxy::BackendConnector connector;
  proxy::HttpProxy http_proxy(connections, registry, **strategy, connector);
  http_proxy.attach();

#if defined(AEGIS_HAVE_EPOLL)
  auto stopper = SignalStopper::install(**loop);
  if (!stopper) {
    log::error(kLog, "cannot install signal handler: " + stopper.error().to_string());
    return 2;
  }
#endif

  log::info(kLog, "AegisProxy " + std::string(AEGIS_VERSION_STRING) + " ready");
  Status run_status = (*loop)->run();
  if (!run_status) {
    log::error(kLog, "event loop failed: " + run_status.error().to_string());
    return 3;
  }
  log::info(kLog, "event loop stopped; " + std::to_string(connections.size()) +
                      " connection(s) open at shutdown, " +
                      std::to_string(connections.total_accepted()) + " accepted in total");
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  auto opts = parse_cli(argc, argv);
  if (!opts) {
    std::fprintf(stderr, "error: %s\n", opts.error().message.c_str());
    print_usage(argv[0]);
    return 64;  // EX_USAGE
  }
  if (opts->help) {
    print_usage(argv[0]);
    return 0;
  }

  auto config = aegis::load_config_file(opts->config_path);
  if (!config) {
    std::fprintf(stderr, "config error: %s\n", config.error().message.c_str());
    return 78;  // EX_CONFIG
  }
  aegis::log::set_level(level_from_name(
      opts->log_level_override.empty() ? config->log_level : opts->log_level_override));

  if (opts->check_only) {
    std::printf("config OK: listen %s:%u, strategy %s, %zu backend(s)\n",
                config->listen_host.c_str(), static_cast<unsigned>(config->listen_port),
                config->strategy.c_str(), config->backends.size());
    return 0;
  }

  return run_proxy(*config);
}
