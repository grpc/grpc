template <typename F0, typename F1> class Seq<F0, F1> {
 private:
  char state_ = 0;
  struct State0 {
    State0(F0&& f0, F1&& f1) : f(std::forward<F0>(f0)), next(std::forward<F1>(f1)) {}
    State0(State0&& other) : f(std::move(other.f)), next(std::move(other.next)) {}
    State0(const State0& other) : f(other.f), next(other.next) {}
    ~State0() = delete;
    using F = F0;
    [[no_unique_address]] F0 f;
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F1>;
    [[no_unique_address]] Next next;
  };
  using FLast = typename State0::Next::Promise;
  union {
    [[no_unique_address]] State0 prior_;
    [[no_unique_address]] FLast f_;
  };
 public:
  Seq(F0 f0, F1 f1) : prior_(std::move(f0), std::move(f1)) {}
  Seq& operator=(const Seq&) = delete;
  Seq(const Seq& other) {
    assert(other.state_ == 0);
    new (&prior_) State0(other.prior_);
  }
  Seq(Seq&& other) {
    assert(other.state_ == 0);
    new (&prior_) State0(std::move(other.prior_));
  }
  ~Seq() {
    switch (state_) {
     case 0:
      Destruct(&prior_.f);
      goto fin0;
     case 1:
      Destruct(&f_);
      return;
    }
  fin0:
    Destruct(&prior_.next);
  }
  decltype(std::declval<typename State0::Next::Promise>()()) operator()() {
    switch (state_) {
     case 0: {
      auto r = prior_.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.f);
      auto n = prior_.next.Once(std::move(*p));
      Destruct(&prior_.next);
      new (&f_) typename State0::Next::Promise(std::move(n));
      state_ = 1;
     }
     case 1:
      return f_();
    }
    return kPending;
  }
};
template <typename F0, typename F1, typename F2> class Seq<F0, F1, F2> {
 private:
  char state_ = 0;
  struct State0 {
    State0(F0&& f0, F1&& f1) : f(std::forward<F0>(f0)), next(std::forward<F1>(f1)) {}
    State0(State0&& other) : f(std::move(other.f)), next(std::move(other.next)) {}
    State0(const State0& other) : f(other.f), next(other.next) {}
    ~State0() = delete;
    using F = F0;
    [[no_unique_address]] F0 f;
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F1>;
    [[no_unique_address]] Next next;
  };
  struct State1 {
    State1(F0&& f0, F1&& f1, F2&& f2) : next(std::forward<F2>(f2)) { new (&prior) State0(std::forward<F0>(f0), std::forward<F1>(f1)); }
    State1(State1&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State1(const State1& other) : prior(other.prior), next(other.next) {}
    ~State1() = delete;
    using F = typename State0::Next::Promise;
    union {
      [[no_unique_address]] State0 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F2>;
    [[no_unique_address]] Next next;
  };
  using FLast = typename State1::Next::Promise;
  union {
    [[no_unique_address]] State1 prior_;
    [[no_unique_address]] FLast f_;
  };
 public:
  Seq(F0 f0, F1 f1, F2 f2) : prior_(std::move(f0), std::move(f1), std::move(f2)) {}
  Seq& operator=(const Seq&) = delete;
  Seq(const Seq& other) {
    assert(other.state_ == 0);
    new (&prior_) State1(other.prior_);
  }
  Seq(Seq&& other) {
    assert(other.state_ == 0);
    new (&prior_) State1(std::move(other.prior_));
  }
  ~Seq() {
    switch (state_) {
     case 0:
      Destruct(&prior_.prior.f);
      goto fin0;
     case 1:
      Destruct(&prior_.f);
      goto fin1;
     case 2:
      Destruct(&f_);
      return;
    }
  fin0:
    Destruct(&prior_.prior.next);
  fin1:
    Destruct(&prior_.next);
  }
  decltype(std::declval<typename State1::Next::Promise>()()) operator()() {
    switch (state_) {
     case 0: {
      auto r = prior_.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.f);
      auto n = prior_.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.next);
      new (&prior_.f) typename State0::Next::Promise(std::move(n));
      state_ = 1;
     }
     case 1: {
      auto r = prior_.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.f);
      auto n = prior_.next.Once(std::move(*p));
      Destruct(&prior_.next);
      new (&f_) typename State1::Next::Promise(std::move(n));
      state_ = 2;
     }
     case 2:
      return f_();
    }
    return kPending;
  }
};
template <typename F0, typename F1, typename F2, typename F3> class Seq<F0, F1, F2, F3> {
 private:
  char state_ = 0;
  struct State0 {
    State0(F0&& f0, F1&& f1) : f(std::forward<F0>(f0)), next(std::forward<F1>(f1)) {}
    State0(State0&& other) : f(std::move(other.f)), next(std::move(other.next)) {}
    State0(const State0& other) : f(other.f), next(other.next) {}
    ~State0() = delete;
    using F = F0;
    [[no_unique_address]] F0 f;
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F1>;
    [[no_unique_address]] Next next;
  };
  struct State1 {
    State1(F0&& f0, F1&& f1, F2&& f2) : next(std::forward<F2>(f2)) { new (&prior) State0(std::forward<F0>(f0), std::forward<F1>(f1)); }
    State1(State1&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State1(const State1& other) : prior(other.prior), next(other.next) {}
    ~State1() = delete;
    using F = typename State0::Next::Promise;
    union {
      [[no_unique_address]] State0 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F2>;
    [[no_unique_address]] Next next;
  };
  struct State2 {
    State2(F0&& f0, F1&& f1, F2&& f2, F3&& f3) : next(std::forward<F3>(f3)) { new (&prior) State1(std::forward<F0>(f0), std::forward<F1>(f1), std::forward<F2>(f2)); }
    State2(State2&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State2(const State2& other) : prior(other.prior), next(other.next) {}
    ~State2() = delete;
    using F = typename State1::Next::Promise;
    union {
      [[no_unique_address]] State1 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F3>;
    [[no_unique_address]] Next next;
  };
  using FLast = typename State2::Next::Promise;
  union {
    [[no_unique_address]] State2 prior_;
    [[no_unique_address]] FLast f_;
  };
 public:
  Seq(F0 f0, F1 f1, F2 f2, F3 f3) : prior_(std::move(f0), std::move(f1), std::move(f2), std::move(f3)) {}
  Seq& operator=(const Seq&) = delete;
  Seq(const Seq& other) {
    assert(other.state_ == 0);
    new (&prior_) State2(other.prior_);
  }
  Seq(Seq&& other) {
    assert(other.state_ == 0);
    new (&prior_) State2(std::move(other.prior_));
  }
  ~Seq() {
    switch (state_) {
     case 0:
      Destruct(&prior_.prior.prior.f);
      goto fin0;
     case 1:
      Destruct(&prior_.prior.f);
      goto fin1;
     case 2:
      Destruct(&prior_.f);
      goto fin2;
     case 3:
      Destruct(&f_);
      return;
    }
  fin0:
    Destruct(&prior_.prior.prior.next);
  fin1:
    Destruct(&prior_.prior.next);
  fin2:
    Destruct(&prior_.next);
  }
  decltype(std::declval<typename State2::Next::Promise>()()) operator()() {
    switch (state_) {
     case 0: {
      auto r = prior_.prior.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.prior.f);
      auto n = prior_.prior.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.prior.next);
      new (&prior_.prior.f) typename State0::Next::Promise(std::move(n));
      state_ = 1;
     }
     case 1: {
      auto r = prior_.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.f);
      auto n = prior_.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.next);
      new (&prior_.f) typename State1::Next::Promise(std::move(n));
      state_ = 2;
     }
     case 2: {
      auto r = prior_.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.f);
      auto n = prior_.next.Once(std::move(*p));
      Destruct(&prior_.next);
      new (&f_) typename State2::Next::Promise(std::move(n));
      state_ = 3;
     }
     case 3:
      return f_();
    }
    return kPending;
  }
};
template <typename F0, typename F1, typename F2, typename F3, typename F4> class Seq<F0, F1, F2, F3, F4> {
 private:
  char state_ = 0;
  struct State0 {
    State0(F0&& f0, F1&& f1) : f(std::forward<F0>(f0)), next(std::forward<F1>(f1)) {}
    State0(State0&& other) : f(std::move(other.f)), next(std::move(other.next)) {}
    State0(const State0& other) : f(other.f), next(other.next) {}
    ~State0() = delete;
    using F = F0;
    [[no_unique_address]] F0 f;
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F1>;
    [[no_unique_address]] Next next;
  };
  struct State1 {
    State1(F0&& f0, F1&& f1, F2&& f2) : next(std::forward<F2>(f2)) { new (&prior) State0(std::forward<F0>(f0), std::forward<F1>(f1)); }
    State1(State1&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State1(const State1& other) : prior(other.prior), next(other.next) {}
    ~State1() = delete;
    using F = typename State0::Next::Promise;
    union {
      [[no_unique_address]] State0 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F2>;
    [[no_unique_address]] Next next;
  };
  struct State2 {
    State2(F0&& f0, F1&& f1, F2&& f2, F3&& f3) : next(std::forward<F3>(f3)) { new (&prior) State1(std::forward<F0>(f0), std::forward<F1>(f1), std::forward<F2>(f2)); }
    State2(State2&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State2(const State2& other) : prior(other.prior), next(other.next) {}
    ~State2() = delete;
    using F = typename State1::Next::Promise;
    union {
      [[no_unique_address]] State1 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F3>;
    [[no_unique_address]] Next next;
  };
  struct State3 {
    State3(F0&& f0, F1&& f1, F2&& f2, F3&& f3, F4&& f4) : next(std::forward<F4>(f4)) { new (&prior) State2(std::forward<F0>(f0), std::forward<F1>(f1), std::forward<F2>(f2), std::forward<F3>(f3)); }
    State3(State3&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State3(const State3& other) : prior(other.prior), next(other.next) {}
    ~State3() = delete;
    using F = typename State2::Next::Promise;
    union {
      [[no_unique_address]] State2 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F4>;
    [[no_unique_address]] Next next;
  };
  using FLast = typename State3::Next::Promise;
  union {
    [[no_unique_address]] State3 prior_;
    [[no_unique_address]] FLast f_;
  };
 public:
  Seq(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4) : prior_(std::move(f0), std::move(f1), std::move(f2), std::move(f3), std::move(f4)) {}
  Seq& operator=(const Seq&) = delete;
  Seq(const Seq& other) {
    assert(other.state_ == 0);
    new (&prior_) State3(other.prior_);
  }
  Seq(Seq&& other) {
    assert(other.state_ == 0);
    new (&prior_) State3(std::move(other.prior_));
  }
  ~Seq() {
    switch (state_) {
     case 0:
      Destruct(&prior_.prior.prior.prior.f);
      goto fin0;
     case 1:
      Destruct(&prior_.prior.prior.f);
      goto fin1;
     case 2:
      Destruct(&prior_.prior.f);
      goto fin2;
     case 3:
      Destruct(&prior_.f);
      goto fin3;
     case 4:
      Destruct(&f_);
      return;
    }
  fin0:
    Destruct(&prior_.prior.prior.prior.next);
  fin1:
    Destruct(&prior_.prior.prior.next);
  fin2:
    Destruct(&prior_.prior.next);
  fin3:
    Destruct(&prior_.next);
  }
  decltype(std::declval<typename State3::Next::Promise>()()) operator()() {
    switch (state_) {
     case 0: {
      auto r = prior_.prior.prior.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.prior.prior.f);
      auto n = prior_.prior.prior.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.prior.prior.next);
      new (&prior_.prior.prior.f) typename State0::Next::Promise(std::move(n));
      state_ = 1;
     }
     case 1: {
      auto r = prior_.prior.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.prior.f);
      auto n = prior_.prior.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.prior.next);
      new (&prior_.prior.f) typename State1::Next::Promise(std::move(n));
      state_ = 2;
     }
     case 2: {
      auto r = prior_.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.f);
      auto n = prior_.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.next);
      new (&prior_.f) typename State2::Next::Promise(std::move(n));
      state_ = 3;
     }
     case 3: {
      auto r = prior_.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.f);
      auto n = prior_.next.Once(std::move(*p));
      Destruct(&prior_.next);
      new (&f_) typename State3::Next::Promise(std::move(n));
      state_ = 4;
     }
     case 4:
      return f_();
    }
    return kPending;
  }
};
template <typename F0, typename F1, typename F2, typename F3, typename F4, typename F5> class Seq<F0, F1, F2, F3, F4, F5> {
 private:
  char state_ = 0;
  struct State0 {
    State0(F0&& f0, F1&& f1) : f(std::forward<F0>(f0)), next(std::forward<F1>(f1)) {}
    State0(State0&& other) : f(std::move(other.f)), next(std::move(other.next)) {}
    State0(const State0& other) : f(other.f), next(other.next) {}
    ~State0() = delete;
    using F = F0;
    [[no_unique_address]] F0 f;
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F1>;
    [[no_unique_address]] Next next;
  };
  struct State1 {
    State1(F0&& f0, F1&& f1, F2&& f2) : next(std::forward<F2>(f2)) { new (&prior) State0(std::forward<F0>(f0), std::forward<F1>(f1)); }
    State1(State1&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State1(const State1& other) : prior(other.prior), next(other.next) {}
    ~State1() = delete;
    using F = typename State0::Next::Promise;
    union {
      [[no_unique_address]] State0 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F2>;
    [[no_unique_address]] Next next;
  };
  struct State2 {
    State2(F0&& f0, F1&& f1, F2&& f2, F3&& f3) : next(std::forward<F3>(f3)) { new (&prior) State1(std::forward<F0>(f0), std::forward<F1>(f1), std::forward<F2>(f2)); }
    State2(State2&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State2(const State2& other) : prior(other.prior), next(other.next) {}
    ~State2() = delete;
    using F = typename State1::Next::Promise;
    union {
      [[no_unique_address]] State1 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F3>;
    [[no_unique_address]] Next next;
  };
  struct State3 {
    State3(F0&& f0, F1&& f1, F2&& f2, F3&& f3, F4&& f4) : next(std::forward<F4>(f4)) { new (&prior) State2(std::forward<F0>(f0), std::forward<F1>(f1), std::forward<F2>(f2), std::forward<F3>(f3)); }
    State3(State3&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State3(const State3& other) : prior(other.prior), next(other.next) {}
    ~State3() = delete;
    using F = typename State2::Next::Promise;
    union {
      [[no_unique_address]] State2 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F4>;
    [[no_unique_address]] Next next;
  };
  struct State4 {
    State4(F0&& f0, F1&& f1, F2&& f2, F3&& f3, F4&& f4, F5&& f5) : next(std::forward<F5>(f5)) { new (&prior) State3(std::forward<F0>(f0), std::forward<F1>(f1), std::forward<F2>(f2), std::forward<F3>(f3), std::forward<F4>(f4)); }
    State4(State4&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State4(const State4& other) : prior(other.prior), next(other.next) {}
    ~State4() = delete;
    using F = typename State3::Next::Promise;
    union {
      [[no_unique_address]] State3 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F5>;
    [[no_unique_address]] Next next;
  };
  using FLast = typename State4::Next::Promise;
  union {
    [[no_unique_address]] State4 prior_;
    [[no_unique_address]] FLast f_;
  };
 public:
  Seq(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5) : prior_(std::move(f0), std::move(f1), std::move(f2), std::move(f3), std::move(f4), std::move(f5)) {}
  Seq& operator=(const Seq&) = delete;
  Seq(const Seq& other) {
    assert(other.state_ == 0);
    new (&prior_) State4(other.prior_);
  }
  Seq(Seq&& other) {
    assert(other.state_ == 0);
    new (&prior_) State4(std::move(other.prior_));
  }
  ~Seq() {
    switch (state_) {
     case 0:
      Destruct(&prior_.prior.prior.prior.prior.f);
      goto fin0;
     case 1:
      Destruct(&prior_.prior.prior.prior.f);
      goto fin1;
     case 2:
      Destruct(&prior_.prior.prior.f);
      goto fin2;
     case 3:
      Destruct(&prior_.prior.f);
      goto fin3;
     case 4:
      Destruct(&prior_.f);
      goto fin4;
     case 5:
      Destruct(&f_);
      return;
    }
  fin0:
    Destruct(&prior_.prior.prior.prior.prior.next);
  fin1:
    Destruct(&prior_.prior.prior.prior.next);
  fin2:
    Destruct(&prior_.prior.prior.next);
  fin3:
    Destruct(&prior_.prior.next);
  fin4:
    Destruct(&prior_.next);
  }
  decltype(std::declval<typename State4::Next::Promise>()()) operator()() {
    switch (state_) {
     case 0: {
      auto r = prior_.prior.prior.prior.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.prior.prior.prior.f);
      auto n = prior_.prior.prior.prior.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.prior.prior.prior.next);
      new (&prior_.prior.prior.prior.f) typename State0::Next::Promise(std::move(n));
      state_ = 1;
     }
     case 1: {
      auto r = prior_.prior.prior.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.prior.prior.f);
      auto n = prior_.prior.prior.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.prior.prior.next);
      new (&prior_.prior.prior.f) typename State1::Next::Promise(std::move(n));
      state_ = 2;
     }
     case 2: {
      auto r = prior_.prior.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.prior.f);
      auto n = prior_.prior.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.prior.next);
      new (&prior_.prior.f) typename State2::Next::Promise(std::move(n));
      state_ = 3;
     }
     case 3: {
      auto r = prior_.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.f);
      auto n = prior_.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.next);
      new (&prior_.f) typename State3::Next::Promise(std::move(n));
      state_ = 4;
     }
     case 4: {
      auto r = prior_.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.f);
      auto n = prior_.next.Once(std::move(*p));
      Destruct(&prior_.next);
      new (&f_) typename State4::Next::Promise(std::move(n));
      state_ = 5;
     }
     case 5:
      return f_();
    }
    return kPending;
  }
};
template <typename F0, typename F1, typename F2, typename F3, typename F4, typename F5, typename F6> class Seq<F0, F1, F2, F3, F4, F5, F6> {
 private:
  char state_ = 0;
  struct State0 {
    State0(F0&& f0, F1&& f1) : f(std::forward<F0>(f0)), next(std::forward<F1>(f1)) {}
    State0(State0&& other) : f(std::move(other.f)), next(std::move(other.next)) {}
    State0(const State0& other) : f(other.f), next(other.next) {}
    ~State0() = delete;
    using F = F0;
    [[no_unique_address]] F0 f;
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F1>;
    [[no_unique_address]] Next next;
  };
  struct State1 {
    State1(F0&& f0, F1&& f1, F2&& f2) : next(std::forward<F2>(f2)) { new (&prior) State0(std::forward<F0>(f0), std::forward<F1>(f1)); }
    State1(State1&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State1(const State1& other) : prior(other.prior), next(other.next) {}
    ~State1() = delete;
    using F = typename State0::Next::Promise;
    union {
      [[no_unique_address]] State0 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F2>;
    [[no_unique_address]] Next next;
  };
  struct State2 {
    State2(F0&& f0, F1&& f1, F2&& f2, F3&& f3) : next(std::forward<F3>(f3)) { new (&prior) State1(std::forward<F0>(f0), std::forward<F1>(f1), std::forward<F2>(f2)); }
    State2(State2&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State2(const State2& other) : prior(other.prior), next(other.next) {}
    ~State2() = delete;
    using F = typename State1::Next::Promise;
    union {
      [[no_unique_address]] State1 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F3>;
    [[no_unique_address]] Next next;
  };
  struct State3 {
    State3(F0&& f0, F1&& f1, F2&& f2, F3&& f3, F4&& f4) : next(std::forward<F4>(f4)) { new (&prior) State2(std::forward<F0>(f0), std::forward<F1>(f1), std::forward<F2>(f2), std::forward<F3>(f3)); }
    State3(State3&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State3(const State3& other) : prior(other.prior), next(other.next) {}
    ~State3() = delete;
    using F = typename State2::Next::Promise;
    union {
      [[no_unique_address]] State2 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F4>;
    [[no_unique_address]] Next next;
  };
  struct State4 {
    State4(F0&& f0, F1&& f1, F2&& f2, F3&& f3, F4&& f4, F5&& f5) : next(std::forward<F5>(f5)) { new (&prior) State3(std::forward<F0>(f0), std::forward<F1>(f1), std::forward<F2>(f2), std::forward<F3>(f3), std::forward<F4>(f4)); }
    State4(State4&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State4(const State4& other) : prior(other.prior), next(other.next) {}
    ~State4() = delete;
    using F = typename State3::Next::Promise;
    union {
      [[no_unique_address]] State3 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F5>;
    [[no_unique_address]] Next next;
  };
  struct State5 {
    State5(F0&& f0, F1&& f1, F2&& f2, F3&& f3, F4&& f4, F5&& f5, F6&& f6) : next(std::forward<F6>(f6)) { new (&prior) State4(std::forward<F0>(f0), std::forward<F1>(f1), std::forward<F2>(f2), std::forward<F3>(f3), std::forward<F4>(f4), std::forward<F5>(f5)); }
    State5(State5&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State5(const State5& other) : prior(other.prior), next(other.next) {}
    ~State5() = delete;
    using F = typename State4::Next::Promise;
    union {
      [[no_unique_address]] State4 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F6>;
    [[no_unique_address]] Next next;
  };
  using FLast = typename State5::Next::Promise;
  union {
    [[no_unique_address]] State5 prior_;
    [[no_unique_address]] FLast f_;
  };
 public:
  Seq(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6) : prior_(std::move(f0), std::move(f1), std::move(f2), std::move(f3), std::move(f4), std::move(f5), std::move(f6)) {}
  Seq& operator=(const Seq&) = delete;
  Seq(const Seq& other) {
    assert(other.state_ == 0);
    new (&prior_) State5(other.prior_);
  }
  Seq(Seq&& other) {
    assert(other.state_ == 0);
    new (&prior_) State5(std::move(other.prior_));
  }
  ~Seq() {
    switch (state_) {
     case 0:
      Destruct(&prior_.prior.prior.prior.prior.prior.f);
      goto fin0;
     case 1:
      Destruct(&prior_.prior.prior.prior.prior.f);
      goto fin1;
     case 2:
      Destruct(&prior_.prior.prior.prior.f);
      goto fin2;
     case 3:
      Destruct(&prior_.prior.prior.f);
      goto fin3;
     case 4:
      Destruct(&prior_.prior.f);
      goto fin4;
     case 5:
      Destruct(&prior_.f);
      goto fin5;
     case 6:
      Destruct(&f_);
      return;
    }
  fin0:
    Destruct(&prior_.prior.prior.prior.prior.prior.next);
  fin1:
    Destruct(&prior_.prior.prior.prior.prior.next);
  fin2:
    Destruct(&prior_.prior.prior.prior.next);
  fin3:
    Destruct(&prior_.prior.prior.next);
  fin4:
    Destruct(&prior_.prior.next);
  fin5:
    Destruct(&prior_.next);
  }
  decltype(std::declval<typename State5::Next::Promise>()()) operator()() {
    switch (state_) {
     case 0: {
      auto r = prior_.prior.prior.prior.prior.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.prior.prior.prior.prior.f);
      auto n = prior_.prior.prior.prior.prior.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.prior.prior.prior.prior.next);
      new (&prior_.prior.prior.prior.prior.f) typename State0::Next::Promise(std::move(n));
      state_ = 1;
     }
     case 1: {
      auto r = prior_.prior.prior.prior.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.prior.prior.prior.f);
      auto n = prior_.prior.prior.prior.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.prior.prior.prior.next);
      new (&prior_.prior.prior.prior.f) typename State1::Next::Promise(std::move(n));
      state_ = 2;
     }
     case 2: {
      auto r = prior_.prior.prior.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.prior.prior.f);
      auto n = prior_.prior.prior.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.prior.prior.next);
      new (&prior_.prior.prior.f) typename State2::Next::Promise(std::move(n));
      state_ = 3;
     }
     case 3: {
      auto r = prior_.prior.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.prior.f);
      auto n = prior_.prior.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.prior.next);
      new (&prior_.prior.f) typename State3::Next::Promise(std::move(n));
      state_ = 4;
     }
     case 4: {
      auto r = prior_.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.f);
      auto n = prior_.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.next);
      new (&prior_.f) typename State4::Next::Promise(std::move(n));
      state_ = 5;
     }
     case 5: {
      auto r = prior_.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.f);
      auto n = prior_.next.Once(std::move(*p));
      Destruct(&prior_.next);
      new (&f_) typename State5::Next::Promise(std::move(n));
      state_ = 6;
     }
     case 6:
      return f_();
    }
    return kPending;
  }
};
template <typename F0, typename F1, typename F2, typename F3, typename F4, typename F5, typename F6, typename F7> class Seq<F0, F1, F2, F3, F4, F5, F6, F7> {
 private:
  char state_ = 0;
  struct State0 {
    State0(F0&& f0, F1&& f1) : f(std::forward<F0>(f0)), next(std::forward<F1>(f1)) {}
    State0(State0&& other) : f(std::move(other.f)), next(std::move(other.next)) {}
    State0(const State0& other) : f(other.f), next(other.next) {}
    ~State0() = delete;
    using F = F0;
    [[no_unique_address]] F0 f;
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F1>;
    [[no_unique_address]] Next next;
  };
  struct State1 {
    State1(F0&& f0, F1&& f1, F2&& f2) : next(std::forward<F2>(f2)) { new (&prior) State0(std::forward<F0>(f0), std::forward<F1>(f1)); }
    State1(State1&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State1(const State1& other) : prior(other.prior), next(other.next) {}
    ~State1() = delete;
    using F = typename State0::Next::Promise;
    union {
      [[no_unique_address]] State0 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F2>;
    [[no_unique_address]] Next next;
  };
  struct State2 {
    State2(F0&& f0, F1&& f1, F2&& f2, F3&& f3) : next(std::forward<F3>(f3)) { new (&prior) State1(std::forward<F0>(f0), std::forward<F1>(f1), std::forward<F2>(f2)); }
    State2(State2&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State2(const State2& other) : prior(other.prior), next(other.next) {}
    ~State2() = delete;
    using F = typename State1::Next::Promise;
    union {
      [[no_unique_address]] State1 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F3>;
    [[no_unique_address]] Next next;
  };
  struct State3 {
    State3(F0&& f0, F1&& f1, F2&& f2, F3&& f3, F4&& f4) : next(std::forward<F4>(f4)) { new (&prior) State2(std::forward<F0>(f0), std::forward<F1>(f1), std::forward<F2>(f2), std::forward<F3>(f3)); }
    State3(State3&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State3(const State3& other) : prior(other.prior), next(other.next) {}
    ~State3() = delete;
    using F = typename State2::Next::Promise;
    union {
      [[no_unique_address]] State2 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F4>;
    [[no_unique_address]] Next next;
  };
  struct State4 {
    State4(F0&& f0, F1&& f1, F2&& f2, F3&& f3, F4&& f4, F5&& f5) : next(std::forward<F5>(f5)) { new (&prior) State3(std::forward<F0>(f0), std::forward<F1>(f1), std::forward<F2>(f2), std::forward<F3>(f3), std::forward<F4>(f4)); }
    State4(State4&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State4(const State4& other) : prior(other.prior), next(other.next) {}
    ~State4() = delete;
    using F = typename State3::Next::Promise;
    union {
      [[no_unique_address]] State3 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F5>;
    [[no_unique_address]] Next next;
  };
  struct State5 {
    State5(F0&& f0, F1&& f1, F2&& f2, F3&& f3, F4&& f4, F5&& f5, F6&& f6) : next(std::forward<F6>(f6)) { new (&prior) State4(std::forward<F0>(f0), std::forward<F1>(f1), std::forward<F2>(f2), std::forward<F3>(f3), std::forward<F4>(f4), std::forward<F5>(f5)); }
    State5(State5&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State5(const State5& other) : prior(other.prior), next(other.next) {}
    ~State5() = delete;
    using F = typename State4::Next::Promise;
    union {
      [[no_unique_address]] State4 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F6>;
    [[no_unique_address]] Next next;
  };
  struct State6 {
    State6(F0&& f0, F1&& f1, F2&& f2, F3&& f3, F4&& f4, F5&& f5, F6&& f6, F7&& f7) : next(std::forward<F7>(f7)) { new (&prior) State5(std::forward<F0>(f0), std::forward<F1>(f1), std::forward<F2>(f2), std::forward<F3>(f3), std::forward<F4>(f4), std::forward<F5>(f5), std::forward<F6>(f6)); }
    State6(State6&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State6(const State6& other) : prior(other.prior), next(other.next) {}
    ~State6() = delete;
    using F = typename State5::Next::Promise;
    union {
      [[no_unique_address]] State5 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F7>;
    [[no_unique_address]] Next next;
  };
  using FLast = typename State6::Next::Promise;
  union {
    [[no_unique_address]] State6 prior_;
    [[no_unique_address]] FLast f_;
  };
 public:
  Seq(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7) : prior_(std::move(f0), std::move(f1), std::move(f2), std::move(f3), std::move(f4), std::move(f5), std::move(f6), std::move(f7)) {}
  Seq& operator=(const Seq&) = delete;
  Seq(const Seq& other) {
    assert(other.state_ == 0);
    new (&prior_) State6(other.prior_);
  }
  Seq(Seq&& other) {
    assert(other.state_ == 0);
    new (&prior_) State6(std::move(other.prior_));
  }
  ~Seq() {
    switch (state_) {
     case 0:
      Destruct(&prior_.prior.prior.prior.prior.prior.prior.f);
      goto fin0;
     case 1:
      Destruct(&prior_.prior.prior.prior.prior.prior.f);
      goto fin1;
     case 2:
      Destruct(&prior_.prior.prior.prior.prior.f);
      goto fin2;
     case 3:
      Destruct(&prior_.prior.prior.prior.f);
      goto fin3;
     case 4:
      Destruct(&prior_.prior.prior.f);
      goto fin4;
     case 5:
      Destruct(&prior_.prior.f);
      goto fin5;
     case 6:
      Destruct(&prior_.f);
      goto fin6;
     case 7:
      Destruct(&f_);
      return;
    }
  fin0:
    Destruct(&prior_.prior.prior.prior.prior.prior.prior.next);
  fin1:
    Destruct(&prior_.prior.prior.prior.prior.prior.next);
  fin2:
    Destruct(&prior_.prior.prior.prior.prior.next);
  fin3:
    Destruct(&prior_.prior.prior.prior.next);
  fin4:
    Destruct(&prior_.prior.prior.next);
  fin5:
    Destruct(&prior_.prior.next);
  fin6:
    Destruct(&prior_.next);
  }
  decltype(std::declval<typename State6::Next::Promise>()()) operator()() {
    switch (state_) {
     case 0: {
      auto r = prior_.prior.prior.prior.prior.prior.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.prior.prior.prior.prior.prior.f);
      auto n = prior_.prior.prior.prior.prior.prior.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.prior.prior.prior.prior.prior.next);
      new (&prior_.prior.prior.prior.prior.prior.f) typename State0::Next::Promise(std::move(n));
      state_ = 1;
     }
     case 1: {
      auto r = prior_.prior.prior.prior.prior.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.prior.prior.prior.prior.f);
      auto n = prior_.prior.prior.prior.prior.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.prior.prior.prior.prior.next);
      new (&prior_.prior.prior.prior.prior.f) typename State1::Next::Promise(std::move(n));
      state_ = 2;
     }
     case 2: {
      auto r = prior_.prior.prior.prior.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.prior.prior.prior.f);
      auto n = prior_.prior.prior.prior.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.prior.prior.prior.next);
      new (&prior_.prior.prior.prior.f) typename State2::Next::Promise(std::move(n));
      state_ = 3;
     }
     case 3: {
      auto r = prior_.prior.prior.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.prior.prior.f);
      auto n = prior_.prior.prior.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.prior.prior.next);
      new (&prior_.prior.prior.f) typename State3::Next::Promise(std::move(n));
      state_ = 4;
     }
     case 4: {
      auto r = prior_.prior.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.prior.f);
      auto n = prior_.prior.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.prior.next);
      new (&prior_.prior.f) typename State4::Next::Promise(std::move(n));
      state_ = 5;
     }
     case 5: {
      auto r = prior_.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.f);
      auto n = prior_.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.next);
      new (&prior_.f) typename State5::Next::Promise(std::move(n));
      state_ = 6;
     }
     case 6: {
      auto r = prior_.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.f);
      auto n = prior_.next.Once(std::move(*p));
      Destruct(&prior_.next);
      new (&f_) typename State6::Next::Promise(std::move(n));
      state_ = 7;
     }
     case 7:
      return f_();
    }
    return kPending;
  }
};
template <typename F0, typename F1, typename F2, typename F3, typename F4, typename F5, typename F6, typename F7, typename F8> class Seq<F0, F1, F2, F3, F4, F5, F6, F7, F8> {
 private:
  char state_ = 0;
  struct State0 {
    State0(F0&& f0, F1&& f1) : f(std::forward<F0>(f0)), next(std::forward<F1>(f1)) {}
    State0(State0&& other) : f(std::move(other.f)), next(std::move(other.next)) {}
    State0(const State0& other) : f(other.f), next(other.next) {}
    ~State0() = delete;
    using F = F0;
    [[no_unique_address]] F0 f;
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F1>;
    [[no_unique_address]] Next next;
  };
  struct State1 {
    State1(F0&& f0, F1&& f1, F2&& f2) : next(std::forward<F2>(f2)) { new (&prior) State0(std::forward<F0>(f0), std::forward<F1>(f1)); }
    State1(State1&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State1(const State1& other) : prior(other.prior), next(other.next) {}
    ~State1() = delete;
    using F = typename State0::Next::Promise;
    union {
      [[no_unique_address]] State0 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F2>;
    [[no_unique_address]] Next next;
  };
  struct State2 {
    State2(F0&& f0, F1&& f1, F2&& f2, F3&& f3) : next(std::forward<F3>(f3)) { new (&prior) State1(std::forward<F0>(f0), std::forward<F1>(f1), std::forward<F2>(f2)); }
    State2(State2&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State2(const State2& other) : prior(other.prior), next(other.next) {}
    ~State2() = delete;
    using F = typename State1::Next::Promise;
    union {
      [[no_unique_address]] State1 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F3>;
    [[no_unique_address]] Next next;
  };
  struct State3 {
    State3(F0&& f0, F1&& f1, F2&& f2, F3&& f3, F4&& f4) : next(std::forward<F4>(f4)) { new (&prior) State2(std::forward<F0>(f0), std::forward<F1>(f1), std::forward<F2>(f2), std::forward<F3>(f3)); }
    State3(State3&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State3(const State3& other) : prior(other.prior), next(other.next) {}
    ~State3() = delete;
    using F = typename State2::Next::Promise;
    union {
      [[no_unique_address]] State2 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F4>;
    [[no_unique_address]] Next next;
  };
  struct State4 {
    State4(F0&& f0, F1&& f1, F2&& f2, F3&& f3, F4&& f4, F5&& f5) : next(std::forward<F5>(f5)) { new (&prior) State3(std::forward<F0>(f0), std::forward<F1>(f1), std::forward<F2>(f2), std::forward<F3>(f3), std::forward<F4>(f4)); }
    State4(State4&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State4(const State4& other) : prior(other.prior), next(other.next) {}
    ~State4() = delete;
    using F = typename State3::Next::Promise;
    union {
      [[no_unique_address]] State3 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F5>;
    [[no_unique_address]] Next next;
  };
  struct State5 {
    State5(F0&& f0, F1&& f1, F2&& f2, F3&& f3, F4&& f4, F5&& f5, F6&& f6) : next(std::forward<F6>(f6)) { new (&prior) State4(std::forward<F0>(f0), std::forward<F1>(f1), std::forward<F2>(f2), std::forward<F3>(f3), std::forward<F4>(f4), std::forward<F5>(f5)); }
    State5(State5&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State5(const State5& other) : prior(other.prior), next(other.next) {}
    ~State5() = delete;
    using F = typename State4::Next::Promise;
    union {
      [[no_unique_address]] State4 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F6>;
    [[no_unique_address]] Next next;
  };
  struct State6 {
    State6(F0&& f0, F1&& f1, F2&& f2, F3&& f3, F4&& f4, F5&& f5, F6&& f6, F7&& f7) : next(std::forward<F7>(f7)) { new (&prior) State5(std::forward<F0>(f0), std::forward<F1>(f1), std::forward<F2>(f2), std::forward<F3>(f3), std::forward<F4>(f4), std::forward<F5>(f5), std::forward<F6>(f6)); }
    State6(State6&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State6(const State6& other) : prior(other.prior), next(other.next) {}
    ~State6() = delete;
    using F = typename State5::Next::Promise;
    union {
      [[no_unique_address]] State5 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F7>;
    [[no_unique_address]] Next next;
  };
  struct State7 {
    State7(F0&& f0, F1&& f1, F2&& f2, F3&& f3, F4&& f4, F5&& f5, F6&& f6, F7&& f7, F8&& f8) : next(std::forward<F8>(f8)) { new (&prior) State6(std::forward<F0>(f0), std::forward<F1>(f1), std::forward<F2>(f2), std::forward<F3>(f3), std::forward<F4>(f4), std::forward<F5>(f5), std::forward<F6>(f6), std::forward<F7>(f7)); }
    State7(State7&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State7(const State7& other) : prior(other.prior), next(other.next) {}
    ~State7() = delete;
    using F = typename State6::Next::Promise;
    union {
      [[no_unique_address]] State6 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F8>;
    [[no_unique_address]] Next next;
  };
  using FLast = typename State7::Next::Promise;
  union {
    [[no_unique_address]] State7 prior_;
    [[no_unique_address]] FLast f_;
  };
 public:
  Seq(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7, F8 f8) : prior_(std::move(f0), std::move(f1), std::move(f2), std::move(f3), std::move(f4), std::move(f5), std::move(f6), std::move(f7), std::move(f8)) {}
  Seq& operator=(const Seq&) = delete;
  Seq(const Seq& other) {
    assert(other.state_ == 0);
    new (&prior_) State7(other.prior_);
  }
  Seq(Seq&& other) {
    assert(other.state_ == 0);
    new (&prior_) State7(std::move(other.prior_));
  }
  ~Seq() {
    switch (state_) {
     case 0:
      Destruct(&prior_.prior.prior.prior.prior.prior.prior.prior.f);
      goto fin0;
     case 1:
      Destruct(&prior_.prior.prior.prior.prior.prior.prior.f);
      goto fin1;
     case 2:
      Destruct(&prior_.prior.prior.prior.prior.prior.f);
      goto fin2;
     case 3:
      Destruct(&prior_.prior.prior.prior.prior.f);
      goto fin3;
     case 4:
      Destruct(&prior_.prior.prior.prior.f);
      goto fin4;
     case 5:
      Destruct(&prior_.prior.prior.f);
      goto fin5;
     case 6:
      Destruct(&prior_.prior.f);
      goto fin6;
     case 7:
      Destruct(&prior_.f);
      goto fin7;
     case 8:
      Destruct(&f_);
      return;
    }
  fin0:
    Destruct(&prior_.prior.prior.prior.prior.prior.prior.prior.next);
  fin1:
    Destruct(&prior_.prior.prior.prior.prior.prior.prior.next);
  fin2:
    Destruct(&prior_.prior.prior.prior.prior.prior.next);
  fin3:
    Destruct(&prior_.prior.prior.prior.prior.next);
  fin4:
    Destruct(&prior_.prior.prior.prior.next);
  fin5:
    Destruct(&prior_.prior.prior.next);
  fin6:
    Destruct(&prior_.prior.next);
  fin7:
    Destruct(&prior_.next);
  }
  decltype(std::declval<typename State7::Next::Promise>()()) operator()() {
    switch (state_) {
     case 0: {
      auto r = prior_.prior.prior.prior.prior.prior.prior.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.prior.prior.prior.prior.prior.prior.f);
      auto n = prior_.prior.prior.prior.prior.prior.prior.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.prior.prior.prior.prior.prior.prior.next);
      new (&prior_.prior.prior.prior.prior.prior.prior.f) typename State0::Next::Promise(std::move(n));
      state_ = 1;
     }
     case 1: {
      auto r = prior_.prior.prior.prior.prior.prior.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.prior.prior.prior.prior.prior.f);
      auto n = prior_.prior.prior.prior.prior.prior.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.prior.prior.prior.prior.prior.next);
      new (&prior_.prior.prior.prior.prior.prior.f) typename State1::Next::Promise(std::move(n));
      state_ = 2;
     }
     case 2: {
      auto r = prior_.prior.prior.prior.prior.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.prior.prior.prior.prior.f);
      auto n = prior_.prior.prior.prior.prior.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.prior.prior.prior.prior.next);
      new (&prior_.prior.prior.prior.prior.f) typename State2::Next::Promise(std::move(n));
      state_ = 3;
     }
     case 3: {
      auto r = prior_.prior.prior.prior.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.prior.prior.prior.f);
      auto n = prior_.prior.prior.prior.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.prior.prior.prior.next);
      new (&prior_.prior.prior.prior.f) typename State3::Next::Promise(std::move(n));
      state_ = 4;
     }
     case 4: {
      auto r = prior_.prior.prior.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.prior.prior.f);
      auto n = prior_.prior.prior.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.prior.prior.next);
      new (&prior_.prior.prior.f) typename State4::Next::Promise(std::move(n));
      state_ = 5;
     }
     case 5: {
      auto r = prior_.prior.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.prior.f);
      auto n = prior_.prior.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.prior.next);
      new (&prior_.prior.f) typename State5::Next::Promise(std::move(n));
      state_ = 6;
     }
     case 6: {
      auto r = prior_.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.f);
      auto n = prior_.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.next);
      new (&prior_.f) typename State6::Next::Promise(std::move(n));
      state_ = 7;
     }
     case 7: {
      auto r = prior_.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.f);
      auto n = prior_.next.Once(std::move(*p));
      Destruct(&prior_.next);
      new (&f_) typename State7::Next::Promise(std::move(n));
      state_ = 8;
     }
     case 8:
      return f_();
    }
    return kPending;
  }
};
template <typename F0, typename F1, typename F2, typename F3, typename F4, typename F5, typename F6, typename F7, typename F8, typename F9> class Seq<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9> {
 private:
  char state_ = 0;
  struct State0 {
    State0(F0&& f0, F1&& f1) : f(std::forward<F0>(f0)), next(std::forward<F1>(f1)) {}
    State0(State0&& other) : f(std::move(other.f)), next(std::move(other.next)) {}
    State0(const State0& other) : f(other.f), next(other.next) {}
    ~State0() = delete;
    using F = F0;
    [[no_unique_address]] F0 f;
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F1>;
    [[no_unique_address]] Next next;
  };
  struct State1 {
    State1(F0&& f0, F1&& f1, F2&& f2) : next(std::forward<F2>(f2)) { new (&prior) State0(std::forward<F0>(f0), std::forward<F1>(f1)); }
    State1(State1&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State1(const State1& other) : prior(other.prior), next(other.next) {}
    ~State1() = delete;
    using F = typename State0::Next::Promise;
    union {
      [[no_unique_address]] State0 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F2>;
    [[no_unique_address]] Next next;
  };
  struct State2 {
    State2(F0&& f0, F1&& f1, F2&& f2, F3&& f3) : next(std::forward<F3>(f3)) { new (&prior) State1(std::forward<F0>(f0), std::forward<F1>(f1), std::forward<F2>(f2)); }
    State2(State2&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State2(const State2& other) : prior(other.prior), next(other.next) {}
    ~State2() = delete;
    using F = typename State1::Next::Promise;
    union {
      [[no_unique_address]] State1 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F3>;
    [[no_unique_address]] Next next;
  };
  struct State3 {
    State3(F0&& f0, F1&& f1, F2&& f2, F3&& f3, F4&& f4) : next(std::forward<F4>(f4)) { new (&prior) State2(std::forward<F0>(f0), std::forward<F1>(f1), std::forward<F2>(f2), std::forward<F3>(f3)); }
    State3(State3&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State3(const State3& other) : prior(other.prior), next(other.next) {}
    ~State3() = delete;
    using F = typename State2::Next::Promise;
    union {
      [[no_unique_address]] State2 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F4>;
    [[no_unique_address]] Next next;
  };
  struct State4 {
    State4(F0&& f0, F1&& f1, F2&& f2, F3&& f3, F4&& f4, F5&& f5) : next(std::forward<F5>(f5)) { new (&prior) State3(std::forward<F0>(f0), std::forward<F1>(f1), std::forward<F2>(f2), std::forward<F3>(f3), std::forward<F4>(f4)); }
    State4(State4&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State4(const State4& other) : prior(other.prior), next(other.next) {}
    ~State4() = delete;
    using F = typename State3::Next::Promise;
    union {
      [[no_unique_address]] State3 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F5>;
    [[no_unique_address]] Next next;
  };
  struct State5 {
    State5(F0&& f0, F1&& f1, F2&& f2, F3&& f3, F4&& f4, F5&& f5, F6&& f6) : next(std::forward<F6>(f6)) { new (&prior) State4(std::forward<F0>(f0), std::forward<F1>(f1), std::forward<F2>(f2), std::forward<F3>(f3), std::forward<F4>(f4), std::forward<F5>(f5)); }
    State5(State5&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State5(const State5& other) : prior(other.prior), next(other.next) {}
    ~State5() = delete;
    using F = typename State4::Next::Promise;
    union {
      [[no_unique_address]] State4 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F6>;
    [[no_unique_address]] Next next;
  };
  struct State6 {
    State6(F0&& f0, F1&& f1, F2&& f2, F3&& f3, F4&& f4, F5&& f5, F6&& f6, F7&& f7) : next(std::forward<F7>(f7)) { new (&prior) State5(std::forward<F0>(f0), std::forward<F1>(f1), std::forward<F2>(f2), std::forward<F3>(f3), std::forward<F4>(f4), std::forward<F5>(f5), std::forward<F6>(f6)); }
    State6(State6&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State6(const State6& other) : prior(other.prior), next(other.next) {}
    ~State6() = delete;
    using F = typename State5::Next::Promise;
    union {
      [[no_unique_address]] State5 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F7>;
    [[no_unique_address]] Next next;
  };
  struct State7 {
    State7(F0&& f0, F1&& f1, F2&& f2, F3&& f3, F4&& f4, F5&& f5, F6&& f6, F7&& f7, F8&& f8) : next(std::forward<F8>(f8)) { new (&prior) State6(std::forward<F0>(f0), std::forward<F1>(f1), std::forward<F2>(f2), std::forward<F3>(f3), std::forward<F4>(f4), std::forward<F5>(f5), std::forward<F6>(f6), std::forward<F7>(f7)); }
    State7(State7&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State7(const State7& other) : prior(other.prior), next(other.next) {}
    ~State7() = delete;
    using F = typename State6::Next::Promise;
    union {
      [[no_unique_address]] State6 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F8>;
    [[no_unique_address]] Next next;
  };
  struct State8 {
    State8(F0&& f0, F1&& f1, F2&& f2, F3&& f3, F4&& f4, F5&& f5, F6&& f6, F7&& f7, F8&& f8, F9&& f9) : next(std::forward<F9>(f9)) { new (&prior) State7(std::forward<F0>(f0), std::forward<F1>(f1), std::forward<F2>(f2), std::forward<F3>(f3), std::forward<F4>(f4), std::forward<F5>(f5), std::forward<F6>(f6), std::forward<F7>(f7), std::forward<F8>(f8)); }
    State8(State8&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}
    State8(const State8& other) : prior(other.prior), next(other.next) {}
    ~State8() = delete;
    using F = typename State7::Next::Promise;
    union {
      [[no_unique_address]] State7 prior;
      [[no_unique_address]] F f;
    };
    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
    using Next = adaptor_detail::Factory<FResult, F9>;
    [[no_unique_address]] Next next;
  };
  using FLast = typename State8::Next::Promise;
  union {
    [[no_unique_address]] State8 prior_;
    [[no_unique_address]] FLast f_;
  };
 public:
  Seq(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7, F8 f8, F9 f9) : prior_(std::move(f0), std::move(f1), std::move(f2), std::move(f3), std::move(f4), std::move(f5), std::move(f6), std::move(f7), std::move(f8), std::move(f9)) {}
  Seq& operator=(const Seq&) = delete;
  Seq(const Seq& other) {
    assert(other.state_ == 0);
    new (&prior_) State8(other.prior_);
  }
  Seq(Seq&& other) {
    assert(other.state_ == 0);
    new (&prior_) State8(std::move(other.prior_));
  }
  ~Seq() {
    switch (state_) {
     case 0:
      Destruct(&prior_.prior.prior.prior.prior.prior.prior.prior.prior.f);
      goto fin0;
     case 1:
      Destruct(&prior_.prior.prior.prior.prior.prior.prior.prior.f);
      goto fin1;
     case 2:
      Destruct(&prior_.prior.prior.prior.prior.prior.prior.f);
      goto fin2;
     case 3:
      Destruct(&prior_.prior.prior.prior.prior.prior.f);
      goto fin3;
     case 4:
      Destruct(&prior_.prior.prior.prior.prior.f);
      goto fin4;
     case 5:
      Destruct(&prior_.prior.prior.prior.f);
      goto fin5;
     case 6:
      Destruct(&prior_.prior.prior.f);
      goto fin6;
     case 7:
      Destruct(&prior_.prior.f);
      goto fin7;
     case 8:
      Destruct(&prior_.f);
      goto fin8;
     case 9:
      Destruct(&f_);
      return;
    }
  fin0:
    Destruct(&prior_.prior.prior.prior.prior.prior.prior.prior.prior.next);
  fin1:
    Destruct(&prior_.prior.prior.prior.prior.prior.prior.prior.next);
  fin2:
    Destruct(&prior_.prior.prior.prior.prior.prior.prior.next);
  fin3:
    Destruct(&prior_.prior.prior.prior.prior.prior.next);
  fin4:
    Destruct(&prior_.prior.prior.prior.prior.next);
  fin5:
    Destruct(&prior_.prior.prior.prior.next);
  fin6:
    Destruct(&prior_.prior.prior.next);
  fin7:
    Destruct(&prior_.prior.next);
  fin8:
    Destruct(&prior_.next);
  }
  decltype(std::declval<typename State8::Next::Promise>()()) operator()() {
    switch (state_) {
     case 0: {
      auto r = prior_.prior.prior.prior.prior.prior.prior.prior.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.prior.prior.prior.prior.prior.prior.prior.f);
      auto n = prior_.prior.prior.prior.prior.prior.prior.prior.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.prior.prior.prior.prior.prior.prior.prior.next);
      new (&prior_.prior.prior.prior.prior.prior.prior.prior.f) typename State0::Next::Promise(std::move(n));
      state_ = 1;
     }
     case 1: {
      auto r = prior_.prior.prior.prior.prior.prior.prior.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.prior.prior.prior.prior.prior.prior.f);
      auto n = prior_.prior.prior.prior.prior.prior.prior.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.prior.prior.prior.prior.prior.prior.next);
      new (&prior_.prior.prior.prior.prior.prior.prior.f) typename State1::Next::Promise(std::move(n));
      state_ = 2;
     }
     case 2: {
      auto r = prior_.prior.prior.prior.prior.prior.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.prior.prior.prior.prior.prior.f);
      auto n = prior_.prior.prior.prior.prior.prior.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.prior.prior.prior.prior.prior.next);
      new (&prior_.prior.prior.prior.prior.prior.f) typename State2::Next::Promise(std::move(n));
      state_ = 3;
     }
     case 3: {
      auto r = prior_.prior.prior.prior.prior.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.prior.prior.prior.prior.f);
      auto n = prior_.prior.prior.prior.prior.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.prior.prior.prior.prior.next);
      new (&prior_.prior.prior.prior.prior.f) typename State3::Next::Promise(std::move(n));
      state_ = 4;
     }
     case 4: {
      auto r = prior_.prior.prior.prior.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.prior.prior.prior.f);
      auto n = prior_.prior.prior.prior.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.prior.prior.prior.next);
      new (&prior_.prior.prior.prior.f) typename State4::Next::Promise(std::move(n));
      state_ = 5;
     }
     case 5: {
      auto r = prior_.prior.prior.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.prior.prior.f);
      auto n = prior_.prior.prior.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.prior.prior.next);
      new (&prior_.prior.prior.f) typename State5::Next::Promise(std::move(n));
      state_ = 6;
     }
     case 6: {
      auto r = prior_.prior.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.prior.f);
      auto n = prior_.prior.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.prior.next);
      new (&prior_.prior.f) typename State6::Next::Promise(std::move(n));
      state_ = 7;
     }
     case 7: {
      auto r = prior_.prior.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.prior.f);
      auto n = prior_.prior.next.Once(std::move(*p));
      Destruct(&prior_.prior.next);
      new (&prior_.f) typename State7::Next::Promise(std::move(n));
      state_ = 8;
     }
     case 8: {
      auto r = prior_.f();
      auto* p = r.get_ready();
      if (p == nullptr) break;
      Destruct(&prior_.f);
      auto n = prior_.next.Once(std::move(*p));
      Destruct(&prior_.next);
      new (&f_) typename State8::Next::Promise(std::move(n));
      state_ = 9;
     }
     case 9:
      return f_();
    }
    return kPending;
  }
};
