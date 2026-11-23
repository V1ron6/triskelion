#pragma once
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "triskelion/updater.hpp"

namespace triskelion {

// Broadcast channel. Platform code implements it (SSE on Windows, FCM bridge on Android).
// The ping carries no data: it only means "go and check now".
class Stream {
 public:
  virtual ~Stream() = default;
  // Blocks, calling on_update() once per broadcast. Returns when the connection ends or fails.
  virtual void run(const std::string& url, const std::function<void()>& on_update) = 0;
  // Thread-safe and STICKY: the current run() returns promptly, and any later run() returns at once.
  virtual void abort() = 0;
};

struct ClientOptions {
  std::chrono::milliseconds check_interval{std::chrono::hours(6)};      // routine pull
  std::chrono::milliseconds retry_interval{std::chrono::minutes(15)};   // after a failed check/download/apply
  std::chrono::milliseconds reconnect_delay{std::chrono::seconds(30)};  // between broadcast reconnects
};

// The embeddable updater: registers the install, pulls on launch and on a timer, listens for
// broadcasts, and pulls immediately when one arrives. Owns two threads, so the app only calls
// start() and stop().
//
//  - Http is used from the worker thread only; the Stream from the listener thread only.
//  - The callback runs on the worker thread. Do not call stop() or destroy the Client from it.
//  - After the first Applied result the client goes idle for good (the running build is still the
//    old version, so checking again would just re-offer the same update).
class Client {
 public:
  using Callback = std::function<void(const RunResult&)>;

  Client(UpdateConfig cfg, Http& http, Stream& stream, Applier& applier, ClientOptions opts = {}, Callback cb = {});
  ~Client();
  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;

  void start();       // idempotent
  void stop();        // idempotent; returns after both threads have finished
  void check_now();   // ask for a check outside the timer
  bool update_applied() const;

 private:
  void worker();
  void listener();
  void signal_check();
  bool stopping();

  UpdateConfig cfg_;
  Http& http_;
  Stream& stream_;
  Applier& applier_;
  ClientOptions opts_;
  Callback cb_;

  mutable std::mutex m_;
  std::condition_variable cv_;
  bool stop_ = false;
  bool wake_ = false;
  bool applied_ = false;
  bool started_ = false;
  std::thread worker_, listener_;
};

}  // namespace triskelion
