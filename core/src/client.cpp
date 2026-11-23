#include "triskelion/client.hpp"

namespace triskelion {

Client::Client(UpdateConfig cfg, Http& http, Stream& stream, Applier& applier, ClientOptions opts, Callback cb)
    : cfg_(std::move(cfg)), http_(http), stream_(stream), applier_(applier), opts_(opts), cb_(std::move(cb)) {}

Client::~Client() { stop(); }

void Client::start() {
  std::lock_guard<std::mutex> lock(m_);
  if (started_ || stop_) return;
  started_ = true;  // the worker's first wait is zero, so the launch check runs immediately
  worker_ = std::thread([this] { worker(); });
  listener_ = std::thread([this] { listener(); });
}

void Client::stop() {
  {
    std::lock_guard<std::mutex> lock(m_);
    stop_ = true;
  }
  cv_.notify_all();
  stream_.abort();  // sticky, so it also catches a listener that has not reached run() yet

  // Called from the callback (worker thread): flags are set, the owner joins later.
  if (std::this_thread::get_id() == worker_.get_id()) return;
  if (worker_.joinable()) worker_.join();
  if (listener_.joinable()) listener_.join();
}

void Client::check_now() { signal_check(); }

bool Client::update_applied() const {
  std::lock_guard<std::mutex> lock(m_);
  return applied_;
}

void Client::signal_check() {
  {
    std::lock_guard<std::mutex> lock(m_);
    wake_ = true;  // many pings while a check runs collapse into one more check
  }
  cv_.notify_all();
}

bool Client::stopping() {
  std::lock_guard<std::mutex> lock(m_);
  return stop_;
}

void Client::worker() {
  bool registered = false;
  std::chrono::milliseconds wait{0};

  while (true) {
    {
      std::unique_lock<std::mutex> lock(m_);
      cv_.wait_for(lock, wait, [this] { return stop_ || wake_; });
      if (stop_) return;
      wake_ = false;
    }

    // The server can only broadcast to registered installs, so keep trying until it sticks.
    if (!registered) registered = register_install(cfg_, http_);

    const RunResult result = run_update(cfg_, http_, applier_);
    if (cb_) {
      try {
        cb_(result);
      } catch (...) {  // an app bug in the callback must not kill updating
      }
    }

    switch (result.outcome) {
      case Outcome::Applied:
        {
          std::lock_guard<std::mutex> lock(m_);
          applied_ = true;
          stop_ = true;
        }
        cv_.notify_all();
        stream_.abort();
        return;
      case Outcome::UpToDate:
      case Outcome::Rejected:  // a bad release will not fix itself in 15 minutes; do not hammer the server
        wait = opts_.check_interval;
        break;
      default:  // check, download or apply failed: probably the network, try again sooner
        wait = opts_.retry_interval;
        break;
    }
  }
}

void Client::listener() {
  const std::string url = build_stream_url(cfg_);
  while (!stopping()) {
    stream_.run(url, [this] { signal_check(); });
    std::unique_lock<std::mutex> lock(m_);
    cv_.wait_for(lock, opts_.reconnect_delay, [this] { return stop_; });
  }
}

}  // namespace triskelion
