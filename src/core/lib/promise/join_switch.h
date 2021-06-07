template <typename F0> class Join<F0> {
 private:
  [[no_unique_address]] uint8_t state_ = 0;
  using R0 = typename decltype(std::declval<F0>()())::Type;
  union { [[no_unique_address]] F0 pending0_; [[no_unique_address]] R0 ready0_; };
 public:
  Join(F0 f0) : pending0_(std::move(f0)) {}
  Join& operator=(const Join&) = delete;
  Join(const Join& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
  }
  Join(Join&& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
  }
  ~Join() {
    if (state_ & 1) Destruct(&ready0_); else Destruct(&pending0_);
  }
  Poll<std::tuple<R0>> operator()() {
    if ((state_ & 1) == 0) {
      auto r = pending0_();
      if (auto* p = r.get_ready()) { state_ |= 1; Destruct(&pending0_); Construct(&ready0_, std::move(*p)); }
    }
    if (state_ != 1) return kPending;
    return ready(std::tuple<R0>(std::move(ready0_)));
  }
};
template <typename F0, typename F1> class Join<F0, F1> {
 private:
  [[no_unique_address]] uint8_t state_ = 0;
  using R0 = typename decltype(std::declval<F0>()())::Type;
  union { [[no_unique_address]] F0 pending0_; [[no_unique_address]] R0 ready0_; };
  using R1 = typename decltype(std::declval<F1>()())::Type;
  union { [[no_unique_address]] F1 pending1_; [[no_unique_address]] R1 ready1_; };
 public:
  Join(F0 f0, F1 f1) : pending0_(std::move(f0)), pending1_(std::move(f1)) {}
  Join& operator=(const Join&) = delete;
  Join(const Join& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
  }
  Join(Join&& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
  }
  ~Join() {
    if (state_ & 1) Destruct(&ready0_); else Destruct(&pending0_);
    if (state_ & 2) Destruct(&ready1_); else Destruct(&pending1_);
  }
  Poll<std::tuple<R0, R1>> operator()() {
    if ((state_ & 1) == 0) {
      auto r = pending0_();
      if (auto* p = r.get_ready()) { state_ |= 1; Destruct(&pending0_); Construct(&ready0_, std::move(*p)); }
    }
    if ((state_ & 2) == 0) {
      auto r = pending1_();
      if (auto* p = r.get_ready()) { state_ |= 2; Destruct(&pending1_); Construct(&ready1_, std::move(*p)); }
    }
    if (state_ != 3) return kPending;
    return ready(std::tuple<R0, R1>(std::move(ready0_), std::move(ready1_)));
  }
};
template <typename F0, typename F1, typename F2> class Join<F0, F1, F2> {
 private:
  [[no_unique_address]] uint8_t state_ = 0;
  using R0 = typename decltype(std::declval<F0>()())::Type;
  union { [[no_unique_address]] F0 pending0_; [[no_unique_address]] R0 ready0_; };
  using R1 = typename decltype(std::declval<F1>()())::Type;
  union { [[no_unique_address]] F1 pending1_; [[no_unique_address]] R1 ready1_; };
  using R2 = typename decltype(std::declval<F2>()())::Type;
  union { [[no_unique_address]] F2 pending2_; [[no_unique_address]] R2 ready2_; };
 public:
  Join(F0 f0, F1 f1, F2 f2) : pending0_(std::move(f0)), pending1_(std::move(f1)), pending2_(std::move(f2)) {}
  Join& operator=(const Join&) = delete;
  Join(const Join& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
  }
  Join(Join&& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
  }
  ~Join() {
    if (state_ & 1) Destruct(&ready0_); else Destruct(&pending0_);
    if (state_ & 2) Destruct(&ready1_); else Destruct(&pending1_);
    if (state_ & 4) Destruct(&ready2_); else Destruct(&pending2_);
  }
  Poll<std::tuple<R0, R1, R2>> operator()() {
    if ((state_ & 1) == 0) {
      auto r = pending0_();
      if (auto* p = r.get_ready()) { state_ |= 1; Destruct(&pending0_); Construct(&ready0_, std::move(*p)); }
    }
    if ((state_ & 2) == 0) {
      auto r = pending1_();
      if (auto* p = r.get_ready()) { state_ |= 2; Destruct(&pending1_); Construct(&ready1_, std::move(*p)); }
    }
    if ((state_ & 4) == 0) {
      auto r = pending2_();
      if (auto* p = r.get_ready()) { state_ |= 4; Destruct(&pending2_); Construct(&ready2_, std::move(*p)); }
    }
    if (state_ != 7) return kPending;
    return ready(std::tuple<R0, R1, R2>(std::move(ready0_), std::move(ready1_), std::move(ready2_)));
  }
};
template <typename F0, typename F1, typename F2, typename F3> class Join<F0, F1, F2, F3> {
 private:
  [[no_unique_address]] uint8_t state_ = 0;
  using R0 = typename decltype(std::declval<F0>()())::Type;
  union { [[no_unique_address]] F0 pending0_; [[no_unique_address]] R0 ready0_; };
  using R1 = typename decltype(std::declval<F1>()())::Type;
  union { [[no_unique_address]] F1 pending1_; [[no_unique_address]] R1 ready1_; };
  using R2 = typename decltype(std::declval<F2>()())::Type;
  union { [[no_unique_address]] F2 pending2_; [[no_unique_address]] R2 ready2_; };
  using R3 = typename decltype(std::declval<F3>()())::Type;
  union { [[no_unique_address]] F3 pending3_; [[no_unique_address]] R3 ready3_; };
 public:
  Join(F0 f0, F1 f1, F2 f2, F3 f3) : pending0_(std::move(f0)), pending1_(std::move(f1)), pending2_(std::move(f2)), pending3_(std::move(f3)) {}
  Join& operator=(const Join&) = delete;
  Join(const Join& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
  }
  Join(Join&& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
  }
  ~Join() {
    if (state_ & 1) Destruct(&ready0_); else Destruct(&pending0_);
    if (state_ & 2) Destruct(&ready1_); else Destruct(&pending1_);
    if (state_ & 4) Destruct(&ready2_); else Destruct(&pending2_);
    if (state_ & 8) Destruct(&ready3_); else Destruct(&pending3_);
  }
  Poll<std::tuple<R0, R1, R2, R3>> operator()() {
    if ((state_ & 1) == 0) {
      auto r = pending0_();
      if (auto* p = r.get_ready()) { state_ |= 1; Destruct(&pending0_); Construct(&ready0_, std::move(*p)); }
    }
    if ((state_ & 2) == 0) {
      auto r = pending1_();
      if (auto* p = r.get_ready()) { state_ |= 2; Destruct(&pending1_); Construct(&ready1_, std::move(*p)); }
    }
    if ((state_ & 4) == 0) {
      auto r = pending2_();
      if (auto* p = r.get_ready()) { state_ |= 4; Destruct(&pending2_); Construct(&ready2_, std::move(*p)); }
    }
    if ((state_ & 8) == 0) {
      auto r = pending3_();
      if (auto* p = r.get_ready()) { state_ |= 8; Destruct(&pending3_); Construct(&ready3_, std::move(*p)); }
    }
    if (state_ != 15) return kPending;
    return ready(std::tuple<R0, R1, R2, R3>(std::move(ready0_), std::move(ready1_), std::move(ready2_), std::move(ready3_)));
  }
};
template <typename F0, typename F1, typename F2, typename F3, typename F4> class Join<F0, F1, F2, F3, F4> {
 private:
  [[no_unique_address]] uint8_t state_ = 0;
  using R0 = typename decltype(std::declval<F0>()())::Type;
  union { [[no_unique_address]] F0 pending0_; [[no_unique_address]] R0 ready0_; };
  using R1 = typename decltype(std::declval<F1>()())::Type;
  union { [[no_unique_address]] F1 pending1_; [[no_unique_address]] R1 ready1_; };
  using R2 = typename decltype(std::declval<F2>()())::Type;
  union { [[no_unique_address]] F2 pending2_; [[no_unique_address]] R2 ready2_; };
  using R3 = typename decltype(std::declval<F3>()())::Type;
  union { [[no_unique_address]] F3 pending3_; [[no_unique_address]] R3 ready3_; };
  using R4 = typename decltype(std::declval<F4>()())::Type;
  union { [[no_unique_address]] F4 pending4_; [[no_unique_address]] R4 ready4_; };
 public:
  Join(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4) : pending0_(std::move(f0)), pending1_(std::move(f1)), pending2_(std::move(f2)), pending3_(std::move(f3)), pending4_(std::move(f4)) {}
  Join& operator=(const Join&) = delete;
  Join(const Join& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
  }
  Join(Join&& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
  }
  ~Join() {
    if (state_ & 1) Destruct(&ready0_); else Destruct(&pending0_);
    if (state_ & 2) Destruct(&ready1_); else Destruct(&pending1_);
    if (state_ & 4) Destruct(&ready2_); else Destruct(&pending2_);
    if (state_ & 8) Destruct(&ready3_); else Destruct(&pending3_);
    if (state_ & 16) Destruct(&ready4_); else Destruct(&pending4_);
  }
  Poll<std::tuple<R0, R1, R2, R3, R4>> operator()() {
    if ((state_ & 1) == 0) {
      auto r = pending0_();
      if (auto* p = r.get_ready()) { state_ |= 1; Destruct(&pending0_); Construct(&ready0_, std::move(*p)); }
    }
    if ((state_ & 2) == 0) {
      auto r = pending1_();
      if (auto* p = r.get_ready()) { state_ |= 2; Destruct(&pending1_); Construct(&ready1_, std::move(*p)); }
    }
    if ((state_ & 4) == 0) {
      auto r = pending2_();
      if (auto* p = r.get_ready()) { state_ |= 4; Destruct(&pending2_); Construct(&ready2_, std::move(*p)); }
    }
    if ((state_ & 8) == 0) {
      auto r = pending3_();
      if (auto* p = r.get_ready()) { state_ |= 8; Destruct(&pending3_); Construct(&ready3_, std::move(*p)); }
    }
    if ((state_ & 16) == 0) {
      auto r = pending4_();
      if (auto* p = r.get_ready()) { state_ |= 16; Destruct(&pending4_); Construct(&ready4_, std::move(*p)); }
    }
    if (state_ != 31) return kPending;
    return ready(std::tuple<R0, R1, R2, R3, R4>(std::move(ready0_), std::move(ready1_), std::move(ready2_), std::move(ready3_), std::move(ready4_)));
  }
};
template <typename F0, typename F1, typename F2, typename F3, typename F4, typename F5> class Join<F0, F1, F2, F3, F4, F5> {
 private:
  [[no_unique_address]] uint8_t state_ = 0;
  using R0 = typename decltype(std::declval<F0>()())::Type;
  union { [[no_unique_address]] F0 pending0_; [[no_unique_address]] R0 ready0_; };
  using R1 = typename decltype(std::declval<F1>()())::Type;
  union { [[no_unique_address]] F1 pending1_; [[no_unique_address]] R1 ready1_; };
  using R2 = typename decltype(std::declval<F2>()())::Type;
  union { [[no_unique_address]] F2 pending2_; [[no_unique_address]] R2 ready2_; };
  using R3 = typename decltype(std::declval<F3>()())::Type;
  union { [[no_unique_address]] F3 pending3_; [[no_unique_address]] R3 ready3_; };
  using R4 = typename decltype(std::declval<F4>()())::Type;
  union { [[no_unique_address]] F4 pending4_; [[no_unique_address]] R4 ready4_; };
  using R5 = typename decltype(std::declval<F5>()())::Type;
  union { [[no_unique_address]] F5 pending5_; [[no_unique_address]] R5 ready5_; };
 public:
  Join(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5) : pending0_(std::move(f0)), pending1_(std::move(f1)), pending2_(std::move(f2)), pending3_(std::move(f3)), pending4_(std::move(f4)), pending5_(std::move(f5)) {}
  Join& operator=(const Join&) = delete;
  Join(const Join& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
  }
  Join(Join&& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
  }
  ~Join() {
    if (state_ & 1) Destruct(&ready0_); else Destruct(&pending0_);
    if (state_ & 2) Destruct(&ready1_); else Destruct(&pending1_);
    if (state_ & 4) Destruct(&ready2_); else Destruct(&pending2_);
    if (state_ & 8) Destruct(&ready3_); else Destruct(&pending3_);
    if (state_ & 16) Destruct(&ready4_); else Destruct(&pending4_);
    if (state_ & 32) Destruct(&ready5_); else Destruct(&pending5_);
  }
  Poll<std::tuple<R0, R1, R2, R3, R4, R5>> operator()() {
    if ((state_ & 1) == 0) {
      auto r = pending0_();
      if (auto* p = r.get_ready()) { state_ |= 1; Destruct(&pending0_); Construct(&ready0_, std::move(*p)); }
    }
    if ((state_ & 2) == 0) {
      auto r = pending1_();
      if (auto* p = r.get_ready()) { state_ |= 2; Destruct(&pending1_); Construct(&ready1_, std::move(*p)); }
    }
    if ((state_ & 4) == 0) {
      auto r = pending2_();
      if (auto* p = r.get_ready()) { state_ |= 4; Destruct(&pending2_); Construct(&ready2_, std::move(*p)); }
    }
    if ((state_ & 8) == 0) {
      auto r = pending3_();
      if (auto* p = r.get_ready()) { state_ |= 8; Destruct(&pending3_); Construct(&ready3_, std::move(*p)); }
    }
    if ((state_ & 16) == 0) {
      auto r = pending4_();
      if (auto* p = r.get_ready()) { state_ |= 16; Destruct(&pending4_); Construct(&ready4_, std::move(*p)); }
    }
    if ((state_ & 32) == 0) {
      auto r = pending5_();
      if (auto* p = r.get_ready()) { state_ |= 32; Destruct(&pending5_); Construct(&ready5_, std::move(*p)); }
    }
    if (state_ != 63) return kPending;
    return ready(std::tuple<R0, R1, R2, R3, R4, R5>(std::move(ready0_), std::move(ready1_), std::move(ready2_), std::move(ready3_), std::move(ready4_), std::move(ready5_)));
  }
};
template <typename F0, typename F1, typename F2, typename F3, typename F4, typename F5, typename F6> class Join<F0, F1, F2, F3, F4, F5, F6> {
 private:
  [[no_unique_address]] uint8_t state_ = 0;
  using R0 = typename decltype(std::declval<F0>()())::Type;
  union { [[no_unique_address]] F0 pending0_; [[no_unique_address]] R0 ready0_; };
  using R1 = typename decltype(std::declval<F1>()())::Type;
  union { [[no_unique_address]] F1 pending1_; [[no_unique_address]] R1 ready1_; };
  using R2 = typename decltype(std::declval<F2>()())::Type;
  union { [[no_unique_address]] F2 pending2_; [[no_unique_address]] R2 ready2_; };
  using R3 = typename decltype(std::declval<F3>()())::Type;
  union { [[no_unique_address]] F3 pending3_; [[no_unique_address]] R3 ready3_; };
  using R4 = typename decltype(std::declval<F4>()())::Type;
  union { [[no_unique_address]] F4 pending4_; [[no_unique_address]] R4 ready4_; };
  using R5 = typename decltype(std::declval<F5>()())::Type;
  union { [[no_unique_address]] F5 pending5_; [[no_unique_address]] R5 ready5_; };
  using R6 = typename decltype(std::declval<F6>()())::Type;
  union { [[no_unique_address]] F6 pending6_; [[no_unique_address]] R6 ready6_; };
 public:
  Join(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6) : pending0_(std::move(f0)), pending1_(std::move(f1)), pending2_(std::move(f2)), pending3_(std::move(f3)), pending4_(std::move(f4)), pending5_(std::move(f5)), pending6_(std::move(f6)) {}
  Join& operator=(const Join&) = delete;
  Join(const Join& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
  }
  Join(Join&& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
  }
  ~Join() {
    if (state_ & 1) Destruct(&ready0_); else Destruct(&pending0_);
    if (state_ & 2) Destruct(&ready1_); else Destruct(&pending1_);
    if (state_ & 4) Destruct(&ready2_); else Destruct(&pending2_);
    if (state_ & 8) Destruct(&ready3_); else Destruct(&pending3_);
    if (state_ & 16) Destruct(&ready4_); else Destruct(&pending4_);
    if (state_ & 32) Destruct(&ready5_); else Destruct(&pending5_);
    if (state_ & 64) Destruct(&ready6_); else Destruct(&pending6_);
  }
  Poll<std::tuple<R0, R1, R2, R3, R4, R5, R6>> operator()() {
    if ((state_ & 1) == 0) {
      auto r = pending0_();
      if (auto* p = r.get_ready()) { state_ |= 1; Destruct(&pending0_); Construct(&ready0_, std::move(*p)); }
    }
    if ((state_ & 2) == 0) {
      auto r = pending1_();
      if (auto* p = r.get_ready()) { state_ |= 2; Destruct(&pending1_); Construct(&ready1_, std::move(*p)); }
    }
    if ((state_ & 4) == 0) {
      auto r = pending2_();
      if (auto* p = r.get_ready()) { state_ |= 4; Destruct(&pending2_); Construct(&ready2_, std::move(*p)); }
    }
    if ((state_ & 8) == 0) {
      auto r = pending3_();
      if (auto* p = r.get_ready()) { state_ |= 8; Destruct(&pending3_); Construct(&ready3_, std::move(*p)); }
    }
    if ((state_ & 16) == 0) {
      auto r = pending4_();
      if (auto* p = r.get_ready()) { state_ |= 16; Destruct(&pending4_); Construct(&ready4_, std::move(*p)); }
    }
    if ((state_ & 32) == 0) {
      auto r = pending5_();
      if (auto* p = r.get_ready()) { state_ |= 32; Destruct(&pending5_); Construct(&ready5_, std::move(*p)); }
    }
    if ((state_ & 64) == 0) {
      auto r = pending6_();
      if (auto* p = r.get_ready()) { state_ |= 64; Destruct(&pending6_); Construct(&ready6_, std::move(*p)); }
    }
    if (state_ != 127) return kPending;
    return ready(std::tuple<R0, R1, R2, R3, R4, R5, R6>(std::move(ready0_), std::move(ready1_), std::move(ready2_), std::move(ready3_), std::move(ready4_), std::move(ready5_), std::move(ready6_)));
  }
};
template <typename F0, typename F1, typename F2, typename F3, typename F4, typename F5, typename F6, typename F7> class Join<F0, F1, F2, F3, F4, F5, F6, F7> {
 private:
  [[no_unique_address]] uint8_t state_ = 0;
  using R0 = typename decltype(std::declval<F0>()())::Type;
  union { [[no_unique_address]] F0 pending0_; [[no_unique_address]] R0 ready0_; };
  using R1 = typename decltype(std::declval<F1>()())::Type;
  union { [[no_unique_address]] F1 pending1_; [[no_unique_address]] R1 ready1_; };
  using R2 = typename decltype(std::declval<F2>()())::Type;
  union { [[no_unique_address]] F2 pending2_; [[no_unique_address]] R2 ready2_; };
  using R3 = typename decltype(std::declval<F3>()())::Type;
  union { [[no_unique_address]] F3 pending3_; [[no_unique_address]] R3 ready3_; };
  using R4 = typename decltype(std::declval<F4>()())::Type;
  union { [[no_unique_address]] F4 pending4_; [[no_unique_address]] R4 ready4_; };
  using R5 = typename decltype(std::declval<F5>()())::Type;
  union { [[no_unique_address]] F5 pending5_; [[no_unique_address]] R5 ready5_; };
  using R6 = typename decltype(std::declval<F6>()())::Type;
  union { [[no_unique_address]] F6 pending6_; [[no_unique_address]] R6 ready6_; };
  using R7 = typename decltype(std::declval<F7>()())::Type;
  union { [[no_unique_address]] F7 pending7_; [[no_unique_address]] R7 ready7_; };
 public:
  Join(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7) : pending0_(std::move(f0)), pending1_(std::move(f1)), pending2_(std::move(f2)), pending3_(std::move(f3)), pending4_(std::move(f4)), pending5_(std::move(f5)), pending6_(std::move(f6)), pending7_(std::move(f7)) {}
  Join& operator=(const Join&) = delete;
  Join(const Join& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
  }
  Join(Join&& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
  }
  ~Join() {
    if (state_ & 1) Destruct(&ready0_); else Destruct(&pending0_);
    if (state_ & 2) Destruct(&ready1_); else Destruct(&pending1_);
    if (state_ & 4) Destruct(&ready2_); else Destruct(&pending2_);
    if (state_ & 8) Destruct(&ready3_); else Destruct(&pending3_);
    if (state_ & 16) Destruct(&ready4_); else Destruct(&pending4_);
    if (state_ & 32) Destruct(&ready5_); else Destruct(&pending5_);
    if (state_ & 64) Destruct(&ready6_); else Destruct(&pending6_);
    if (state_ & 128) Destruct(&ready7_); else Destruct(&pending7_);
  }
  Poll<std::tuple<R0, R1, R2, R3, R4, R5, R6, R7>> operator()() {
    if ((state_ & 1) == 0) {
      auto r = pending0_();
      if (auto* p = r.get_ready()) { state_ |= 1; Destruct(&pending0_); Construct(&ready0_, std::move(*p)); }
    }
    if ((state_ & 2) == 0) {
      auto r = pending1_();
      if (auto* p = r.get_ready()) { state_ |= 2; Destruct(&pending1_); Construct(&ready1_, std::move(*p)); }
    }
    if ((state_ & 4) == 0) {
      auto r = pending2_();
      if (auto* p = r.get_ready()) { state_ |= 4; Destruct(&pending2_); Construct(&ready2_, std::move(*p)); }
    }
    if ((state_ & 8) == 0) {
      auto r = pending3_();
      if (auto* p = r.get_ready()) { state_ |= 8; Destruct(&pending3_); Construct(&ready3_, std::move(*p)); }
    }
    if ((state_ & 16) == 0) {
      auto r = pending4_();
      if (auto* p = r.get_ready()) { state_ |= 16; Destruct(&pending4_); Construct(&ready4_, std::move(*p)); }
    }
    if ((state_ & 32) == 0) {
      auto r = pending5_();
      if (auto* p = r.get_ready()) { state_ |= 32; Destruct(&pending5_); Construct(&ready5_, std::move(*p)); }
    }
    if ((state_ & 64) == 0) {
      auto r = pending6_();
      if (auto* p = r.get_ready()) { state_ |= 64; Destruct(&pending6_); Construct(&ready6_, std::move(*p)); }
    }
    if ((state_ & 128) == 0) {
      auto r = pending7_();
      if (auto* p = r.get_ready()) { state_ |= 128; Destruct(&pending7_); Construct(&ready7_, std::move(*p)); }
    }
    if (state_ != 255) return kPending;
    return ready(std::tuple<R0, R1, R2, R3, R4, R5, R6, R7>(std::move(ready0_), std::move(ready1_), std::move(ready2_), std::move(ready3_), std::move(ready4_), std::move(ready5_), std::move(ready6_), std::move(ready7_)));
  }
};
template <typename F0, typename F1, typename F2, typename F3, typename F4, typename F5, typename F6, typename F7, typename F8> class Join<F0, F1, F2, F3, F4, F5, F6, F7, F8> {
 private:
  [[no_unique_address]] uint16_t state_ = 0;
  using R0 = typename decltype(std::declval<F0>()())::Type;
  union { [[no_unique_address]] F0 pending0_; [[no_unique_address]] R0 ready0_; };
  using R1 = typename decltype(std::declval<F1>()())::Type;
  union { [[no_unique_address]] F1 pending1_; [[no_unique_address]] R1 ready1_; };
  using R2 = typename decltype(std::declval<F2>()())::Type;
  union { [[no_unique_address]] F2 pending2_; [[no_unique_address]] R2 ready2_; };
  using R3 = typename decltype(std::declval<F3>()())::Type;
  union { [[no_unique_address]] F3 pending3_; [[no_unique_address]] R3 ready3_; };
  using R4 = typename decltype(std::declval<F4>()())::Type;
  union { [[no_unique_address]] F4 pending4_; [[no_unique_address]] R4 ready4_; };
  using R5 = typename decltype(std::declval<F5>()())::Type;
  union { [[no_unique_address]] F5 pending5_; [[no_unique_address]] R5 ready5_; };
  using R6 = typename decltype(std::declval<F6>()())::Type;
  union { [[no_unique_address]] F6 pending6_; [[no_unique_address]] R6 ready6_; };
  using R7 = typename decltype(std::declval<F7>()())::Type;
  union { [[no_unique_address]] F7 pending7_; [[no_unique_address]] R7 ready7_; };
  using R8 = typename decltype(std::declval<F8>()())::Type;
  union { [[no_unique_address]] F8 pending8_; [[no_unique_address]] R8 ready8_; };
 public:
  Join(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7, F8 f8) : pending0_(std::move(f0)), pending1_(std::move(f1)), pending2_(std::move(f2)), pending3_(std::move(f3)), pending4_(std::move(f4)), pending5_(std::move(f5)), pending6_(std::move(f6)), pending7_(std::move(f7)), pending8_(std::move(f8)) {}
  Join& operator=(const Join&) = delete;
  Join(const Join& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
  }
  Join(Join&& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
  }
  ~Join() {
    if (state_ & 1) Destruct(&ready0_); else Destruct(&pending0_);
    if (state_ & 2) Destruct(&ready1_); else Destruct(&pending1_);
    if (state_ & 4) Destruct(&ready2_); else Destruct(&pending2_);
    if (state_ & 8) Destruct(&ready3_); else Destruct(&pending3_);
    if (state_ & 16) Destruct(&ready4_); else Destruct(&pending4_);
    if (state_ & 32) Destruct(&ready5_); else Destruct(&pending5_);
    if (state_ & 64) Destruct(&ready6_); else Destruct(&pending6_);
    if (state_ & 128) Destruct(&ready7_); else Destruct(&pending7_);
    if (state_ & 256) Destruct(&ready8_); else Destruct(&pending8_);
  }
  Poll<std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8>> operator()() {
    if ((state_ & 1) == 0) {
      auto r = pending0_();
      if (auto* p = r.get_ready()) { state_ |= 1; Destruct(&pending0_); Construct(&ready0_, std::move(*p)); }
    }
    if ((state_ & 2) == 0) {
      auto r = pending1_();
      if (auto* p = r.get_ready()) { state_ |= 2; Destruct(&pending1_); Construct(&ready1_, std::move(*p)); }
    }
    if ((state_ & 4) == 0) {
      auto r = pending2_();
      if (auto* p = r.get_ready()) { state_ |= 4; Destruct(&pending2_); Construct(&ready2_, std::move(*p)); }
    }
    if ((state_ & 8) == 0) {
      auto r = pending3_();
      if (auto* p = r.get_ready()) { state_ |= 8; Destruct(&pending3_); Construct(&ready3_, std::move(*p)); }
    }
    if ((state_ & 16) == 0) {
      auto r = pending4_();
      if (auto* p = r.get_ready()) { state_ |= 16; Destruct(&pending4_); Construct(&ready4_, std::move(*p)); }
    }
    if ((state_ & 32) == 0) {
      auto r = pending5_();
      if (auto* p = r.get_ready()) { state_ |= 32; Destruct(&pending5_); Construct(&ready5_, std::move(*p)); }
    }
    if ((state_ & 64) == 0) {
      auto r = pending6_();
      if (auto* p = r.get_ready()) { state_ |= 64; Destruct(&pending6_); Construct(&ready6_, std::move(*p)); }
    }
    if ((state_ & 128) == 0) {
      auto r = pending7_();
      if (auto* p = r.get_ready()) { state_ |= 128; Destruct(&pending7_); Construct(&ready7_, std::move(*p)); }
    }
    if ((state_ & 256) == 0) {
      auto r = pending8_();
      if (auto* p = r.get_ready()) { state_ |= 256; Destruct(&pending8_); Construct(&ready8_, std::move(*p)); }
    }
    if (state_ != 511) return kPending;
    return ready(std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8>(std::move(ready0_), std::move(ready1_), std::move(ready2_), std::move(ready3_), std::move(ready4_), std::move(ready5_), std::move(ready6_), std::move(ready7_), std::move(ready8_)));
  }
};
template <typename F0, typename F1, typename F2, typename F3, typename F4, typename F5, typename F6, typename F7, typename F8, typename F9> class Join<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9> {
 private:
  [[no_unique_address]] uint16_t state_ = 0;
  using R0 = typename decltype(std::declval<F0>()())::Type;
  union { [[no_unique_address]] F0 pending0_; [[no_unique_address]] R0 ready0_; };
  using R1 = typename decltype(std::declval<F1>()())::Type;
  union { [[no_unique_address]] F1 pending1_; [[no_unique_address]] R1 ready1_; };
  using R2 = typename decltype(std::declval<F2>()())::Type;
  union { [[no_unique_address]] F2 pending2_; [[no_unique_address]] R2 ready2_; };
  using R3 = typename decltype(std::declval<F3>()())::Type;
  union { [[no_unique_address]] F3 pending3_; [[no_unique_address]] R3 ready3_; };
  using R4 = typename decltype(std::declval<F4>()())::Type;
  union { [[no_unique_address]] F4 pending4_; [[no_unique_address]] R4 ready4_; };
  using R5 = typename decltype(std::declval<F5>()())::Type;
  union { [[no_unique_address]] F5 pending5_; [[no_unique_address]] R5 ready5_; };
  using R6 = typename decltype(std::declval<F6>()())::Type;
  union { [[no_unique_address]] F6 pending6_; [[no_unique_address]] R6 ready6_; };
  using R7 = typename decltype(std::declval<F7>()())::Type;
  union { [[no_unique_address]] F7 pending7_; [[no_unique_address]] R7 ready7_; };
  using R8 = typename decltype(std::declval<F8>()())::Type;
  union { [[no_unique_address]] F8 pending8_; [[no_unique_address]] R8 ready8_; };
  using R9 = typename decltype(std::declval<F9>()())::Type;
  union { [[no_unique_address]] F9 pending9_; [[no_unique_address]] R9 ready9_; };
 public:
  Join(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7, F8 f8, F9 f9) : pending0_(std::move(f0)), pending1_(std::move(f1)), pending2_(std::move(f2)), pending3_(std::move(f3)), pending4_(std::move(f4)), pending5_(std::move(f5)), pending6_(std::move(f6)), pending7_(std::move(f7)), pending8_(std::move(f8)), pending9_(std::move(f9)) {}
  Join& operator=(const Join&) = delete;
  Join(const Join& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
  }
  Join(Join&& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
  }
  ~Join() {
    if (state_ & 1) Destruct(&ready0_); else Destruct(&pending0_);
    if (state_ & 2) Destruct(&ready1_); else Destruct(&pending1_);
    if (state_ & 4) Destruct(&ready2_); else Destruct(&pending2_);
    if (state_ & 8) Destruct(&ready3_); else Destruct(&pending3_);
    if (state_ & 16) Destruct(&ready4_); else Destruct(&pending4_);
    if (state_ & 32) Destruct(&ready5_); else Destruct(&pending5_);
    if (state_ & 64) Destruct(&ready6_); else Destruct(&pending6_);
    if (state_ & 128) Destruct(&ready7_); else Destruct(&pending7_);
    if (state_ & 256) Destruct(&ready8_); else Destruct(&pending8_);
    if (state_ & 512) Destruct(&ready9_); else Destruct(&pending9_);
  }
  Poll<std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9>> operator()() {
    if ((state_ & 1) == 0) {
      auto r = pending0_();
      if (auto* p = r.get_ready()) { state_ |= 1; Destruct(&pending0_); Construct(&ready0_, std::move(*p)); }
    }
    if ((state_ & 2) == 0) {
      auto r = pending1_();
      if (auto* p = r.get_ready()) { state_ |= 2; Destruct(&pending1_); Construct(&ready1_, std::move(*p)); }
    }
    if ((state_ & 4) == 0) {
      auto r = pending2_();
      if (auto* p = r.get_ready()) { state_ |= 4; Destruct(&pending2_); Construct(&ready2_, std::move(*p)); }
    }
    if ((state_ & 8) == 0) {
      auto r = pending3_();
      if (auto* p = r.get_ready()) { state_ |= 8; Destruct(&pending3_); Construct(&ready3_, std::move(*p)); }
    }
    if ((state_ & 16) == 0) {
      auto r = pending4_();
      if (auto* p = r.get_ready()) { state_ |= 16; Destruct(&pending4_); Construct(&ready4_, std::move(*p)); }
    }
    if ((state_ & 32) == 0) {
      auto r = pending5_();
      if (auto* p = r.get_ready()) { state_ |= 32; Destruct(&pending5_); Construct(&ready5_, std::move(*p)); }
    }
    if ((state_ & 64) == 0) {
      auto r = pending6_();
      if (auto* p = r.get_ready()) { state_ |= 64; Destruct(&pending6_); Construct(&ready6_, std::move(*p)); }
    }
    if ((state_ & 128) == 0) {
      auto r = pending7_();
      if (auto* p = r.get_ready()) { state_ |= 128; Destruct(&pending7_); Construct(&ready7_, std::move(*p)); }
    }
    if ((state_ & 256) == 0) {
      auto r = pending8_();
      if (auto* p = r.get_ready()) { state_ |= 256; Destruct(&pending8_); Construct(&ready8_, std::move(*p)); }
    }
    if ((state_ & 512) == 0) {
      auto r = pending9_();
      if (auto* p = r.get_ready()) { state_ |= 512; Destruct(&pending9_); Construct(&ready9_, std::move(*p)); }
    }
    if (state_ != 1023) return kPending;
    return ready(std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9>(std::move(ready0_), std::move(ready1_), std::move(ready2_), std::move(ready3_), std::move(ready4_), std::move(ready5_), std::move(ready6_), std::move(ready7_), std::move(ready8_), std::move(ready9_)));
  }
};
template <typename F0, typename F1, typename F2, typename F3, typename F4, typename F5, typename F6, typename F7, typename F8, typename F9, typename F10> class Join<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10> {
 private:
  [[no_unique_address]] uint16_t state_ = 0;
  using R0 = typename decltype(std::declval<F0>()())::Type;
  union { [[no_unique_address]] F0 pending0_; [[no_unique_address]] R0 ready0_; };
  using R1 = typename decltype(std::declval<F1>()())::Type;
  union { [[no_unique_address]] F1 pending1_; [[no_unique_address]] R1 ready1_; };
  using R2 = typename decltype(std::declval<F2>()())::Type;
  union { [[no_unique_address]] F2 pending2_; [[no_unique_address]] R2 ready2_; };
  using R3 = typename decltype(std::declval<F3>()())::Type;
  union { [[no_unique_address]] F3 pending3_; [[no_unique_address]] R3 ready3_; };
  using R4 = typename decltype(std::declval<F4>()())::Type;
  union { [[no_unique_address]] F4 pending4_; [[no_unique_address]] R4 ready4_; };
  using R5 = typename decltype(std::declval<F5>()())::Type;
  union { [[no_unique_address]] F5 pending5_; [[no_unique_address]] R5 ready5_; };
  using R6 = typename decltype(std::declval<F6>()())::Type;
  union { [[no_unique_address]] F6 pending6_; [[no_unique_address]] R6 ready6_; };
  using R7 = typename decltype(std::declval<F7>()())::Type;
  union { [[no_unique_address]] F7 pending7_; [[no_unique_address]] R7 ready7_; };
  using R8 = typename decltype(std::declval<F8>()())::Type;
  union { [[no_unique_address]] F8 pending8_; [[no_unique_address]] R8 ready8_; };
  using R9 = typename decltype(std::declval<F9>()())::Type;
  union { [[no_unique_address]] F9 pending9_; [[no_unique_address]] R9 ready9_; };
  using R10 = typename decltype(std::declval<F10>()())::Type;
  union { [[no_unique_address]] F10 pending10_; [[no_unique_address]] R10 ready10_; };
 public:
  Join(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7, F8 f8, F9 f9, F10 f10) : pending0_(std::move(f0)), pending1_(std::move(f1)), pending2_(std::move(f2)), pending3_(std::move(f3)), pending4_(std::move(f4)), pending5_(std::move(f5)), pending6_(std::move(f6)), pending7_(std::move(f7)), pending8_(std::move(f8)), pending9_(std::move(f9)), pending10_(std::move(f10)) {}
  Join& operator=(const Join&) = delete;
  Join(const Join& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
  }
  Join(Join&& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
  }
  ~Join() {
    if (state_ & 1) Destruct(&ready0_); else Destruct(&pending0_);
    if (state_ & 2) Destruct(&ready1_); else Destruct(&pending1_);
    if (state_ & 4) Destruct(&ready2_); else Destruct(&pending2_);
    if (state_ & 8) Destruct(&ready3_); else Destruct(&pending3_);
    if (state_ & 16) Destruct(&ready4_); else Destruct(&pending4_);
    if (state_ & 32) Destruct(&ready5_); else Destruct(&pending5_);
    if (state_ & 64) Destruct(&ready6_); else Destruct(&pending6_);
    if (state_ & 128) Destruct(&ready7_); else Destruct(&pending7_);
    if (state_ & 256) Destruct(&ready8_); else Destruct(&pending8_);
    if (state_ & 512) Destruct(&ready9_); else Destruct(&pending9_);
    if (state_ & 1024) Destruct(&ready10_); else Destruct(&pending10_);
  }
  Poll<std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10>> operator()() {
    if ((state_ & 1) == 0) {
      auto r = pending0_();
      if (auto* p = r.get_ready()) { state_ |= 1; Destruct(&pending0_); Construct(&ready0_, std::move(*p)); }
    }
    if ((state_ & 2) == 0) {
      auto r = pending1_();
      if (auto* p = r.get_ready()) { state_ |= 2; Destruct(&pending1_); Construct(&ready1_, std::move(*p)); }
    }
    if ((state_ & 4) == 0) {
      auto r = pending2_();
      if (auto* p = r.get_ready()) { state_ |= 4; Destruct(&pending2_); Construct(&ready2_, std::move(*p)); }
    }
    if ((state_ & 8) == 0) {
      auto r = pending3_();
      if (auto* p = r.get_ready()) { state_ |= 8; Destruct(&pending3_); Construct(&ready3_, std::move(*p)); }
    }
    if ((state_ & 16) == 0) {
      auto r = pending4_();
      if (auto* p = r.get_ready()) { state_ |= 16; Destruct(&pending4_); Construct(&ready4_, std::move(*p)); }
    }
    if ((state_ & 32) == 0) {
      auto r = pending5_();
      if (auto* p = r.get_ready()) { state_ |= 32; Destruct(&pending5_); Construct(&ready5_, std::move(*p)); }
    }
    if ((state_ & 64) == 0) {
      auto r = pending6_();
      if (auto* p = r.get_ready()) { state_ |= 64; Destruct(&pending6_); Construct(&ready6_, std::move(*p)); }
    }
    if ((state_ & 128) == 0) {
      auto r = pending7_();
      if (auto* p = r.get_ready()) { state_ |= 128; Destruct(&pending7_); Construct(&ready7_, std::move(*p)); }
    }
    if ((state_ & 256) == 0) {
      auto r = pending8_();
      if (auto* p = r.get_ready()) { state_ |= 256; Destruct(&pending8_); Construct(&ready8_, std::move(*p)); }
    }
    if ((state_ & 512) == 0) {
      auto r = pending9_();
      if (auto* p = r.get_ready()) { state_ |= 512; Destruct(&pending9_); Construct(&ready9_, std::move(*p)); }
    }
    if ((state_ & 1024) == 0) {
      auto r = pending10_();
      if (auto* p = r.get_ready()) { state_ |= 1024; Destruct(&pending10_); Construct(&ready10_, std::move(*p)); }
    }
    if (state_ != 2047) return kPending;
    return ready(std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10>(std::move(ready0_), std::move(ready1_), std::move(ready2_), std::move(ready3_), std::move(ready4_), std::move(ready5_), std::move(ready6_), std::move(ready7_), std::move(ready8_), std::move(ready9_), std::move(ready10_)));
  }
};
template <typename F0, typename F1, typename F2, typename F3, typename F4, typename F5, typename F6, typename F7, typename F8, typename F9, typename F10, typename F11> class Join<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11> {
 private:
  [[no_unique_address]] uint16_t state_ = 0;
  using R0 = typename decltype(std::declval<F0>()())::Type;
  union { [[no_unique_address]] F0 pending0_; [[no_unique_address]] R0 ready0_; };
  using R1 = typename decltype(std::declval<F1>()())::Type;
  union { [[no_unique_address]] F1 pending1_; [[no_unique_address]] R1 ready1_; };
  using R2 = typename decltype(std::declval<F2>()())::Type;
  union { [[no_unique_address]] F2 pending2_; [[no_unique_address]] R2 ready2_; };
  using R3 = typename decltype(std::declval<F3>()())::Type;
  union { [[no_unique_address]] F3 pending3_; [[no_unique_address]] R3 ready3_; };
  using R4 = typename decltype(std::declval<F4>()())::Type;
  union { [[no_unique_address]] F4 pending4_; [[no_unique_address]] R4 ready4_; };
  using R5 = typename decltype(std::declval<F5>()())::Type;
  union { [[no_unique_address]] F5 pending5_; [[no_unique_address]] R5 ready5_; };
  using R6 = typename decltype(std::declval<F6>()())::Type;
  union { [[no_unique_address]] F6 pending6_; [[no_unique_address]] R6 ready6_; };
  using R7 = typename decltype(std::declval<F7>()())::Type;
  union { [[no_unique_address]] F7 pending7_; [[no_unique_address]] R7 ready7_; };
  using R8 = typename decltype(std::declval<F8>()())::Type;
  union { [[no_unique_address]] F8 pending8_; [[no_unique_address]] R8 ready8_; };
  using R9 = typename decltype(std::declval<F9>()())::Type;
  union { [[no_unique_address]] F9 pending9_; [[no_unique_address]] R9 ready9_; };
  using R10 = typename decltype(std::declval<F10>()())::Type;
  union { [[no_unique_address]] F10 pending10_; [[no_unique_address]] R10 ready10_; };
  using R11 = typename decltype(std::declval<F11>()())::Type;
  union { [[no_unique_address]] F11 pending11_; [[no_unique_address]] R11 ready11_; };
 public:
  Join(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7, F8 f8, F9 f9, F10 f10, F11 f11) : pending0_(std::move(f0)), pending1_(std::move(f1)), pending2_(std::move(f2)), pending3_(std::move(f3)), pending4_(std::move(f4)), pending5_(std::move(f5)), pending6_(std::move(f6)), pending7_(std::move(f7)), pending8_(std::move(f8)), pending9_(std::move(f9)), pending10_(std::move(f10)), pending11_(std::move(f11)) {}
  Join& operator=(const Join&) = delete;
  Join(const Join& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
  }
  Join(Join&& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
  }
  ~Join() {
    if (state_ & 1) Destruct(&ready0_); else Destruct(&pending0_);
    if (state_ & 2) Destruct(&ready1_); else Destruct(&pending1_);
    if (state_ & 4) Destruct(&ready2_); else Destruct(&pending2_);
    if (state_ & 8) Destruct(&ready3_); else Destruct(&pending3_);
    if (state_ & 16) Destruct(&ready4_); else Destruct(&pending4_);
    if (state_ & 32) Destruct(&ready5_); else Destruct(&pending5_);
    if (state_ & 64) Destruct(&ready6_); else Destruct(&pending6_);
    if (state_ & 128) Destruct(&ready7_); else Destruct(&pending7_);
    if (state_ & 256) Destruct(&ready8_); else Destruct(&pending8_);
    if (state_ & 512) Destruct(&ready9_); else Destruct(&pending9_);
    if (state_ & 1024) Destruct(&ready10_); else Destruct(&pending10_);
    if (state_ & 2048) Destruct(&ready11_); else Destruct(&pending11_);
  }
  Poll<std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11>> operator()() {
    if ((state_ & 1) == 0) {
      auto r = pending0_();
      if (auto* p = r.get_ready()) { state_ |= 1; Destruct(&pending0_); Construct(&ready0_, std::move(*p)); }
    }
    if ((state_ & 2) == 0) {
      auto r = pending1_();
      if (auto* p = r.get_ready()) { state_ |= 2; Destruct(&pending1_); Construct(&ready1_, std::move(*p)); }
    }
    if ((state_ & 4) == 0) {
      auto r = pending2_();
      if (auto* p = r.get_ready()) { state_ |= 4; Destruct(&pending2_); Construct(&ready2_, std::move(*p)); }
    }
    if ((state_ & 8) == 0) {
      auto r = pending3_();
      if (auto* p = r.get_ready()) { state_ |= 8; Destruct(&pending3_); Construct(&ready3_, std::move(*p)); }
    }
    if ((state_ & 16) == 0) {
      auto r = pending4_();
      if (auto* p = r.get_ready()) { state_ |= 16; Destruct(&pending4_); Construct(&ready4_, std::move(*p)); }
    }
    if ((state_ & 32) == 0) {
      auto r = pending5_();
      if (auto* p = r.get_ready()) { state_ |= 32; Destruct(&pending5_); Construct(&ready5_, std::move(*p)); }
    }
    if ((state_ & 64) == 0) {
      auto r = pending6_();
      if (auto* p = r.get_ready()) { state_ |= 64; Destruct(&pending6_); Construct(&ready6_, std::move(*p)); }
    }
    if ((state_ & 128) == 0) {
      auto r = pending7_();
      if (auto* p = r.get_ready()) { state_ |= 128; Destruct(&pending7_); Construct(&ready7_, std::move(*p)); }
    }
    if ((state_ & 256) == 0) {
      auto r = pending8_();
      if (auto* p = r.get_ready()) { state_ |= 256; Destruct(&pending8_); Construct(&ready8_, std::move(*p)); }
    }
    if ((state_ & 512) == 0) {
      auto r = pending9_();
      if (auto* p = r.get_ready()) { state_ |= 512; Destruct(&pending9_); Construct(&ready9_, std::move(*p)); }
    }
    if ((state_ & 1024) == 0) {
      auto r = pending10_();
      if (auto* p = r.get_ready()) { state_ |= 1024; Destruct(&pending10_); Construct(&ready10_, std::move(*p)); }
    }
    if ((state_ & 2048) == 0) {
      auto r = pending11_();
      if (auto* p = r.get_ready()) { state_ |= 2048; Destruct(&pending11_); Construct(&ready11_, std::move(*p)); }
    }
    if (state_ != 4095) return kPending;
    return ready(std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11>(std::move(ready0_), std::move(ready1_), std::move(ready2_), std::move(ready3_), std::move(ready4_), std::move(ready5_), std::move(ready6_), std::move(ready7_), std::move(ready8_), std::move(ready9_), std::move(ready10_), std::move(ready11_)));
  }
};
template <typename F0, typename F1, typename F2, typename F3, typename F4, typename F5, typename F6, typename F7, typename F8, typename F9, typename F10, typename F11, typename F12> class Join<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12> {
 private:
  [[no_unique_address]] uint16_t state_ = 0;
  using R0 = typename decltype(std::declval<F0>()())::Type;
  union { [[no_unique_address]] F0 pending0_; [[no_unique_address]] R0 ready0_; };
  using R1 = typename decltype(std::declval<F1>()())::Type;
  union { [[no_unique_address]] F1 pending1_; [[no_unique_address]] R1 ready1_; };
  using R2 = typename decltype(std::declval<F2>()())::Type;
  union { [[no_unique_address]] F2 pending2_; [[no_unique_address]] R2 ready2_; };
  using R3 = typename decltype(std::declval<F3>()())::Type;
  union { [[no_unique_address]] F3 pending3_; [[no_unique_address]] R3 ready3_; };
  using R4 = typename decltype(std::declval<F4>()())::Type;
  union { [[no_unique_address]] F4 pending4_; [[no_unique_address]] R4 ready4_; };
  using R5 = typename decltype(std::declval<F5>()())::Type;
  union { [[no_unique_address]] F5 pending5_; [[no_unique_address]] R5 ready5_; };
  using R6 = typename decltype(std::declval<F6>()())::Type;
  union { [[no_unique_address]] F6 pending6_; [[no_unique_address]] R6 ready6_; };
  using R7 = typename decltype(std::declval<F7>()())::Type;
  union { [[no_unique_address]] F7 pending7_; [[no_unique_address]] R7 ready7_; };
  using R8 = typename decltype(std::declval<F8>()())::Type;
  union { [[no_unique_address]] F8 pending8_; [[no_unique_address]] R8 ready8_; };
  using R9 = typename decltype(std::declval<F9>()())::Type;
  union { [[no_unique_address]] F9 pending9_; [[no_unique_address]] R9 ready9_; };
  using R10 = typename decltype(std::declval<F10>()())::Type;
  union { [[no_unique_address]] F10 pending10_; [[no_unique_address]] R10 ready10_; };
  using R11 = typename decltype(std::declval<F11>()())::Type;
  union { [[no_unique_address]] F11 pending11_; [[no_unique_address]] R11 ready11_; };
  using R12 = typename decltype(std::declval<F12>()())::Type;
  union { [[no_unique_address]] F12 pending12_; [[no_unique_address]] R12 ready12_; };
 public:
  Join(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7, F8 f8, F9 f9, F10 f10, F11 f11, F12 f12) : pending0_(std::move(f0)), pending1_(std::move(f1)), pending2_(std::move(f2)), pending3_(std::move(f3)), pending4_(std::move(f4)), pending5_(std::move(f5)), pending6_(std::move(f6)), pending7_(std::move(f7)), pending8_(std::move(f8)), pending9_(std::move(f9)), pending10_(std::move(f10)), pending11_(std::move(f11)), pending12_(std::move(f12)) {}
  Join& operator=(const Join&) = delete;
  Join(const Join& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
  }
  Join(Join&& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
  }
  ~Join() {
    if (state_ & 1) Destruct(&ready0_); else Destruct(&pending0_);
    if (state_ & 2) Destruct(&ready1_); else Destruct(&pending1_);
    if (state_ & 4) Destruct(&ready2_); else Destruct(&pending2_);
    if (state_ & 8) Destruct(&ready3_); else Destruct(&pending3_);
    if (state_ & 16) Destruct(&ready4_); else Destruct(&pending4_);
    if (state_ & 32) Destruct(&ready5_); else Destruct(&pending5_);
    if (state_ & 64) Destruct(&ready6_); else Destruct(&pending6_);
    if (state_ & 128) Destruct(&ready7_); else Destruct(&pending7_);
    if (state_ & 256) Destruct(&ready8_); else Destruct(&pending8_);
    if (state_ & 512) Destruct(&ready9_); else Destruct(&pending9_);
    if (state_ & 1024) Destruct(&ready10_); else Destruct(&pending10_);
    if (state_ & 2048) Destruct(&ready11_); else Destruct(&pending11_);
    if (state_ & 4096) Destruct(&ready12_); else Destruct(&pending12_);
  }
  Poll<std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12>> operator()() {
    if ((state_ & 1) == 0) {
      auto r = pending0_();
      if (auto* p = r.get_ready()) { state_ |= 1; Destruct(&pending0_); Construct(&ready0_, std::move(*p)); }
    }
    if ((state_ & 2) == 0) {
      auto r = pending1_();
      if (auto* p = r.get_ready()) { state_ |= 2; Destruct(&pending1_); Construct(&ready1_, std::move(*p)); }
    }
    if ((state_ & 4) == 0) {
      auto r = pending2_();
      if (auto* p = r.get_ready()) { state_ |= 4; Destruct(&pending2_); Construct(&ready2_, std::move(*p)); }
    }
    if ((state_ & 8) == 0) {
      auto r = pending3_();
      if (auto* p = r.get_ready()) { state_ |= 8; Destruct(&pending3_); Construct(&ready3_, std::move(*p)); }
    }
    if ((state_ & 16) == 0) {
      auto r = pending4_();
      if (auto* p = r.get_ready()) { state_ |= 16; Destruct(&pending4_); Construct(&ready4_, std::move(*p)); }
    }
    if ((state_ & 32) == 0) {
      auto r = pending5_();
      if (auto* p = r.get_ready()) { state_ |= 32; Destruct(&pending5_); Construct(&ready5_, std::move(*p)); }
    }
    if ((state_ & 64) == 0) {
      auto r = pending6_();
      if (auto* p = r.get_ready()) { state_ |= 64; Destruct(&pending6_); Construct(&ready6_, std::move(*p)); }
    }
    if ((state_ & 128) == 0) {
      auto r = pending7_();
      if (auto* p = r.get_ready()) { state_ |= 128; Destruct(&pending7_); Construct(&ready7_, std::move(*p)); }
    }
    if ((state_ & 256) == 0) {
      auto r = pending8_();
      if (auto* p = r.get_ready()) { state_ |= 256; Destruct(&pending8_); Construct(&ready8_, std::move(*p)); }
    }
    if ((state_ & 512) == 0) {
      auto r = pending9_();
      if (auto* p = r.get_ready()) { state_ |= 512; Destruct(&pending9_); Construct(&ready9_, std::move(*p)); }
    }
    if ((state_ & 1024) == 0) {
      auto r = pending10_();
      if (auto* p = r.get_ready()) { state_ |= 1024; Destruct(&pending10_); Construct(&ready10_, std::move(*p)); }
    }
    if ((state_ & 2048) == 0) {
      auto r = pending11_();
      if (auto* p = r.get_ready()) { state_ |= 2048; Destruct(&pending11_); Construct(&ready11_, std::move(*p)); }
    }
    if ((state_ & 4096) == 0) {
      auto r = pending12_();
      if (auto* p = r.get_ready()) { state_ |= 4096; Destruct(&pending12_); Construct(&ready12_, std::move(*p)); }
    }
    if (state_ != 8191) return kPending;
    return ready(std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12>(std::move(ready0_), std::move(ready1_), std::move(ready2_), std::move(ready3_), std::move(ready4_), std::move(ready5_), std::move(ready6_), std::move(ready7_), std::move(ready8_), std::move(ready9_), std::move(ready10_), std::move(ready11_), std::move(ready12_)));
  }
};
template <typename F0, typename F1, typename F2, typename F3, typename F4, typename F5, typename F6, typename F7, typename F8, typename F9, typename F10, typename F11, typename F12, typename F13> class Join<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13> {
 private:
  [[no_unique_address]] uint16_t state_ = 0;
  using R0 = typename decltype(std::declval<F0>()())::Type;
  union { [[no_unique_address]] F0 pending0_; [[no_unique_address]] R0 ready0_; };
  using R1 = typename decltype(std::declval<F1>()())::Type;
  union { [[no_unique_address]] F1 pending1_; [[no_unique_address]] R1 ready1_; };
  using R2 = typename decltype(std::declval<F2>()())::Type;
  union { [[no_unique_address]] F2 pending2_; [[no_unique_address]] R2 ready2_; };
  using R3 = typename decltype(std::declval<F3>()())::Type;
  union { [[no_unique_address]] F3 pending3_; [[no_unique_address]] R3 ready3_; };
  using R4 = typename decltype(std::declval<F4>()())::Type;
  union { [[no_unique_address]] F4 pending4_; [[no_unique_address]] R4 ready4_; };
  using R5 = typename decltype(std::declval<F5>()())::Type;
  union { [[no_unique_address]] F5 pending5_; [[no_unique_address]] R5 ready5_; };
  using R6 = typename decltype(std::declval<F6>()())::Type;
  union { [[no_unique_address]] F6 pending6_; [[no_unique_address]] R6 ready6_; };
  using R7 = typename decltype(std::declval<F7>()())::Type;
  union { [[no_unique_address]] F7 pending7_; [[no_unique_address]] R7 ready7_; };
  using R8 = typename decltype(std::declval<F8>()())::Type;
  union { [[no_unique_address]] F8 pending8_; [[no_unique_address]] R8 ready8_; };
  using R9 = typename decltype(std::declval<F9>()())::Type;
  union { [[no_unique_address]] F9 pending9_; [[no_unique_address]] R9 ready9_; };
  using R10 = typename decltype(std::declval<F10>()())::Type;
  union { [[no_unique_address]] F10 pending10_; [[no_unique_address]] R10 ready10_; };
  using R11 = typename decltype(std::declval<F11>()())::Type;
  union { [[no_unique_address]] F11 pending11_; [[no_unique_address]] R11 ready11_; };
  using R12 = typename decltype(std::declval<F12>()())::Type;
  union { [[no_unique_address]] F12 pending12_; [[no_unique_address]] R12 ready12_; };
  using R13 = typename decltype(std::declval<F13>()())::Type;
  union { [[no_unique_address]] F13 pending13_; [[no_unique_address]] R13 ready13_; };
 public:
  Join(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7, F8 f8, F9 f9, F10 f10, F11 f11, F12 f12, F13 f13) : pending0_(std::move(f0)), pending1_(std::move(f1)), pending2_(std::move(f2)), pending3_(std::move(f3)), pending4_(std::move(f4)), pending5_(std::move(f5)), pending6_(std::move(f6)), pending7_(std::move(f7)), pending8_(std::move(f8)), pending9_(std::move(f9)), pending10_(std::move(f10)), pending11_(std::move(f11)), pending12_(std::move(f12)), pending13_(std::move(f13)) {}
  Join& operator=(const Join&) = delete;
  Join(const Join& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
  }
  Join(Join&& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
  }
  ~Join() {
    if (state_ & 1) Destruct(&ready0_); else Destruct(&pending0_);
    if (state_ & 2) Destruct(&ready1_); else Destruct(&pending1_);
    if (state_ & 4) Destruct(&ready2_); else Destruct(&pending2_);
    if (state_ & 8) Destruct(&ready3_); else Destruct(&pending3_);
    if (state_ & 16) Destruct(&ready4_); else Destruct(&pending4_);
    if (state_ & 32) Destruct(&ready5_); else Destruct(&pending5_);
    if (state_ & 64) Destruct(&ready6_); else Destruct(&pending6_);
    if (state_ & 128) Destruct(&ready7_); else Destruct(&pending7_);
    if (state_ & 256) Destruct(&ready8_); else Destruct(&pending8_);
    if (state_ & 512) Destruct(&ready9_); else Destruct(&pending9_);
    if (state_ & 1024) Destruct(&ready10_); else Destruct(&pending10_);
    if (state_ & 2048) Destruct(&ready11_); else Destruct(&pending11_);
    if (state_ & 4096) Destruct(&ready12_); else Destruct(&pending12_);
    if (state_ & 8192) Destruct(&ready13_); else Destruct(&pending13_);
  }
  Poll<std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13>> operator()() {
    if ((state_ & 1) == 0) {
      auto r = pending0_();
      if (auto* p = r.get_ready()) { state_ |= 1; Destruct(&pending0_); Construct(&ready0_, std::move(*p)); }
    }
    if ((state_ & 2) == 0) {
      auto r = pending1_();
      if (auto* p = r.get_ready()) { state_ |= 2; Destruct(&pending1_); Construct(&ready1_, std::move(*p)); }
    }
    if ((state_ & 4) == 0) {
      auto r = pending2_();
      if (auto* p = r.get_ready()) { state_ |= 4; Destruct(&pending2_); Construct(&ready2_, std::move(*p)); }
    }
    if ((state_ & 8) == 0) {
      auto r = pending3_();
      if (auto* p = r.get_ready()) { state_ |= 8; Destruct(&pending3_); Construct(&ready3_, std::move(*p)); }
    }
    if ((state_ & 16) == 0) {
      auto r = pending4_();
      if (auto* p = r.get_ready()) { state_ |= 16; Destruct(&pending4_); Construct(&ready4_, std::move(*p)); }
    }
    if ((state_ & 32) == 0) {
      auto r = pending5_();
      if (auto* p = r.get_ready()) { state_ |= 32; Destruct(&pending5_); Construct(&ready5_, std::move(*p)); }
    }
    if ((state_ & 64) == 0) {
      auto r = pending6_();
      if (auto* p = r.get_ready()) { state_ |= 64; Destruct(&pending6_); Construct(&ready6_, std::move(*p)); }
    }
    if ((state_ & 128) == 0) {
      auto r = pending7_();
      if (auto* p = r.get_ready()) { state_ |= 128; Destruct(&pending7_); Construct(&ready7_, std::move(*p)); }
    }
    if ((state_ & 256) == 0) {
      auto r = pending8_();
      if (auto* p = r.get_ready()) { state_ |= 256; Destruct(&pending8_); Construct(&ready8_, std::move(*p)); }
    }
    if ((state_ & 512) == 0) {
      auto r = pending9_();
      if (auto* p = r.get_ready()) { state_ |= 512; Destruct(&pending9_); Construct(&ready9_, std::move(*p)); }
    }
    if ((state_ & 1024) == 0) {
      auto r = pending10_();
      if (auto* p = r.get_ready()) { state_ |= 1024; Destruct(&pending10_); Construct(&ready10_, std::move(*p)); }
    }
    if ((state_ & 2048) == 0) {
      auto r = pending11_();
      if (auto* p = r.get_ready()) { state_ |= 2048; Destruct(&pending11_); Construct(&ready11_, std::move(*p)); }
    }
    if ((state_ & 4096) == 0) {
      auto r = pending12_();
      if (auto* p = r.get_ready()) { state_ |= 4096; Destruct(&pending12_); Construct(&ready12_, std::move(*p)); }
    }
    if ((state_ & 8192) == 0) {
      auto r = pending13_();
      if (auto* p = r.get_ready()) { state_ |= 8192; Destruct(&pending13_); Construct(&ready13_, std::move(*p)); }
    }
    if (state_ != 16383) return kPending;
    return ready(std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13>(std::move(ready0_), std::move(ready1_), std::move(ready2_), std::move(ready3_), std::move(ready4_), std::move(ready5_), std::move(ready6_), std::move(ready7_), std::move(ready8_), std::move(ready9_), std::move(ready10_), std::move(ready11_), std::move(ready12_), std::move(ready13_)));
  }
};
template <typename F0, typename F1, typename F2, typename F3, typename F4, typename F5, typename F6, typename F7, typename F8, typename F9, typename F10, typename F11, typename F12, typename F13, typename F14> class Join<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14> {
 private:
  [[no_unique_address]] uint16_t state_ = 0;
  using R0 = typename decltype(std::declval<F0>()())::Type;
  union { [[no_unique_address]] F0 pending0_; [[no_unique_address]] R0 ready0_; };
  using R1 = typename decltype(std::declval<F1>()())::Type;
  union { [[no_unique_address]] F1 pending1_; [[no_unique_address]] R1 ready1_; };
  using R2 = typename decltype(std::declval<F2>()())::Type;
  union { [[no_unique_address]] F2 pending2_; [[no_unique_address]] R2 ready2_; };
  using R3 = typename decltype(std::declval<F3>()())::Type;
  union { [[no_unique_address]] F3 pending3_; [[no_unique_address]] R3 ready3_; };
  using R4 = typename decltype(std::declval<F4>()())::Type;
  union { [[no_unique_address]] F4 pending4_; [[no_unique_address]] R4 ready4_; };
  using R5 = typename decltype(std::declval<F5>()())::Type;
  union { [[no_unique_address]] F5 pending5_; [[no_unique_address]] R5 ready5_; };
  using R6 = typename decltype(std::declval<F6>()())::Type;
  union { [[no_unique_address]] F6 pending6_; [[no_unique_address]] R6 ready6_; };
  using R7 = typename decltype(std::declval<F7>()())::Type;
  union { [[no_unique_address]] F7 pending7_; [[no_unique_address]] R7 ready7_; };
  using R8 = typename decltype(std::declval<F8>()())::Type;
  union { [[no_unique_address]] F8 pending8_; [[no_unique_address]] R8 ready8_; };
  using R9 = typename decltype(std::declval<F9>()())::Type;
  union { [[no_unique_address]] F9 pending9_; [[no_unique_address]] R9 ready9_; };
  using R10 = typename decltype(std::declval<F10>()())::Type;
  union { [[no_unique_address]] F10 pending10_; [[no_unique_address]] R10 ready10_; };
  using R11 = typename decltype(std::declval<F11>()())::Type;
  union { [[no_unique_address]] F11 pending11_; [[no_unique_address]] R11 ready11_; };
  using R12 = typename decltype(std::declval<F12>()())::Type;
  union { [[no_unique_address]] F12 pending12_; [[no_unique_address]] R12 ready12_; };
  using R13 = typename decltype(std::declval<F13>()())::Type;
  union { [[no_unique_address]] F13 pending13_; [[no_unique_address]] R13 ready13_; };
  using R14 = typename decltype(std::declval<F14>()())::Type;
  union { [[no_unique_address]] F14 pending14_; [[no_unique_address]] R14 ready14_; };
 public:
  Join(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7, F8 f8, F9 f9, F10 f10, F11 f11, F12 f12, F13 f13, F14 f14) : pending0_(std::move(f0)), pending1_(std::move(f1)), pending2_(std::move(f2)), pending3_(std::move(f3)), pending4_(std::move(f4)), pending5_(std::move(f5)), pending6_(std::move(f6)), pending7_(std::move(f7)), pending8_(std::move(f8)), pending9_(std::move(f9)), pending10_(std::move(f10)), pending11_(std::move(f11)), pending12_(std::move(f12)), pending13_(std::move(f13)), pending14_(std::move(f14)) {}
  Join& operator=(const Join&) = delete;
  Join(const Join& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
  }
  Join(Join&& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
  }
  ~Join() {
    if (state_ & 1) Destruct(&ready0_); else Destruct(&pending0_);
    if (state_ & 2) Destruct(&ready1_); else Destruct(&pending1_);
    if (state_ & 4) Destruct(&ready2_); else Destruct(&pending2_);
    if (state_ & 8) Destruct(&ready3_); else Destruct(&pending3_);
    if (state_ & 16) Destruct(&ready4_); else Destruct(&pending4_);
    if (state_ & 32) Destruct(&ready5_); else Destruct(&pending5_);
    if (state_ & 64) Destruct(&ready6_); else Destruct(&pending6_);
    if (state_ & 128) Destruct(&ready7_); else Destruct(&pending7_);
    if (state_ & 256) Destruct(&ready8_); else Destruct(&pending8_);
    if (state_ & 512) Destruct(&ready9_); else Destruct(&pending9_);
    if (state_ & 1024) Destruct(&ready10_); else Destruct(&pending10_);
    if (state_ & 2048) Destruct(&ready11_); else Destruct(&pending11_);
    if (state_ & 4096) Destruct(&ready12_); else Destruct(&pending12_);
    if (state_ & 8192) Destruct(&ready13_); else Destruct(&pending13_);
    if (state_ & 16384) Destruct(&ready14_); else Destruct(&pending14_);
  }
  Poll<std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14>> operator()() {
    if ((state_ & 1) == 0) {
      auto r = pending0_();
      if (auto* p = r.get_ready()) { state_ |= 1; Destruct(&pending0_); Construct(&ready0_, std::move(*p)); }
    }
    if ((state_ & 2) == 0) {
      auto r = pending1_();
      if (auto* p = r.get_ready()) { state_ |= 2; Destruct(&pending1_); Construct(&ready1_, std::move(*p)); }
    }
    if ((state_ & 4) == 0) {
      auto r = pending2_();
      if (auto* p = r.get_ready()) { state_ |= 4; Destruct(&pending2_); Construct(&ready2_, std::move(*p)); }
    }
    if ((state_ & 8) == 0) {
      auto r = pending3_();
      if (auto* p = r.get_ready()) { state_ |= 8; Destruct(&pending3_); Construct(&ready3_, std::move(*p)); }
    }
    if ((state_ & 16) == 0) {
      auto r = pending4_();
      if (auto* p = r.get_ready()) { state_ |= 16; Destruct(&pending4_); Construct(&ready4_, std::move(*p)); }
    }
    if ((state_ & 32) == 0) {
      auto r = pending5_();
      if (auto* p = r.get_ready()) { state_ |= 32; Destruct(&pending5_); Construct(&ready5_, std::move(*p)); }
    }
    if ((state_ & 64) == 0) {
      auto r = pending6_();
      if (auto* p = r.get_ready()) { state_ |= 64; Destruct(&pending6_); Construct(&ready6_, std::move(*p)); }
    }
    if ((state_ & 128) == 0) {
      auto r = pending7_();
      if (auto* p = r.get_ready()) { state_ |= 128; Destruct(&pending7_); Construct(&ready7_, std::move(*p)); }
    }
    if ((state_ & 256) == 0) {
      auto r = pending8_();
      if (auto* p = r.get_ready()) { state_ |= 256; Destruct(&pending8_); Construct(&ready8_, std::move(*p)); }
    }
    if ((state_ & 512) == 0) {
      auto r = pending9_();
      if (auto* p = r.get_ready()) { state_ |= 512; Destruct(&pending9_); Construct(&ready9_, std::move(*p)); }
    }
    if ((state_ & 1024) == 0) {
      auto r = pending10_();
      if (auto* p = r.get_ready()) { state_ |= 1024; Destruct(&pending10_); Construct(&ready10_, std::move(*p)); }
    }
    if ((state_ & 2048) == 0) {
      auto r = pending11_();
      if (auto* p = r.get_ready()) { state_ |= 2048; Destruct(&pending11_); Construct(&ready11_, std::move(*p)); }
    }
    if ((state_ & 4096) == 0) {
      auto r = pending12_();
      if (auto* p = r.get_ready()) { state_ |= 4096; Destruct(&pending12_); Construct(&ready12_, std::move(*p)); }
    }
    if ((state_ & 8192) == 0) {
      auto r = pending13_();
      if (auto* p = r.get_ready()) { state_ |= 8192; Destruct(&pending13_); Construct(&ready13_, std::move(*p)); }
    }
    if ((state_ & 16384) == 0) {
      auto r = pending14_();
      if (auto* p = r.get_ready()) { state_ |= 16384; Destruct(&pending14_); Construct(&ready14_, std::move(*p)); }
    }
    if (state_ != 32767) return kPending;
    return ready(std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14>(std::move(ready0_), std::move(ready1_), std::move(ready2_), std::move(ready3_), std::move(ready4_), std::move(ready5_), std::move(ready6_), std::move(ready7_), std::move(ready8_), std::move(ready9_), std::move(ready10_), std::move(ready11_), std::move(ready12_), std::move(ready13_), std::move(ready14_)));
  }
};
template <typename F0, typename F1, typename F2, typename F3, typename F4, typename F5, typename F6, typename F7, typename F8, typename F9, typename F10, typename F11, typename F12, typename F13, typename F14, typename F15> class Join<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15> {
 private:
  [[no_unique_address]] uint16_t state_ = 0;
  using R0 = typename decltype(std::declval<F0>()())::Type;
  union { [[no_unique_address]] F0 pending0_; [[no_unique_address]] R0 ready0_; };
  using R1 = typename decltype(std::declval<F1>()())::Type;
  union { [[no_unique_address]] F1 pending1_; [[no_unique_address]] R1 ready1_; };
  using R2 = typename decltype(std::declval<F2>()())::Type;
  union { [[no_unique_address]] F2 pending2_; [[no_unique_address]] R2 ready2_; };
  using R3 = typename decltype(std::declval<F3>()())::Type;
  union { [[no_unique_address]] F3 pending3_; [[no_unique_address]] R3 ready3_; };
  using R4 = typename decltype(std::declval<F4>()())::Type;
  union { [[no_unique_address]] F4 pending4_; [[no_unique_address]] R4 ready4_; };
  using R5 = typename decltype(std::declval<F5>()())::Type;
  union { [[no_unique_address]] F5 pending5_; [[no_unique_address]] R5 ready5_; };
  using R6 = typename decltype(std::declval<F6>()())::Type;
  union { [[no_unique_address]] F6 pending6_; [[no_unique_address]] R6 ready6_; };
  using R7 = typename decltype(std::declval<F7>()())::Type;
  union { [[no_unique_address]] F7 pending7_; [[no_unique_address]] R7 ready7_; };
  using R8 = typename decltype(std::declval<F8>()())::Type;
  union { [[no_unique_address]] F8 pending8_; [[no_unique_address]] R8 ready8_; };
  using R9 = typename decltype(std::declval<F9>()())::Type;
  union { [[no_unique_address]] F9 pending9_; [[no_unique_address]] R9 ready9_; };
  using R10 = typename decltype(std::declval<F10>()())::Type;
  union { [[no_unique_address]] F10 pending10_; [[no_unique_address]] R10 ready10_; };
  using R11 = typename decltype(std::declval<F11>()())::Type;
  union { [[no_unique_address]] F11 pending11_; [[no_unique_address]] R11 ready11_; };
  using R12 = typename decltype(std::declval<F12>()())::Type;
  union { [[no_unique_address]] F12 pending12_; [[no_unique_address]] R12 ready12_; };
  using R13 = typename decltype(std::declval<F13>()())::Type;
  union { [[no_unique_address]] F13 pending13_; [[no_unique_address]] R13 ready13_; };
  using R14 = typename decltype(std::declval<F14>()())::Type;
  union { [[no_unique_address]] F14 pending14_; [[no_unique_address]] R14 ready14_; };
  using R15 = typename decltype(std::declval<F15>()())::Type;
  union { [[no_unique_address]] F15 pending15_; [[no_unique_address]] R15 ready15_; };
 public:
  Join(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7, F8 f8, F9 f9, F10 f10, F11 f11, F12 f12, F13 f13, F14 f14, F15 f15) : pending0_(std::move(f0)), pending1_(std::move(f1)), pending2_(std::move(f2)), pending3_(std::move(f3)), pending4_(std::move(f4)), pending5_(std::move(f5)), pending6_(std::move(f6)), pending7_(std::move(f7)), pending8_(std::move(f8)), pending9_(std::move(f9)), pending10_(std::move(f10)), pending11_(std::move(f11)), pending12_(std::move(f12)), pending13_(std::move(f13)), pending14_(std::move(f14)), pending15_(std::move(f15)) {}
  Join& operator=(const Join&) = delete;
  Join(const Join& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
    Construct(&pending15_, std::move(other.pending15_));
  }
  Join(Join&& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
    Construct(&pending15_, std::move(other.pending15_));
  }
  ~Join() {
    if (state_ & 1) Destruct(&ready0_); else Destruct(&pending0_);
    if (state_ & 2) Destruct(&ready1_); else Destruct(&pending1_);
    if (state_ & 4) Destruct(&ready2_); else Destruct(&pending2_);
    if (state_ & 8) Destruct(&ready3_); else Destruct(&pending3_);
    if (state_ & 16) Destruct(&ready4_); else Destruct(&pending4_);
    if (state_ & 32) Destruct(&ready5_); else Destruct(&pending5_);
    if (state_ & 64) Destruct(&ready6_); else Destruct(&pending6_);
    if (state_ & 128) Destruct(&ready7_); else Destruct(&pending7_);
    if (state_ & 256) Destruct(&ready8_); else Destruct(&pending8_);
    if (state_ & 512) Destruct(&ready9_); else Destruct(&pending9_);
    if (state_ & 1024) Destruct(&ready10_); else Destruct(&pending10_);
    if (state_ & 2048) Destruct(&ready11_); else Destruct(&pending11_);
    if (state_ & 4096) Destruct(&ready12_); else Destruct(&pending12_);
    if (state_ & 8192) Destruct(&ready13_); else Destruct(&pending13_);
    if (state_ & 16384) Destruct(&ready14_); else Destruct(&pending14_);
    if (state_ & 32768) Destruct(&ready15_); else Destruct(&pending15_);
  }
  Poll<std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15>> operator()() {
    if ((state_ & 1) == 0) {
      auto r = pending0_();
      if (auto* p = r.get_ready()) { state_ |= 1; Destruct(&pending0_); Construct(&ready0_, std::move(*p)); }
    }
    if ((state_ & 2) == 0) {
      auto r = pending1_();
      if (auto* p = r.get_ready()) { state_ |= 2; Destruct(&pending1_); Construct(&ready1_, std::move(*p)); }
    }
    if ((state_ & 4) == 0) {
      auto r = pending2_();
      if (auto* p = r.get_ready()) { state_ |= 4; Destruct(&pending2_); Construct(&ready2_, std::move(*p)); }
    }
    if ((state_ & 8) == 0) {
      auto r = pending3_();
      if (auto* p = r.get_ready()) { state_ |= 8; Destruct(&pending3_); Construct(&ready3_, std::move(*p)); }
    }
    if ((state_ & 16) == 0) {
      auto r = pending4_();
      if (auto* p = r.get_ready()) { state_ |= 16; Destruct(&pending4_); Construct(&ready4_, std::move(*p)); }
    }
    if ((state_ & 32) == 0) {
      auto r = pending5_();
      if (auto* p = r.get_ready()) { state_ |= 32; Destruct(&pending5_); Construct(&ready5_, std::move(*p)); }
    }
    if ((state_ & 64) == 0) {
      auto r = pending6_();
      if (auto* p = r.get_ready()) { state_ |= 64; Destruct(&pending6_); Construct(&ready6_, std::move(*p)); }
    }
    if ((state_ & 128) == 0) {
      auto r = pending7_();
      if (auto* p = r.get_ready()) { state_ |= 128; Destruct(&pending7_); Construct(&ready7_, std::move(*p)); }
    }
    if ((state_ & 256) == 0) {
      auto r = pending8_();
      if (auto* p = r.get_ready()) { state_ |= 256; Destruct(&pending8_); Construct(&ready8_, std::move(*p)); }
    }
    if ((state_ & 512) == 0) {
      auto r = pending9_();
      if (auto* p = r.get_ready()) { state_ |= 512; Destruct(&pending9_); Construct(&ready9_, std::move(*p)); }
    }
    if ((state_ & 1024) == 0) {
      auto r = pending10_();
      if (auto* p = r.get_ready()) { state_ |= 1024; Destruct(&pending10_); Construct(&ready10_, std::move(*p)); }
    }
    if ((state_ & 2048) == 0) {
      auto r = pending11_();
      if (auto* p = r.get_ready()) { state_ |= 2048; Destruct(&pending11_); Construct(&ready11_, std::move(*p)); }
    }
    if ((state_ & 4096) == 0) {
      auto r = pending12_();
      if (auto* p = r.get_ready()) { state_ |= 4096; Destruct(&pending12_); Construct(&ready12_, std::move(*p)); }
    }
    if ((state_ & 8192) == 0) {
      auto r = pending13_();
      if (auto* p = r.get_ready()) { state_ |= 8192; Destruct(&pending13_); Construct(&ready13_, std::move(*p)); }
    }
    if ((state_ & 16384) == 0) {
      auto r = pending14_();
      if (auto* p = r.get_ready()) { state_ |= 16384; Destruct(&pending14_); Construct(&ready14_, std::move(*p)); }
    }
    if ((state_ & 32768) == 0) {
      auto r = pending15_();
      if (auto* p = r.get_ready()) { state_ |= 32768; Destruct(&pending15_); Construct(&ready15_, std::move(*p)); }
    }
    if (state_ != 65535) return kPending;
    return ready(std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15>(std::move(ready0_), std::move(ready1_), std::move(ready2_), std::move(ready3_), std::move(ready4_), std::move(ready5_), std::move(ready6_), std::move(ready7_), std::move(ready8_), std::move(ready9_), std::move(ready10_), std::move(ready11_), std::move(ready12_), std::move(ready13_), std::move(ready14_), std::move(ready15_)));
  }
};
template <typename F0, typename F1, typename F2, typename F3, typename F4, typename F5, typename F6, typename F7, typename F8, typename F9, typename F10, typename F11, typename F12, typename F13, typename F14, typename F15, typename F16> class Join<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15, F16> {
 private:
  [[no_unique_address]] uint32_t state_ = 0;
  using R0 = typename decltype(std::declval<F0>()())::Type;
  union { [[no_unique_address]] F0 pending0_; [[no_unique_address]] R0 ready0_; };
  using R1 = typename decltype(std::declval<F1>()())::Type;
  union { [[no_unique_address]] F1 pending1_; [[no_unique_address]] R1 ready1_; };
  using R2 = typename decltype(std::declval<F2>()())::Type;
  union { [[no_unique_address]] F2 pending2_; [[no_unique_address]] R2 ready2_; };
  using R3 = typename decltype(std::declval<F3>()())::Type;
  union { [[no_unique_address]] F3 pending3_; [[no_unique_address]] R3 ready3_; };
  using R4 = typename decltype(std::declval<F4>()())::Type;
  union { [[no_unique_address]] F4 pending4_; [[no_unique_address]] R4 ready4_; };
  using R5 = typename decltype(std::declval<F5>()())::Type;
  union { [[no_unique_address]] F5 pending5_; [[no_unique_address]] R5 ready5_; };
  using R6 = typename decltype(std::declval<F6>()())::Type;
  union { [[no_unique_address]] F6 pending6_; [[no_unique_address]] R6 ready6_; };
  using R7 = typename decltype(std::declval<F7>()())::Type;
  union { [[no_unique_address]] F7 pending7_; [[no_unique_address]] R7 ready7_; };
  using R8 = typename decltype(std::declval<F8>()())::Type;
  union { [[no_unique_address]] F8 pending8_; [[no_unique_address]] R8 ready8_; };
  using R9 = typename decltype(std::declval<F9>()())::Type;
  union { [[no_unique_address]] F9 pending9_; [[no_unique_address]] R9 ready9_; };
  using R10 = typename decltype(std::declval<F10>()())::Type;
  union { [[no_unique_address]] F10 pending10_; [[no_unique_address]] R10 ready10_; };
  using R11 = typename decltype(std::declval<F11>()())::Type;
  union { [[no_unique_address]] F11 pending11_; [[no_unique_address]] R11 ready11_; };
  using R12 = typename decltype(std::declval<F12>()())::Type;
  union { [[no_unique_address]] F12 pending12_; [[no_unique_address]] R12 ready12_; };
  using R13 = typename decltype(std::declval<F13>()())::Type;
  union { [[no_unique_address]] F13 pending13_; [[no_unique_address]] R13 ready13_; };
  using R14 = typename decltype(std::declval<F14>()())::Type;
  union { [[no_unique_address]] F14 pending14_; [[no_unique_address]] R14 ready14_; };
  using R15 = typename decltype(std::declval<F15>()())::Type;
  union { [[no_unique_address]] F15 pending15_; [[no_unique_address]] R15 ready15_; };
  using R16 = typename decltype(std::declval<F16>()())::Type;
  union { [[no_unique_address]] F16 pending16_; [[no_unique_address]] R16 ready16_; };
 public:
  Join(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7, F8 f8, F9 f9, F10 f10, F11 f11, F12 f12, F13 f13, F14 f14, F15 f15, F16 f16) : pending0_(std::move(f0)), pending1_(std::move(f1)), pending2_(std::move(f2)), pending3_(std::move(f3)), pending4_(std::move(f4)), pending5_(std::move(f5)), pending6_(std::move(f6)), pending7_(std::move(f7)), pending8_(std::move(f8)), pending9_(std::move(f9)), pending10_(std::move(f10)), pending11_(std::move(f11)), pending12_(std::move(f12)), pending13_(std::move(f13)), pending14_(std::move(f14)), pending15_(std::move(f15)), pending16_(std::move(f16)) {}
  Join& operator=(const Join&) = delete;
  Join(const Join& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
    Construct(&pending15_, std::move(other.pending15_));
    Construct(&pending16_, std::move(other.pending16_));
  }
  Join(Join&& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
    Construct(&pending15_, std::move(other.pending15_));
    Construct(&pending16_, std::move(other.pending16_));
  }
  ~Join() {
    if (state_ & 1) Destruct(&ready0_); else Destruct(&pending0_);
    if (state_ & 2) Destruct(&ready1_); else Destruct(&pending1_);
    if (state_ & 4) Destruct(&ready2_); else Destruct(&pending2_);
    if (state_ & 8) Destruct(&ready3_); else Destruct(&pending3_);
    if (state_ & 16) Destruct(&ready4_); else Destruct(&pending4_);
    if (state_ & 32) Destruct(&ready5_); else Destruct(&pending5_);
    if (state_ & 64) Destruct(&ready6_); else Destruct(&pending6_);
    if (state_ & 128) Destruct(&ready7_); else Destruct(&pending7_);
    if (state_ & 256) Destruct(&ready8_); else Destruct(&pending8_);
    if (state_ & 512) Destruct(&ready9_); else Destruct(&pending9_);
    if (state_ & 1024) Destruct(&ready10_); else Destruct(&pending10_);
    if (state_ & 2048) Destruct(&ready11_); else Destruct(&pending11_);
    if (state_ & 4096) Destruct(&ready12_); else Destruct(&pending12_);
    if (state_ & 8192) Destruct(&ready13_); else Destruct(&pending13_);
    if (state_ & 16384) Destruct(&ready14_); else Destruct(&pending14_);
    if (state_ & 32768) Destruct(&ready15_); else Destruct(&pending15_);
    if (state_ & 65536) Destruct(&ready16_); else Destruct(&pending16_);
  }
  Poll<std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16>> operator()() {
    if ((state_ & 1) == 0) {
      auto r = pending0_();
      if (auto* p = r.get_ready()) { state_ |= 1; Destruct(&pending0_); Construct(&ready0_, std::move(*p)); }
    }
    if ((state_ & 2) == 0) {
      auto r = pending1_();
      if (auto* p = r.get_ready()) { state_ |= 2; Destruct(&pending1_); Construct(&ready1_, std::move(*p)); }
    }
    if ((state_ & 4) == 0) {
      auto r = pending2_();
      if (auto* p = r.get_ready()) { state_ |= 4; Destruct(&pending2_); Construct(&ready2_, std::move(*p)); }
    }
    if ((state_ & 8) == 0) {
      auto r = pending3_();
      if (auto* p = r.get_ready()) { state_ |= 8; Destruct(&pending3_); Construct(&ready3_, std::move(*p)); }
    }
    if ((state_ & 16) == 0) {
      auto r = pending4_();
      if (auto* p = r.get_ready()) { state_ |= 16; Destruct(&pending4_); Construct(&ready4_, std::move(*p)); }
    }
    if ((state_ & 32) == 0) {
      auto r = pending5_();
      if (auto* p = r.get_ready()) { state_ |= 32; Destruct(&pending5_); Construct(&ready5_, std::move(*p)); }
    }
    if ((state_ & 64) == 0) {
      auto r = pending6_();
      if (auto* p = r.get_ready()) { state_ |= 64; Destruct(&pending6_); Construct(&ready6_, std::move(*p)); }
    }
    if ((state_ & 128) == 0) {
      auto r = pending7_();
      if (auto* p = r.get_ready()) { state_ |= 128; Destruct(&pending7_); Construct(&ready7_, std::move(*p)); }
    }
    if ((state_ & 256) == 0) {
      auto r = pending8_();
      if (auto* p = r.get_ready()) { state_ |= 256; Destruct(&pending8_); Construct(&ready8_, std::move(*p)); }
    }
    if ((state_ & 512) == 0) {
      auto r = pending9_();
      if (auto* p = r.get_ready()) { state_ |= 512; Destruct(&pending9_); Construct(&ready9_, std::move(*p)); }
    }
    if ((state_ & 1024) == 0) {
      auto r = pending10_();
      if (auto* p = r.get_ready()) { state_ |= 1024; Destruct(&pending10_); Construct(&ready10_, std::move(*p)); }
    }
    if ((state_ & 2048) == 0) {
      auto r = pending11_();
      if (auto* p = r.get_ready()) { state_ |= 2048; Destruct(&pending11_); Construct(&ready11_, std::move(*p)); }
    }
    if ((state_ & 4096) == 0) {
      auto r = pending12_();
      if (auto* p = r.get_ready()) { state_ |= 4096; Destruct(&pending12_); Construct(&ready12_, std::move(*p)); }
    }
    if ((state_ & 8192) == 0) {
      auto r = pending13_();
      if (auto* p = r.get_ready()) { state_ |= 8192; Destruct(&pending13_); Construct(&ready13_, std::move(*p)); }
    }
    if ((state_ & 16384) == 0) {
      auto r = pending14_();
      if (auto* p = r.get_ready()) { state_ |= 16384; Destruct(&pending14_); Construct(&ready14_, std::move(*p)); }
    }
    if ((state_ & 32768) == 0) {
      auto r = pending15_();
      if (auto* p = r.get_ready()) { state_ |= 32768; Destruct(&pending15_); Construct(&ready15_, std::move(*p)); }
    }
    if ((state_ & 65536) == 0) {
      auto r = pending16_();
      if (auto* p = r.get_ready()) { state_ |= 65536; Destruct(&pending16_); Construct(&ready16_, std::move(*p)); }
    }
    if (state_ != 131071) return kPending;
    return ready(std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16>(std::move(ready0_), std::move(ready1_), std::move(ready2_), std::move(ready3_), std::move(ready4_), std::move(ready5_), std::move(ready6_), std::move(ready7_), std::move(ready8_), std::move(ready9_), std::move(ready10_), std::move(ready11_), std::move(ready12_), std::move(ready13_), std::move(ready14_), std::move(ready15_), std::move(ready16_)));
  }
};
template <typename F0, typename F1, typename F2, typename F3, typename F4, typename F5, typename F6, typename F7, typename F8, typename F9, typename F10, typename F11, typename F12, typename F13, typename F14, typename F15, typename F16, typename F17> class Join<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15, F16, F17> {
 private:
  [[no_unique_address]] uint32_t state_ = 0;
  using R0 = typename decltype(std::declval<F0>()())::Type;
  union { [[no_unique_address]] F0 pending0_; [[no_unique_address]] R0 ready0_; };
  using R1 = typename decltype(std::declval<F1>()())::Type;
  union { [[no_unique_address]] F1 pending1_; [[no_unique_address]] R1 ready1_; };
  using R2 = typename decltype(std::declval<F2>()())::Type;
  union { [[no_unique_address]] F2 pending2_; [[no_unique_address]] R2 ready2_; };
  using R3 = typename decltype(std::declval<F3>()())::Type;
  union { [[no_unique_address]] F3 pending3_; [[no_unique_address]] R3 ready3_; };
  using R4 = typename decltype(std::declval<F4>()())::Type;
  union { [[no_unique_address]] F4 pending4_; [[no_unique_address]] R4 ready4_; };
  using R5 = typename decltype(std::declval<F5>()())::Type;
  union { [[no_unique_address]] F5 pending5_; [[no_unique_address]] R5 ready5_; };
  using R6 = typename decltype(std::declval<F6>()())::Type;
  union { [[no_unique_address]] F6 pending6_; [[no_unique_address]] R6 ready6_; };
  using R7 = typename decltype(std::declval<F7>()())::Type;
  union { [[no_unique_address]] F7 pending7_; [[no_unique_address]] R7 ready7_; };
  using R8 = typename decltype(std::declval<F8>()())::Type;
  union { [[no_unique_address]] F8 pending8_; [[no_unique_address]] R8 ready8_; };
  using R9 = typename decltype(std::declval<F9>()())::Type;
  union { [[no_unique_address]] F9 pending9_; [[no_unique_address]] R9 ready9_; };
  using R10 = typename decltype(std::declval<F10>()())::Type;
  union { [[no_unique_address]] F10 pending10_; [[no_unique_address]] R10 ready10_; };
  using R11 = typename decltype(std::declval<F11>()())::Type;
  union { [[no_unique_address]] F11 pending11_; [[no_unique_address]] R11 ready11_; };
  using R12 = typename decltype(std::declval<F12>()())::Type;
  union { [[no_unique_address]] F12 pending12_; [[no_unique_address]] R12 ready12_; };
  using R13 = typename decltype(std::declval<F13>()())::Type;
  union { [[no_unique_address]] F13 pending13_; [[no_unique_address]] R13 ready13_; };
  using R14 = typename decltype(std::declval<F14>()())::Type;
  union { [[no_unique_address]] F14 pending14_; [[no_unique_address]] R14 ready14_; };
  using R15 = typename decltype(std::declval<F15>()())::Type;
  union { [[no_unique_address]] F15 pending15_; [[no_unique_address]] R15 ready15_; };
  using R16 = typename decltype(std::declval<F16>()())::Type;
  union { [[no_unique_address]] F16 pending16_; [[no_unique_address]] R16 ready16_; };
  using R17 = typename decltype(std::declval<F17>()())::Type;
  union { [[no_unique_address]] F17 pending17_; [[no_unique_address]] R17 ready17_; };
 public:
  Join(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7, F8 f8, F9 f9, F10 f10, F11 f11, F12 f12, F13 f13, F14 f14, F15 f15, F16 f16, F17 f17) : pending0_(std::move(f0)), pending1_(std::move(f1)), pending2_(std::move(f2)), pending3_(std::move(f3)), pending4_(std::move(f4)), pending5_(std::move(f5)), pending6_(std::move(f6)), pending7_(std::move(f7)), pending8_(std::move(f8)), pending9_(std::move(f9)), pending10_(std::move(f10)), pending11_(std::move(f11)), pending12_(std::move(f12)), pending13_(std::move(f13)), pending14_(std::move(f14)), pending15_(std::move(f15)), pending16_(std::move(f16)), pending17_(std::move(f17)) {}
  Join& operator=(const Join&) = delete;
  Join(const Join& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
    Construct(&pending15_, std::move(other.pending15_));
    Construct(&pending16_, std::move(other.pending16_));
    Construct(&pending17_, std::move(other.pending17_));
  }
  Join(Join&& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
    Construct(&pending15_, std::move(other.pending15_));
    Construct(&pending16_, std::move(other.pending16_));
    Construct(&pending17_, std::move(other.pending17_));
  }
  ~Join() {
    if (state_ & 1) Destruct(&ready0_); else Destruct(&pending0_);
    if (state_ & 2) Destruct(&ready1_); else Destruct(&pending1_);
    if (state_ & 4) Destruct(&ready2_); else Destruct(&pending2_);
    if (state_ & 8) Destruct(&ready3_); else Destruct(&pending3_);
    if (state_ & 16) Destruct(&ready4_); else Destruct(&pending4_);
    if (state_ & 32) Destruct(&ready5_); else Destruct(&pending5_);
    if (state_ & 64) Destruct(&ready6_); else Destruct(&pending6_);
    if (state_ & 128) Destruct(&ready7_); else Destruct(&pending7_);
    if (state_ & 256) Destruct(&ready8_); else Destruct(&pending8_);
    if (state_ & 512) Destruct(&ready9_); else Destruct(&pending9_);
    if (state_ & 1024) Destruct(&ready10_); else Destruct(&pending10_);
    if (state_ & 2048) Destruct(&ready11_); else Destruct(&pending11_);
    if (state_ & 4096) Destruct(&ready12_); else Destruct(&pending12_);
    if (state_ & 8192) Destruct(&ready13_); else Destruct(&pending13_);
    if (state_ & 16384) Destruct(&ready14_); else Destruct(&pending14_);
    if (state_ & 32768) Destruct(&ready15_); else Destruct(&pending15_);
    if (state_ & 65536) Destruct(&ready16_); else Destruct(&pending16_);
    if (state_ & 131072) Destruct(&ready17_); else Destruct(&pending17_);
  }
  Poll<std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16, R17>> operator()() {
    if ((state_ & 1) == 0) {
      auto r = pending0_();
      if (auto* p = r.get_ready()) { state_ |= 1; Destruct(&pending0_); Construct(&ready0_, std::move(*p)); }
    }
    if ((state_ & 2) == 0) {
      auto r = pending1_();
      if (auto* p = r.get_ready()) { state_ |= 2; Destruct(&pending1_); Construct(&ready1_, std::move(*p)); }
    }
    if ((state_ & 4) == 0) {
      auto r = pending2_();
      if (auto* p = r.get_ready()) { state_ |= 4; Destruct(&pending2_); Construct(&ready2_, std::move(*p)); }
    }
    if ((state_ & 8) == 0) {
      auto r = pending3_();
      if (auto* p = r.get_ready()) { state_ |= 8; Destruct(&pending3_); Construct(&ready3_, std::move(*p)); }
    }
    if ((state_ & 16) == 0) {
      auto r = pending4_();
      if (auto* p = r.get_ready()) { state_ |= 16; Destruct(&pending4_); Construct(&ready4_, std::move(*p)); }
    }
    if ((state_ & 32) == 0) {
      auto r = pending5_();
      if (auto* p = r.get_ready()) { state_ |= 32; Destruct(&pending5_); Construct(&ready5_, std::move(*p)); }
    }
    if ((state_ & 64) == 0) {
      auto r = pending6_();
      if (auto* p = r.get_ready()) { state_ |= 64; Destruct(&pending6_); Construct(&ready6_, std::move(*p)); }
    }
    if ((state_ & 128) == 0) {
      auto r = pending7_();
      if (auto* p = r.get_ready()) { state_ |= 128; Destruct(&pending7_); Construct(&ready7_, std::move(*p)); }
    }
    if ((state_ & 256) == 0) {
      auto r = pending8_();
      if (auto* p = r.get_ready()) { state_ |= 256; Destruct(&pending8_); Construct(&ready8_, std::move(*p)); }
    }
    if ((state_ & 512) == 0) {
      auto r = pending9_();
      if (auto* p = r.get_ready()) { state_ |= 512; Destruct(&pending9_); Construct(&ready9_, std::move(*p)); }
    }
    if ((state_ & 1024) == 0) {
      auto r = pending10_();
      if (auto* p = r.get_ready()) { state_ |= 1024; Destruct(&pending10_); Construct(&ready10_, std::move(*p)); }
    }
    if ((state_ & 2048) == 0) {
      auto r = pending11_();
      if (auto* p = r.get_ready()) { state_ |= 2048; Destruct(&pending11_); Construct(&ready11_, std::move(*p)); }
    }
    if ((state_ & 4096) == 0) {
      auto r = pending12_();
      if (auto* p = r.get_ready()) { state_ |= 4096; Destruct(&pending12_); Construct(&ready12_, std::move(*p)); }
    }
    if ((state_ & 8192) == 0) {
      auto r = pending13_();
      if (auto* p = r.get_ready()) { state_ |= 8192; Destruct(&pending13_); Construct(&ready13_, std::move(*p)); }
    }
    if ((state_ & 16384) == 0) {
      auto r = pending14_();
      if (auto* p = r.get_ready()) { state_ |= 16384; Destruct(&pending14_); Construct(&ready14_, std::move(*p)); }
    }
    if ((state_ & 32768) == 0) {
      auto r = pending15_();
      if (auto* p = r.get_ready()) { state_ |= 32768; Destruct(&pending15_); Construct(&ready15_, std::move(*p)); }
    }
    if ((state_ & 65536) == 0) {
      auto r = pending16_();
      if (auto* p = r.get_ready()) { state_ |= 65536; Destruct(&pending16_); Construct(&ready16_, std::move(*p)); }
    }
    if ((state_ & 131072) == 0) {
      auto r = pending17_();
      if (auto* p = r.get_ready()) { state_ |= 131072; Destruct(&pending17_); Construct(&ready17_, std::move(*p)); }
    }
    if (state_ != 262143) return kPending;
    return ready(std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16, R17>(std::move(ready0_), std::move(ready1_), std::move(ready2_), std::move(ready3_), std::move(ready4_), std::move(ready5_), std::move(ready6_), std::move(ready7_), std::move(ready8_), std::move(ready9_), std::move(ready10_), std::move(ready11_), std::move(ready12_), std::move(ready13_), std::move(ready14_), std::move(ready15_), std::move(ready16_), std::move(ready17_)));
  }
};
template <typename F0, typename F1, typename F2, typename F3, typename F4, typename F5, typename F6, typename F7, typename F8, typename F9, typename F10, typename F11, typename F12, typename F13, typename F14, typename F15, typename F16, typename F17, typename F18> class Join<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15, F16, F17, F18> {
 private:
  [[no_unique_address]] uint32_t state_ = 0;
  using R0 = typename decltype(std::declval<F0>()())::Type;
  union { [[no_unique_address]] F0 pending0_; [[no_unique_address]] R0 ready0_; };
  using R1 = typename decltype(std::declval<F1>()())::Type;
  union { [[no_unique_address]] F1 pending1_; [[no_unique_address]] R1 ready1_; };
  using R2 = typename decltype(std::declval<F2>()())::Type;
  union { [[no_unique_address]] F2 pending2_; [[no_unique_address]] R2 ready2_; };
  using R3 = typename decltype(std::declval<F3>()())::Type;
  union { [[no_unique_address]] F3 pending3_; [[no_unique_address]] R3 ready3_; };
  using R4 = typename decltype(std::declval<F4>()())::Type;
  union { [[no_unique_address]] F4 pending4_; [[no_unique_address]] R4 ready4_; };
  using R5 = typename decltype(std::declval<F5>()())::Type;
  union { [[no_unique_address]] F5 pending5_; [[no_unique_address]] R5 ready5_; };
  using R6 = typename decltype(std::declval<F6>()())::Type;
  union { [[no_unique_address]] F6 pending6_; [[no_unique_address]] R6 ready6_; };
  using R7 = typename decltype(std::declval<F7>()())::Type;
  union { [[no_unique_address]] F7 pending7_; [[no_unique_address]] R7 ready7_; };
  using R8 = typename decltype(std::declval<F8>()())::Type;
  union { [[no_unique_address]] F8 pending8_; [[no_unique_address]] R8 ready8_; };
  using R9 = typename decltype(std::declval<F9>()())::Type;
  union { [[no_unique_address]] F9 pending9_; [[no_unique_address]] R9 ready9_; };
  using R10 = typename decltype(std::declval<F10>()())::Type;
  union { [[no_unique_address]] F10 pending10_; [[no_unique_address]] R10 ready10_; };
  using R11 = typename decltype(std::declval<F11>()())::Type;
  union { [[no_unique_address]] F11 pending11_; [[no_unique_address]] R11 ready11_; };
  using R12 = typename decltype(std::declval<F12>()())::Type;
  union { [[no_unique_address]] F12 pending12_; [[no_unique_address]] R12 ready12_; };
  using R13 = typename decltype(std::declval<F13>()())::Type;
  union { [[no_unique_address]] F13 pending13_; [[no_unique_address]] R13 ready13_; };
  using R14 = typename decltype(std::declval<F14>()())::Type;
  union { [[no_unique_address]] F14 pending14_; [[no_unique_address]] R14 ready14_; };
  using R15 = typename decltype(std::declval<F15>()())::Type;
  union { [[no_unique_address]] F15 pending15_; [[no_unique_address]] R15 ready15_; };
  using R16 = typename decltype(std::declval<F16>()())::Type;
  union { [[no_unique_address]] F16 pending16_; [[no_unique_address]] R16 ready16_; };
  using R17 = typename decltype(std::declval<F17>()())::Type;
  union { [[no_unique_address]] F17 pending17_; [[no_unique_address]] R17 ready17_; };
  using R18 = typename decltype(std::declval<F18>()())::Type;
  union { [[no_unique_address]] F18 pending18_; [[no_unique_address]] R18 ready18_; };
 public:
  Join(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7, F8 f8, F9 f9, F10 f10, F11 f11, F12 f12, F13 f13, F14 f14, F15 f15, F16 f16, F17 f17, F18 f18) : pending0_(std::move(f0)), pending1_(std::move(f1)), pending2_(std::move(f2)), pending3_(std::move(f3)), pending4_(std::move(f4)), pending5_(std::move(f5)), pending6_(std::move(f6)), pending7_(std::move(f7)), pending8_(std::move(f8)), pending9_(std::move(f9)), pending10_(std::move(f10)), pending11_(std::move(f11)), pending12_(std::move(f12)), pending13_(std::move(f13)), pending14_(std::move(f14)), pending15_(std::move(f15)), pending16_(std::move(f16)), pending17_(std::move(f17)), pending18_(std::move(f18)) {}
  Join& operator=(const Join&) = delete;
  Join(const Join& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
    Construct(&pending15_, std::move(other.pending15_));
    Construct(&pending16_, std::move(other.pending16_));
    Construct(&pending17_, std::move(other.pending17_));
    Construct(&pending18_, std::move(other.pending18_));
  }
  Join(Join&& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
    Construct(&pending15_, std::move(other.pending15_));
    Construct(&pending16_, std::move(other.pending16_));
    Construct(&pending17_, std::move(other.pending17_));
    Construct(&pending18_, std::move(other.pending18_));
  }
  ~Join() {
    if (state_ & 1) Destruct(&ready0_); else Destruct(&pending0_);
    if (state_ & 2) Destruct(&ready1_); else Destruct(&pending1_);
    if (state_ & 4) Destruct(&ready2_); else Destruct(&pending2_);
    if (state_ & 8) Destruct(&ready3_); else Destruct(&pending3_);
    if (state_ & 16) Destruct(&ready4_); else Destruct(&pending4_);
    if (state_ & 32) Destruct(&ready5_); else Destruct(&pending5_);
    if (state_ & 64) Destruct(&ready6_); else Destruct(&pending6_);
    if (state_ & 128) Destruct(&ready7_); else Destruct(&pending7_);
    if (state_ & 256) Destruct(&ready8_); else Destruct(&pending8_);
    if (state_ & 512) Destruct(&ready9_); else Destruct(&pending9_);
    if (state_ & 1024) Destruct(&ready10_); else Destruct(&pending10_);
    if (state_ & 2048) Destruct(&ready11_); else Destruct(&pending11_);
    if (state_ & 4096) Destruct(&ready12_); else Destruct(&pending12_);
    if (state_ & 8192) Destruct(&ready13_); else Destruct(&pending13_);
    if (state_ & 16384) Destruct(&ready14_); else Destruct(&pending14_);
    if (state_ & 32768) Destruct(&ready15_); else Destruct(&pending15_);
    if (state_ & 65536) Destruct(&ready16_); else Destruct(&pending16_);
    if (state_ & 131072) Destruct(&ready17_); else Destruct(&pending17_);
    if (state_ & 262144) Destruct(&ready18_); else Destruct(&pending18_);
  }
  Poll<std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16, R17, R18>> operator()() {
    if ((state_ & 1) == 0) {
      auto r = pending0_();
      if (auto* p = r.get_ready()) { state_ |= 1; Destruct(&pending0_); Construct(&ready0_, std::move(*p)); }
    }
    if ((state_ & 2) == 0) {
      auto r = pending1_();
      if (auto* p = r.get_ready()) { state_ |= 2; Destruct(&pending1_); Construct(&ready1_, std::move(*p)); }
    }
    if ((state_ & 4) == 0) {
      auto r = pending2_();
      if (auto* p = r.get_ready()) { state_ |= 4; Destruct(&pending2_); Construct(&ready2_, std::move(*p)); }
    }
    if ((state_ & 8) == 0) {
      auto r = pending3_();
      if (auto* p = r.get_ready()) { state_ |= 8; Destruct(&pending3_); Construct(&ready3_, std::move(*p)); }
    }
    if ((state_ & 16) == 0) {
      auto r = pending4_();
      if (auto* p = r.get_ready()) { state_ |= 16; Destruct(&pending4_); Construct(&ready4_, std::move(*p)); }
    }
    if ((state_ & 32) == 0) {
      auto r = pending5_();
      if (auto* p = r.get_ready()) { state_ |= 32; Destruct(&pending5_); Construct(&ready5_, std::move(*p)); }
    }
    if ((state_ & 64) == 0) {
      auto r = pending6_();
      if (auto* p = r.get_ready()) { state_ |= 64; Destruct(&pending6_); Construct(&ready6_, std::move(*p)); }
    }
    if ((state_ & 128) == 0) {
      auto r = pending7_();
      if (auto* p = r.get_ready()) { state_ |= 128; Destruct(&pending7_); Construct(&ready7_, std::move(*p)); }
    }
    if ((state_ & 256) == 0) {
      auto r = pending8_();
      if (auto* p = r.get_ready()) { state_ |= 256; Destruct(&pending8_); Construct(&ready8_, std::move(*p)); }
    }
    if ((state_ & 512) == 0) {
      auto r = pending9_();
      if (auto* p = r.get_ready()) { state_ |= 512; Destruct(&pending9_); Construct(&ready9_, std::move(*p)); }
    }
    if ((state_ & 1024) == 0) {
      auto r = pending10_();
      if (auto* p = r.get_ready()) { state_ |= 1024; Destruct(&pending10_); Construct(&ready10_, std::move(*p)); }
    }
    if ((state_ & 2048) == 0) {
      auto r = pending11_();
      if (auto* p = r.get_ready()) { state_ |= 2048; Destruct(&pending11_); Construct(&ready11_, std::move(*p)); }
    }
    if ((state_ & 4096) == 0) {
      auto r = pending12_();
      if (auto* p = r.get_ready()) { state_ |= 4096; Destruct(&pending12_); Construct(&ready12_, std::move(*p)); }
    }
    if ((state_ & 8192) == 0) {
      auto r = pending13_();
      if (auto* p = r.get_ready()) { state_ |= 8192; Destruct(&pending13_); Construct(&ready13_, std::move(*p)); }
    }
    if ((state_ & 16384) == 0) {
      auto r = pending14_();
      if (auto* p = r.get_ready()) { state_ |= 16384; Destruct(&pending14_); Construct(&ready14_, std::move(*p)); }
    }
    if ((state_ & 32768) == 0) {
      auto r = pending15_();
      if (auto* p = r.get_ready()) { state_ |= 32768; Destruct(&pending15_); Construct(&ready15_, std::move(*p)); }
    }
    if ((state_ & 65536) == 0) {
      auto r = pending16_();
      if (auto* p = r.get_ready()) { state_ |= 65536; Destruct(&pending16_); Construct(&ready16_, std::move(*p)); }
    }
    if ((state_ & 131072) == 0) {
      auto r = pending17_();
      if (auto* p = r.get_ready()) { state_ |= 131072; Destruct(&pending17_); Construct(&ready17_, std::move(*p)); }
    }
    if ((state_ & 262144) == 0) {
      auto r = pending18_();
      if (auto* p = r.get_ready()) { state_ |= 262144; Destruct(&pending18_); Construct(&ready18_, std::move(*p)); }
    }
    if (state_ != 524287) return kPending;
    return ready(std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16, R17, R18>(std::move(ready0_), std::move(ready1_), std::move(ready2_), std::move(ready3_), std::move(ready4_), std::move(ready5_), std::move(ready6_), std::move(ready7_), std::move(ready8_), std::move(ready9_), std::move(ready10_), std::move(ready11_), std::move(ready12_), std::move(ready13_), std::move(ready14_), std::move(ready15_), std::move(ready16_), std::move(ready17_), std::move(ready18_)));
  }
};
template <typename F0, typename F1, typename F2, typename F3, typename F4, typename F5, typename F6, typename F7, typename F8, typename F9, typename F10, typename F11, typename F12, typename F13, typename F14, typename F15, typename F16, typename F17, typename F18, typename F19> class Join<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15, F16, F17, F18, F19> {
 private:
  [[no_unique_address]] uint32_t state_ = 0;
  using R0 = typename decltype(std::declval<F0>()())::Type;
  union { [[no_unique_address]] F0 pending0_; [[no_unique_address]] R0 ready0_; };
  using R1 = typename decltype(std::declval<F1>()())::Type;
  union { [[no_unique_address]] F1 pending1_; [[no_unique_address]] R1 ready1_; };
  using R2 = typename decltype(std::declval<F2>()())::Type;
  union { [[no_unique_address]] F2 pending2_; [[no_unique_address]] R2 ready2_; };
  using R3 = typename decltype(std::declval<F3>()())::Type;
  union { [[no_unique_address]] F3 pending3_; [[no_unique_address]] R3 ready3_; };
  using R4 = typename decltype(std::declval<F4>()())::Type;
  union { [[no_unique_address]] F4 pending4_; [[no_unique_address]] R4 ready4_; };
  using R5 = typename decltype(std::declval<F5>()())::Type;
  union { [[no_unique_address]] F5 pending5_; [[no_unique_address]] R5 ready5_; };
  using R6 = typename decltype(std::declval<F6>()())::Type;
  union { [[no_unique_address]] F6 pending6_; [[no_unique_address]] R6 ready6_; };
  using R7 = typename decltype(std::declval<F7>()())::Type;
  union { [[no_unique_address]] F7 pending7_; [[no_unique_address]] R7 ready7_; };
  using R8 = typename decltype(std::declval<F8>()())::Type;
  union { [[no_unique_address]] F8 pending8_; [[no_unique_address]] R8 ready8_; };
  using R9 = typename decltype(std::declval<F9>()())::Type;
  union { [[no_unique_address]] F9 pending9_; [[no_unique_address]] R9 ready9_; };
  using R10 = typename decltype(std::declval<F10>()())::Type;
  union { [[no_unique_address]] F10 pending10_; [[no_unique_address]] R10 ready10_; };
  using R11 = typename decltype(std::declval<F11>()())::Type;
  union { [[no_unique_address]] F11 pending11_; [[no_unique_address]] R11 ready11_; };
  using R12 = typename decltype(std::declval<F12>()())::Type;
  union { [[no_unique_address]] F12 pending12_; [[no_unique_address]] R12 ready12_; };
  using R13 = typename decltype(std::declval<F13>()())::Type;
  union { [[no_unique_address]] F13 pending13_; [[no_unique_address]] R13 ready13_; };
  using R14 = typename decltype(std::declval<F14>()())::Type;
  union { [[no_unique_address]] F14 pending14_; [[no_unique_address]] R14 ready14_; };
  using R15 = typename decltype(std::declval<F15>()())::Type;
  union { [[no_unique_address]] F15 pending15_; [[no_unique_address]] R15 ready15_; };
  using R16 = typename decltype(std::declval<F16>()())::Type;
  union { [[no_unique_address]] F16 pending16_; [[no_unique_address]] R16 ready16_; };
  using R17 = typename decltype(std::declval<F17>()())::Type;
  union { [[no_unique_address]] F17 pending17_; [[no_unique_address]] R17 ready17_; };
  using R18 = typename decltype(std::declval<F18>()())::Type;
  union { [[no_unique_address]] F18 pending18_; [[no_unique_address]] R18 ready18_; };
  using R19 = typename decltype(std::declval<F19>()())::Type;
  union { [[no_unique_address]] F19 pending19_; [[no_unique_address]] R19 ready19_; };
 public:
  Join(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7, F8 f8, F9 f9, F10 f10, F11 f11, F12 f12, F13 f13, F14 f14, F15 f15, F16 f16, F17 f17, F18 f18, F19 f19) : pending0_(std::move(f0)), pending1_(std::move(f1)), pending2_(std::move(f2)), pending3_(std::move(f3)), pending4_(std::move(f4)), pending5_(std::move(f5)), pending6_(std::move(f6)), pending7_(std::move(f7)), pending8_(std::move(f8)), pending9_(std::move(f9)), pending10_(std::move(f10)), pending11_(std::move(f11)), pending12_(std::move(f12)), pending13_(std::move(f13)), pending14_(std::move(f14)), pending15_(std::move(f15)), pending16_(std::move(f16)), pending17_(std::move(f17)), pending18_(std::move(f18)), pending19_(std::move(f19)) {}
  Join& operator=(const Join&) = delete;
  Join(const Join& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
    Construct(&pending15_, std::move(other.pending15_));
    Construct(&pending16_, std::move(other.pending16_));
    Construct(&pending17_, std::move(other.pending17_));
    Construct(&pending18_, std::move(other.pending18_));
    Construct(&pending19_, std::move(other.pending19_));
  }
  Join(Join&& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
    Construct(&pending15_, std::move(other.pending15_));
    Construct(&pending16_, std::move(other.pending16_));
    Construct(&pending17_, std::move(other.pending17_));
    Construct(&pending18_, std::move(other.pending18_));
    Construct(&pending19_, std::move(other.pending19_));
  }
  ~Join() {
    if (state_ & 1) Destruct(&ready0_); else Destruct(&pending0_);
    if (state_ & 2) Destruct(&ready1_); else Destruct(&pending1_);
    if (state_ & 4) Destruct(&ready2_); else Destruct(&pending2_);
    if (state_ & 8) Destruct(&ready3_); else Destruct(&pending3_);
    if (state_ & 16) Destruct(&ready4_); else Destruct(&pending4_);
    if (state_ & 32) Destruct(&ready5_); else Destruct(&pending5_);
    if (state_ & 64) Destruct(&ready6_); else Destruct(&pending6_);
    if (state_ & 128) Destruct(&ready7_); else Destruct(&pending7_);
    if (state_ & 256) Destruct(&ready8_); else Destruct(&pending8_);
    if (state_ & 512) Destruct(&ready9_); else Destruct(&pending9_);
    if (state_ & 1024) Destruct(&ready10_); else Destruct(&pending10_);
    if (state_ & 2048) Destruct(&ready11_); else Destruct(&pending11_);
    if (state_ & 4096) Destruct(&ready12_); else Destruct(&pending12_);
    if (state_ & 8192) Destruct(&ready13_); else Destruct(&pending13_);
    if (state_ & 16384) Destruct(&ready14_); else Destruct(&pending14_);
    if (state_ & 32768) Destruct(&ready15_); else Destruct(&pending15_);
    if (state_ & 65536) Destruct(&ready16_); else Destruct(&pending16_);
    if (state_ & 131072) Destruct(&ready17_); else Destruct(&pending17_);
    if (state_ & 262144) Destruct(&ready18_); else Destruct(&pending18_);
    if (state_ & 524288) Destruct(&ready19_); else Destruct(&pending19_);
  }
  Poll<std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16, R17, R18, R19>> operator()() {
    if ((state_ & 1) == 0) {
      auto r = pending0_();
      if (auto* p = r.get_ready()) { state_ |= 1; Destruct(&pending0_); Construct(&ready0_, std::move(*p)); }
    }
    if ((state_ & 2) == 0) {
      auto r = pending1_();
      if (auto* p = r.get_ready()) { state_ |= 2; Destruct(&pending1_); Construct(&ready1_, std::move(*p)); }
    }
    if ((state_ & 4) == 0) {
      auto r = pending2_();
      if (auto* p = r.get_ready()) { state_ |= 4; Destruct(&pending2_); Construct(&ready2_, std::move(*p)); }
    }
    if ((state_ & 8) == 0) {
      auto r = pending3_();
      if (auto* p = r.get_ready()) { state_ |= 8; Destruct(&pending3_); Construct(&ready3_, std::move(*p)); }
    }
    if ((state_ & 16) == 0) {
      auto r = pending4_();
      if (auto* p = r.get_ready()) { state_ |= 16; Destruct(&pending4_); Construct(&ready4_, std::move(*p)); }
    }
    if ((state_ & 32) == 0) {
      auto r = pending5_();
      if (auto* p = r.get_ready()) { state_ |= 32; Destruct(&pending5_); Construct(&ready5_, std::move(*p)); }
    }
    if ((state_ & 64) == 0) {
      auto r = pending6_();
      if (auto* p = r.get_ready()) { state_ |= 64; Destruct(&pending6_); Construct(&ready6_, std::move(*p)); }
    }
    if ((state_ & 128) == 0) {
      auto r = pending7_();
      if (auto* p = r.get_ready()) { state_ |= 128; Destruct(&pending7_); Construct(&ready7_, std::move(*p)); }
    }
    if ((state_ & 256) == 0) {
      auto r = pending8_();
      if (auto* p = r.get_ready()) { state_ |= 256; Destruct(&pending8_); Construct(&ready8_, std::move(*p)); }
    }
    if ((state_ & 512) == 0) {
      auto r = pending9_();
      if (auto* p = r.get_ready()) { state_ |= 512; Destruct(&pending9_); Construct(&ready9_, std::move(*p)); }
    }
    if ((state_ & 1024) == 0) {
      auto r = pending10_();
      if (auto* p = r.get_ready()) { state_ |= 1024; Destruct(&pending10_); Construct(&ready10_, std::move(*p)); }
    }
    if ((state_ & 2048) == 0) {
      auto r = pending11_();
      if (auto* p = r.get_ready()) { state_ |= 2048; Destruct(&pending11_); Construct(&ready11_, std::move(*p)); }
    }
    if ((state_ & 4096) == 0) {
      auto r = pending12_();
      if (auto* p = r.get_ready()) { state_ |= 4096; Destruct(&pending12_); Construct(&ready12_, std::move(*p)); }
    }
    if ((state_ & 8192) == 0) {
      auto r = pending13_();
      if (auto* p = r.get_ready()) { state_ |= 8192; Destruct(&pending13_); Construct(&ready13_, std::move(*p)); }
    }
    if ((state_ & 16384) == 0) {
      auto r = pending14_();
      if (auto* p = r.get_ready()) { state_ |= 16384; Destruct(&pending14_); Construct(&ready14_, std::move(*p)); }
    }
    if ((state_ & 32768) == 0) {
      auto r = pending15_();
      if (auto* p = r.get_ready()) { state_ |= 32768; Destruct(&pending15_); Construct(&ready15_, std::move(*p)); }
    }
    if ((state_ & 65536) == 0) {
      auto r = pending16_();
      if (auto* p = r.get_ready()) { state_ |= 65536; Destruct(&pending16_); Construct(&ready16_, std::move(*p)); }
    }
    if ((state_ & 131072) == 0) {
      auto r = pending17_();
      if (auto* p = r.get_ready()) { state_ |= 131072; Destruct(&pending17_); Construct(&ready17_, std::move(*p)); }
    }
    if ((state_ & 262144) == 0) {
      auto r = pending18_();
      if (auto* p = r.get_ready()) { state_ |= 262144; Destruct(&pending18_); Construct(&ready18_, std::move(*p)); }
    }
    if ((state_ & 524288) == 0) {
      auto r = pending19_();
      if (auto* p = r.get_ready()) { state_ |= 524288; Destruct(&pending19_); Construct(&ready19_, std::move(*p)); }
    }
    if (state_ != 1048575) return kPending;
    return ready(std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16, R17, R18, R19>(std::move(ready0_), std::move(ready1_), std::move(ready2_), std::move(ready3_), std::move(ready4_), std::move(ready5_), std::move(ready6_), std::move(ready7_), std::move(ready8_), std::move(ready9_), std::move(ready10_), std::move(ready11_), std::move(ready12_), std::move(ready13_), std::move(ready14_), std::move(ready15_), std::move(ready16_), std::move(ready17_), std::move(ready18_), std::move(ready19_)));
  }
};
template <typename F0, typename F1, typename F2, typename F3, typename F4, typename F5, typename F6, typename F7, typename F8, typename F9, typename F10, typename F11, typename F12, typename F13, typename F14, typename F15, typename F16, typename F17, typename F18, typename F19, typename F20> class Join<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15, F16, F17, F18, F19, F20> {
 private:
  [[no_unique_address]] uint32_t state_ = 0;
  using R0 = typename decltype(std::declval<F0>()())::Type;
  union { [[no_unique_address]] F0 pending0_; [[no_unique_address]] R0 ready0_; };
  using R1 = typename decltype(std::declval<F1>()())::Type;
  union { [[no_unique_address]] F1 pending1_; [[no_unique_address]] R1 ready1_; };
  using R2 = typename decltype(std::declval<F2>()())::Type;
  union { [[no_unique_address]] F2 pending2_; [[no_unique_address]] R2 ready2_; };
  using R3 = typename decltype(std::declval<F3>()())::Type;
  union { [[no_unique_address]] F3 pending3_; [[no_unique_address]] R3 ready3_; };
  using R4 = typename decltype(std::declval<F4>()())::Type;
  union { [[no_unique_address]] F4 pending4_; [[no_unique_address]] R4 ready4_; };
  using R5 = typename decltype(std::declval<F5>()())::Type;
  union { [[no_unique_address]] F5 pending5_; [[no_unique_address]] R5 ready5_; };
  using R6 = typename decltype(std::declval<F6>()())::Type;
  union { [[no_unique_address]] F6 pending6_; [[no_unique_address]] R6 ready6_; };
  using R7 = typename decltype(std::declval<F7>()())::Type;
  union { [[no_unique_address]] F7 pending7_; [[no_unique_address]] R7 ready7_; };
  using R8 = typename decltype(std::declval<F8>()())::Type;
  union { [[no_unique_address]] F8 pending8_; [[no_unique_address]] R8 ready8_; };
  using R9 = typename decltype(std::declval<F9>()())::Type;
  union { [[no_unique_address]] F9 pending9_; [[no_unique_address]] R9 ready9_; };
  using R10 = typename decltype(std::declval<F10>()())::Type;
  union { [[no_unique_address]] F10 pending10_; [[no_unique_address]] R10 ready10_; };
  using R11 = typename decltype(std::declval<F11>()())::Type;
  union { [[no_unique_address]] F11 pending11_; [[no_unique_address]] R11 ready11_; };
  using R12 = typename decltype(std::declval<F12>()())::Type;
  union { [[no_unique_address]] F12 pending12_; [[no_unique_address]] R12 ready12_; };
  using R13 = typename decltype(std::declval<F13>()())::Type;
  union { [[no_unique_address]] F13 pending13_; [[no_unique_address]] R13 ready13_; };
  using R14 = typename decltype(std::declval<F14>()())::Type;
  union { [[no_unique_address]] F14 pending14_; [[no_unique_address]] R14 ready14_; };
  using R15 = typename decltype(std::declval<F15>()())::Type;
  union { [[no_unique_address]] F15 pending15_; [[no_unique_address]] R15 ready15_; };
  using R16 = typename decltype(std::declval<F16>()())::Type;
  union { [[no_unique_address]] F16 pending16_; [[no_unique_address]] R16 ready16_; };
  using R17 = typename decltype(std::declval<F17>()())::Type;
  union { [[no_unique_address]] F17 pending17_; [[no_unique_address]] R17 ready17_; };
  using R18 = typename decltype(std::declval<F18>()())::Type;
  union { [[no_unique_address]] F18 pending18_; [[no_unique_address]] R18 ready18_; };
  using R19 = typename decltype(std::declval<F19>()())::Type;
  union { [[no_unique_address]] F19 pending19_; [[no_unique_address]] R19 ready19_; };
  using R20 = typename decltype(std::declval<F20>()())::Type;
  union { [[no_unique_address]] F20 pending20_; [[no_unique_address]] R20 ready20_; };
 public:
  Join(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7, F8 f8, F9 f9, F10 f10, F11 f11, F12 f12, F13 f13, F14 f14, F15 f15, F16 f16, F17 f17, F18 f18, F19 f19, F20 f20) : pending0_(std::move(f0)), pending1_(std::move(f1)), pending2_(std::move(f2)), pending3_(std::move(f3)), pending4_(std::move(f4)), pending5_(std::move(f5)), pending6_(std::move(f6)), pending7_(std::move(f7)), pending8_(std::move(f8)), pending9_(std::move(f9)), pending10_(std::move(f10)), pending11_(std::move(f11)), pending12_(std::move(f12)), pending13_(std::move(f13)), pending14_(std::move(f14)), pending15_(std::move(f15)), pending16_(std::move(f16)), pending17_(std::move(f17)), pending18_(std::move(f18)), pending19_(std::move(f19)), pending20_(std::move(f20)) {}
  Join& operator=(const Join&) = delete;
  Join(const Join& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
    Construct(&pending15_, std::move(other.pending15_));
    Construct(&pending16_, std::move(other.pending16_));
    Construct(&pending17_, std::move(other.pending17_));
    Construct(&pending18_, std::move(other.pending18_));
    Construct(&pending19_, std::move(other.pending19_));
    Construct(&pending20_, std::move(other.pending20_));
  }
  Join(Join&& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
    Construct(&pending15_, std::move(other.pending15_));
    Construct(&pending16_, std::move(other.pending16_));
    Construct(&pending17_, std::move(other.pending17_));
    Construct(&pending18_, std::move(other.pending18_));
    Construct(&pending19_, std::move(other.pending19_));
    Construct(&pending20_, std::move(other.pending20_));
  }
  ~Join() {
    if (state_ & 1) Destruct(&ready0_); else Destruct(&pending0_);
    if (state_ & 2) Destruct(&ready1_); else Destruct(&pending1_);
    if (state_ & 4) Destruct(&ready2_); else Destruct(&pending2_);
    if (state_ & 8) Destruct(&ready3_); else Destruct(&pending3_);
    if (state_ & 16) Destruct(&ready4_); else Destruct(&pending4_);
    if (state_ & 32) Destruct(&ready5_); else Destruct(&pending5_);
    if (state_ & 64) Destruct(&ready6_); else Destruct(&pending6_);
    if (state_ & 128) Destruct(&ready7_); else Destruct(&pending7_);
    if (state_ & 256) Destruct(&ready8_); else Destruct(&pending8_);
    if (state_ & 512) Destruct(&ready9_); else Destruct(&pending9_);
    if (state_ & 1024) Destruct(&ready10_); else Destruct(&pending10_);
    if (state_ & 2048) Destruct(&ready11_); else Destruct(&pending11_);
    if (state_ & 4096) Destruct(&ready12_); else Destruct(&pending12_);
    if (state_ & 8192) Destruct(&ready13_); else Destruct(&pending13_);
    if (state_ & 16384) Destruct(&ready14_); else Destruct(&pending14_);
    if (state_ & 32768) Destruct(&ready15_); else Destruct(&pending15_);
    if (state_ & 65536) Destruct(&ready16_); else Destruct(&pending16_);
    if (state_ & 131072) Destruct(&ready17_); else Destruct(&pending17_);
    if (state_ & 262144) Destruct(&ready18_); else Destruct(&pending18_);
    if (state_ & 524288) Destruct(&ready19_); else Destruct(&pending19_);
    if (state_ & 1048576) Destruct(&ready20_); else Destruct(&pending20_);
  }
  Poll<std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16, R17, R18, R19, R20>> operator()() {
    if ((state_ & 1) == 0) {
      auto r = pending0_();
      if (auto* p = r.get_ready()) { state_ |= 1; Destruct(&pending0_); Construct(&ready0_, std::move(*p)); }
    }
    if ((state_ & 2) == 0) {
      auto r = pending1_();
      if (auto* p = r.get_ready()) { state_ |= 2; Destruct(&pending1_); Construct(&ready1_, std::move(*p)); }
    }
    if ((state_ & 4) == 0) {
      auto r = pending2_();
      if (auto* p = r.get_ready()) { state_ |= 4; Destruct(&pending2_); Construct(&ready2_, std::move(*p)); }
    }
    if ((state_ & 8) == 0) {
      auto r = pending3_();
      if (auto* p = r.get_ready()) { state_ |= 8; Destruct(&pending3_); Construct(&ready3_, std::move(*p)); }
    }
    if ((state_ & 16) == 0) {
      auto r = pending4_();
      if (auto* p = r.get_ready()) { state_ |= 16; Destruct(&pending4_); Construct(&ready4_, std::move(*p)); }
    }
    if ((state_ & 32) == 0) {
      auto r = pending5_();
      if (auto* p = r.get_ready()) { state_ |= 32; Destruct(&pending5_); Construct(&ready5_, std::move(*p)); }
    }
    if ((state_ & 64) == 0) {
      auto r = pending6_();
      if (auto* p = r.get_ready()) { state_ |= 64; Destruct(&pending6_); Construct(&ready6_, std::move(*p)); }
    }
    if ((state_ & 128) == 0) {
      auto r = pending7_();
      if (auto* p = r.get_ready()) { state_ |= 128; Destruct(&pending7_); Construct(&ready7_, std::move(*p)); }
    }
    if ((state_ & 256) == 0) {
      auto r = pending8_();
      if (auto* p = r.get_ready()) { state_ |= 256; Destruct(&pending8_); Construct(&ready8_, std::move(*p)); }
    }
    if ((state_ & 512) == 0) {
      auto r = pending9_();
      if (auto* p = r.get_ready()) { state_ |= 512; Destruct(&pending9_); Construct(&ready9_, std::move(*p)); }
    }
    if ((state_ & 1024) == 0) {
      auto r = pending10_();
      if (auto* p = r.get_ready()) { state_ |= 1024; Destruct(&pending10_); Construct(&ready10_, std::move(*p)); }
    }
    if ((state_ & 2048) == 0) {
      auto r = pending11_();
      if (auto* p = r.get_ready()) { state_ |= 2048; Destruct(&pending11_); Construct(&ready11_, std::move(*p)); }
    }
    if ((state_ & 4096) == 0) {
      auto r = pending12_();
      if (auto* p = r.get_ready()) { state_ |= 4096; Destruct(&pending12_); Construct(&ready12_, std::move(*p)); }
    }
    if ((state_ & 8192) == 0) {
      auto r = pending13_();
      if (auto* p = r.get_ready()) { state_ |= 8192; Destruct(&pending13_); Construct(&ready13_, std::move(*p)); }
    }
    if ((state_ & 16384) == 0) {
      auto r = pending14_();
      if (auto* p = r.get_ready()) { state_ |= 16384; Destruct(&pending14_); Construct(&ready14_, std::move(*p)); }
    }
    if ((state_ & 32768) == 0) {
      auto r = pending15_();
      if (auto* p = r.get_ready()) { state_ |= 32768; Destruct(&pending15_); Construct(&ready15_, std::move(*p)); }
    }
    if ((state_ & 65536) == 0) {
      auto r = pending16_();
      if (auto* p = r.get_ready()) { state_ |= 65536; Destruct(&pending16_); Construct(&ready16_, std::move(*p)); }
    }
    if ((state_ & 131072) == 0) {
      auto r = pending17_();
      if (auto* p = r.get_ready()) { state_ |= 131072; Destruct(&pending17_); Construct(&ready17_, std::move(*p)); }
    }
    if ((state_ & 262144) == 0) {
      auto r = pending18_();
      if (auto* p = r.get_ready()) { state_ |= 262144; Destruct(&pending18_); Construct(&ready18_, std::move(*p)); }
    }
    if ((state_ & 524288) == 0) {
      auto r = pending19_();
      if (auto* p = r.get_ready()) { state_ |= 524288; Destruct(&pending19_); Construct(&ready19_, std::move(*p)); }
    }
    if ((state_ & 1048576) == 0) {
      auto r = pending20_();
      if (auto* p = r.get_ready()) { state_ |= 1048576; Destruct(&pending20_); Construct(&ready20_, std::move(*p)); }
    }
    if (state_ != 2097151) return kPending;
    return ready(std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16, R17, R18, R19, R20>(std::move(ready0_), std::move(ready1_), std::move(ready2_), std::move(ready3_), std::move(ready4_), std::move(ready5_), std::move(ready6_), std::move(ready7_), std::move(ready8_), std::move(ready9_), std::move(ready10_), std::move(ready11_), std::move(ready12_), std::move(ready13_), std::move(ready14_), std::move(ready15_), std::move(ready16_), std::move(ready17_), std::move(ready18_), std::move(ready19_), std::move(ready20_)));
  }
};
template <typename F0, typename F1, typename F2, typename F3, typename F4, typename F5, typename F6, typename F7, typename F8, typename F9, typename F10, typename F11, typename F12, typename F13, typename F14, typename F15, typename F16, typename F17, typename F18, typename F19, typename F20, typename F21> class Join<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15, F16, F17, F18, F19, F20, F21> {
 private:
  [[no_unique_address]] uint32_t state_ = 0;
  using R0 = typename decltype(std::declval<F0>()())::Type;
  union { [[no_unique_address]] F0 pending0_; [[no_unique_address]] R0 ready0_; };
  using R1 = typename decltype(std::declval<F1>()())::Type;
  union { [[no_unique_address]] F1 pending1_; [[no_unique_address]] R1 ready1_; };
  using R2 = typename decltype(std::declval<F2>()())::Type;
  union { [[no_unique_address]] F2 pending2_; [[no_unique_address]] R2 ready2_; };
  using R3 = typename decltype(std::declval<F3>()())::Type;
  union { [[no_unique_address]] F3 pending3_; [[no_unique_address]] R3 ready3_; };
  using R4 = typename decltype(std::declval<F4>()())::Type;
  union { [[no_unique_address]] F4 pending4_; [[no_unique_address]] R4 ready4_; };
  using R5 = typename decltype(std::declval<F5>()())::Type;
  union { [[no_unique_address]] F5 pending5_; [[no_unique_address]] R5 ready5_; };
  using R6 = typename decltype(std::declval<F6>()())::Type;
  union { [[no_unique_address]] F6 pending6_; [[no_unique_address]] R6 ready6_; };
  using R7 = typename decltype(std::declval<F7>()())::Type;
  union { [[no_unique_address]] F7 pending7_; [[no_unique_address]] R7 ready7_; };
  using R8 = typename decltype(std::declval<F8>()())::Type;
  union { [[no_unique_address]] F8 pending8_; [[no_unique_address]] R8 ready8_; };
  using R9 = typename decltype(std::declval<F9>()())::Type;
  union { [[no_unique_address]] F9 pending9_; [[no_unique_address]] R9 ready9_; };
  using R10 = typename decltype(std::declval<F10>()())::Type;
  union { [[no_unique_address]] F10 pending10_; [[no_unique_address]] R10 ready10_; };
  using R11 = typename decltype(std::declval<F11>()())::Type;
  union { [[no_unique_address]] F11 pending11_; [[no_unique_address]] R11 ready11_; };
  using R12 = typename decltype(std::declval<F12>()())::Type;
  union { [[no_unique_address]] F12 pending12_; [[no_unique_address]] R12 ready12_; };
  using R13 = typename decltype(std::declval<F13>()())::Type;
  union { [[no_unique_address]] F13 pending13_; [[no_unique_address]] R13 ready13_; };
  using R14 = typename decltype(std::declval<F14>()())::Type;
  union { [[no_unique_address]] F14 pending14_; [[no_unique_address]] R14 ready14_; };
  using R15 = typename decltype(std::declval<F15>()())::Type;
  union { [[no_unique_address]] F15 pending15_; [[no_unique_address]] R15 ready15_; };
  using R16 = typename decltype(std::declval<F16>()())::Type;
  union { [[no_unique_address]] F16 pending16_; [[no_unique_address]] R16 ready16_; };
  using R17 = typename decltype(std::declval<F17>()())::Type;
  union { [[no_unique_address]] F17 pending17_; [[no_unique_address]] R17 ready17_; };
  using R18 = typename decltype(std::declval<F18>()())::Type;
  union { [[no_unique_address]] F18 pending18_; [[no_unique_address]] R18 ready18_; };
  using R19 = typename decltype(std::declval<F19>()())::Type;
  union { [[no_unique_address]] F19 pending19_; [[no_unique_address]] R19 ready19_; };
  using R20 = typename decltype(std::declval<F20>()())::Type;
  union { [[no_unique_address]] F20 pending20_; [[no_unique_address]] R20 ready20_; };
  using R21 = typename decltype(std::declval<F21>()())::Type;
  union { [[no_unique_address]] F21 pending21_; [[no_unique_address]] R21 ready21_; };
 public:
  Join(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7, F8 f8, F9 f9, F10 f10, F11 f11, F12 f12, F13 f13, F14 f14, F15 f15, F16 f16, F17 f17, F18 f18, F19 f19, F20 f20, F21 f21) : pending0_(std::move(f0)), pending1_(std::move(f1)), pending2_(std::move(f2)), pending3_(std::move(f3)), pending4_(std::move(f4)), pending5_(std::move(f5)), pending6_(std::move(f6)), pending7_(std::move(f7)), pending8_(std::move(f8)), pending9_(std::move(f9)), pending10_(std::move(f10)), pending11_(std::move(f11)), pending12_(std::move(f12)), pending13_(std::move(f13)), pending14_(std::move(f14)), pending15_(std::move(f15)), pending16_(std::move(f16)), pending17_(std::move(f17)), pending18_(std::move(f18)), pending19_(std::move(f19)), pending20_(std::move(f20)), pending21_(std::move(f21)) {}
  Join& operator=(const Join&) = delete;
  Join(const Join& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
    Construct(&pending15_, std::move(other.pending15_));
    Construct(&pending16_, std::move(other.pending16_));
    Construct(&pending17_, std::move(other.pending17_));
    Construct(&pending18_, std::move(other.pending18_));
    Construct(&pending19_, std::move(other.pending19_));
    Construct(&pending20_, std::move(other.pending20_));
    Construct(&pending21_, std::move(other.pending21_));
  }
  Join(Join&& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
    Construct(&pending15_, std::move(other.pending15_));
    Construct(&pending16_, std::move(other.pending16_));
    Construct(&pending17_, std::move(other.pending17_));
    Construct(&pending18_, std::move(other.pending18_));
    Construct(&pending19_, std::move(other.pending19_));
    Construct(&pending20_, std::move(other.pending20_));
    Construct(&pending21_, std::move(other.pending21_));
  }
  ~Join() {
    if (state_ & 1) Destruct(&ready0_); else Destruct(&pending0_);
    if (state_ & 2) Destruct(&ready1_); else Destruct(&pending1_);
    if (state_ & 4) Destruct(&ready2_); else Destruct(&pending2_);
    if (state_ & 8) Destruct(&ready3_); else Destruct(&pending3_);
    if (state_ & 16) Destruct(&ready4_); else Destruct(&pending4_);
    if (state_ & 32) Destruct(&ready5_); else Destruct(&pending5_);
    if (state_ & 64) Destruct(&ready6_); else Destruct(&pending6_);
    if (state_ & 128) Destruct(&ready7_); else Destruct(&pending7_);
    if (state_ & 256) Destruct(&ready8_); else Destruct(&pending8_);
    if (state_ & 512) Destruct(&ready9_); else Destruct(&pending9_);
    if (state_ & 1024) Destruct(&ready10_); else Destruct(&pending10_);
    if (state_ & 2048) Destruct(&ready11_); else Destruct(&pending11_);
    if (state_ & 4096) Destruct(&ready12_); else Destruct(&pending12_);
    if (state_ & 8192) Destruct(&ready13_); else Destruct(&pending13_);
    if (state_ & 16384) Destruct(&ready14_); else Destruct(&pending14_);
    if (state_ & 32768) Destruct(&ready15_); else Destruct(&pending15_);
    if (state_ & 65536) Destruct(&ready16_); else Destruct(&pending16_);
    if (state_ & 131072) Destruct(&ready17_); else Destruct(&pending17_);
    if (state_ & 262144) Destruct(&ready18_); else Destruct(&pending18_);
    if (state_ & 524288) Destruct(&ready19_); else Destruct(&pending19_);
    if (state_ & 1048576) Destruct(&ready20_); else Destruct(&pending20_);
    if (state_ & 2097152) Destruct(&ready21_); else Destruct(&pending21_);
  }
  Poll<std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16, R17, R18, R19, R20, R21>> operator()() {
    if ((state_ & 1) == 0) {
      auto r = pending0_();
      if (auto* p = r.get_ready()) { state_ |= 1; Destruct(&pending0_); Construct(&ready0_, std::move(*p)); }
    }
    if ((state_ & 2) == 0) {
      auto r = pending1_();
      if (auto* p = r.get_ready()) { state_ |= 2; Destruct(&pending1_); Construct(&ready1_, std::move(*p)); }
    }
    if ((state_ & 4) == 0) {
      auto r = pending2_();
      if (auto* p = r.get_ready()) { state_ |= 4; Destruct(&pending2_); Construct(&ready2_, std::move(*p)); }
    }
    if ((state_ & 8) == 0) {
      auto r = pending3_();
      if (auto* p = r.get_ready()) { state_ |= 8; Destruct(&pending3_); Construct(&ready3_, std::move(*p)); }
    }
    if ((state_ & 16) == 0) {
      auto r = pending4_();
      if (auto* p = r.get_ready()) { state_ |= 16; Destruct(&pending4_); Construct(&ready4_, std::move(*p)); }
    }
    if ((state_ & 32) == 0) {
      auto r = pending5_();
      if (auto* p = r.get_ready()) { state_ |= 32; Destruct(&pending5_); Construct(&ready5_, std::move(*p)); }
    }
    if ((state_ & 64) == 0) {
      auto r = pending6_();
      if (auto* p = r.get_ready()) { state_ |= 64; Destruct(&pending6_); Construct(&ready6_, std::move(*p)); }
    }
    if ((state_ & 128) == 0) {
      auto r = pending7_();
      if (auto* p = r.get_ready()) { state_ |= 128; Destruct(&pending7_); Construct(&ready7_, std::move(*p)); }
    }
    if ((state_ & 256) == 0) {
      auto r = pending8_();
      if (auto* p = r.get_ready()) { state_ |= 256; Destruct(&pending8_); Construct(&ready8_, std::move(*p)); }
    }
    if ((state_ & 512) == 0) {
      auto r = pending9_();
      if (auto* p = r.get_ready()) { state_ |= 512; Destruct(&pending9_); Construct(&ready9_, std::move(*p)); }
    }
    if ((state_ & 1024) == 0) {
      auto r = pending10_();
      if (auto* p = r.get_ready()) { state_ |= 1024; Destruct(&pending10_); Construct(&ready10_, std::move(*p)); }
    }
    if ((state_ & 2048) == 0) {
      auto r = pending11_();
      if (auto* p = r.get_ready()) { state_ |= 2048; Destruct(&pending11_); Construct(&ready11_, std::move(*p)); }
    }
    if ((state_ & 4096) == 0) {
      auto r = pending12_();
      if (auto* p = r.get_ready()) { state_ |= 4096; Destruct(&pending12_); Construct(&ready12_, std::move(*p)); }
    }
    if ((state_ & 8192) == 0) {
      auto r = pending13_();
      if (auto* p = r.get_ready()) { state_ |= 8192; Destruct(&pending13_); Construct(&ready13_, std::move(*p)); }
    }
    if ((state_ & 16384) == 0) {
      auto r = pending14_();
      if (auto* p = r.get_ready()) { state_ |= 16384; Destruct(&pending14_); Construct(&ready14_, std::move(*p)); }
    }
    if ((state_ & 32768) == 0) {
      auto r = pending15_();
      if (auto* p = r.get_ready()) { state_ |= 32768; Destruct(&pending15_); Construct(&ready15_, std::move(*p)); }
    }
    if ((state_ & 65536) == 0) {
      auto r = pending16_();
      if (auto* p = r.get_ready()) { state_ |= 65536; Destruct(&pending16_); Construct(&ready16_, std::move(*p)); }
    }
    if ((state_ & 131072) == 0) {
      auto r = pending17_();
      if (auto* p = r.get_ready()) { state_ |= 131072; Destruct(&pending17_); Construct(&ready17_, std::move(*p)); }
    }
    if ((state_ & 262144) == 0) {
      auto r = pending18_();
      if (auto* p = r.get_ready()) { state_ |= 262144; Destruct(&pending18_); Construct(&ready18_, std::move(*p)); }
    }
    if ((state_ & 524288) == 0) {
      auto r = pending19_();
      if (auto* p = r.get_ready()) { state_ |= 524288; Destruct(&pending19_); Construct(&ready19_, std::move(*p)); }
    }
    if ((state_ & 1048576) == 0) {
      auto r = pending20_();
      if (auto* p = r.get_ready()) { state_ |= 1048576; Destruct(&pending20_); Construct(&ready20_, std::move(*p)); }
    }
    if ((state_ & 2097152) == 0) {
      auto r = pending21_();
      if (auto* p = r.get_ready()) { state_ |= 2097152; Destruct(&pending21_); Construct(&ready21_, std::move(*p)); }
    }
    if (state_ != 4194303) return kPending;
    return ready(std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16, R17, R18, R19, R20, R21>(std::move(ready0_), std::move(ready1_), std::move(ready2_), std::move(ready3_), std::move(ready4_), std::move(ready5_), std::move(ready6_), std::move(ready7_), std::move(ready8_), std::move(ready9_), std::move(ready10_), std::move(ready11_), std::move(ready12_), std::move(ready13_), std::move(ready14_), std::move(ready15_), std::move(ready16_), std::move(ready17_), std::move(ready18_), std::move(ready19_), std::move(ready20_), std::move(ready21_)));
  }
};
template <typename F0, typename F1, typename F2, typename F3, typename F4, typename F5, typename F6, typename F7, typename F8, typename F9, typename F10, typename F11, typename F12, typename F13, typename F14, typename F15, typename F16, typename F17, typename F18, typename F19, typename F20, typename F21, typename F22> class Join<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15, F16, F17, F18, F19, F20, F21, F22> {
 private:
  [[no_unique_address]] uint32_t state_ = 0;
  using R0 = typename decltype(std::declval<F0>()())::Type;
  union { [[no_unique_address]] F0 pending0_; [[no_unique_address]] R0 ready0_; };
  using R1 = typename decltype(std::declval<F1>()())::Type;
  union { [[no_unique_address]] F1 pending1_; [[no_unique_address]] R1 ready1_; };
  using R2 = typename decltype(std::declval<F2>()())::Type;
  union { [[no_unique_address]] F2 pending2_; [[no_unique_address]] R2 ready2_; };
  using R3 = typename decltype(std::declval<F3>()())::Type;
  union { [[no_unique_address]] F3 pending3_; [[no_unique_address]] R3 ready3_; };
  using R4 = typename decltype(std::declval<F4>()())::Type;
  union { [[no_unique_address]] F4 pending4_; [[no_unique_address]] R4 ready4_; };
  using R5 = typename decltype(std::declval<F5>()())::Type;
  union { [[no_unique_address]] F5 pending5_; [[no_unique_address]] R5 ready5_; };
  using R6 = typename decltype(std::declval<F6>()())::Type;
  union { [[no_unique_address]] F6 pending6_; [[no_unique_address]] R6 ready6_; };
  using R7 = typename decltype(std::declval<F7>()())::Type;
  union { [[no_unique_address]] F7 pending7_; [[no_unique_address]] R7 ready7_; };
  using R8 = typename decltype(std::declval<F8>()())::Type;
  union { [[no_unique_address]] F8 pending8_; [[no_unique_address]] R8 ready8_; };
  using R9 = typename decltype(std::declval<F9>()())::Type;
  union { [[no_unique_address]] F9 pending9_; [[no_unique_address]] R9 ready9_; };
  using R10 = typename decltype(std::declval<F10>()())::Type;
  union { [[no_unique_address]] F10 pending10_; [[no_unique_address]] R10 ready10_; };
  using R11 = typename decltype(std::declval<F11>()())::Type;
  union { [[no_unique_address]] F11 pending11_; [[no_unique_address]] R11 ready11_; };
  using R12 = typename decltype(std::declval<F12>()())::Type;
  union { [[no_unique_address]] F12 pending12_; [[no_unique_address]] R12 ready12_; };
  using R13 = typename decltype(std::declval<F13>()())::Type;
  union { [[no_unique_address]] F13 pending13_; [[no_unique_address]] R13 ready13_; };
  using R14 = typename decltype(std::declval<F14>()())::Type;
  union { [[no_unique_address]] F14 pending14_; [[no_unique_address]] R14 ready14_; };
  using R15 = typename decltype(std::declval<F15>()())::Type;
  union { [[no_unique_address]] F15 pending15_; [[no_unique_address]] R15 ready15_; };
  using R16 = typename decltype(std::declval<F16>()())::Type;
  union { [[no_unique_address]] F16 pending16_; [[no_unique_address]] R16 ready16_; };
  using R17 = typename decltype(std::declval<F17>()())::Type;
  union { [[no_unique_address]] F17 pending17_; [[no_unique_address]] R17 ready17_; };
  using R18 = typename decltype(std::declval<F18>()())::Type;
  union { [[no_unique_address]] F18 pending18_; [[no_unique_address]] R18 ready18_; };
  using R19 = typename decltype(std::declval<F19>()())::Type;
  union { [[no_unique_address]] F19 pending19_; [[no_unique_address]] R19 ready19_; };
  using R20 = typename decltype(std::declval<F20>()())::Type;
  union { [[no_unique_address]] F20 pending20_; [[no_unique_address]] R20 ready20_; };
  using R21 = typename decltype(std::declval<F21>()())::Type;
  union { [[no_unique_address]] F21 pending21_; [[no_unique_address]] R21 ready21_; };
  using R22 = typename decltype(std::declval<F22>()())::Type;
  union { [[no_unique_address]] F22 pending22_; [[no_unique_address]] R22 ready22_; };
 public:
  Join(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7, F8 f8, F9 f9, F10 f10, F11 f11, F12 f12, F13 f13, F14 f14, F15 f15, F16 f16, F17 f17, F18 f18, F19 f19, F20 f20, F21 f21, F22 f22) : pending0_(std::move(f0)), pending1_(std::move(f1)), pending2_(std::move(f2)), pending3_(std::move(f3)), pending4_(std::move(f4)), pending5_(std::move(f5)), pending6_(std::move(f6)), pending7_(std::move(f7)), pending8_(std::move(f8)), pending9_(std::move(f9)), pending10_(std::move(f10)), pending11_(std::move(f11)), pending12_(std::move(f12)), pending13_(std::move(f13)), pending14_(std::move(f14)), pending15_(std::move(f15)), pending16_(std::move(f16)), pending17_(std::move(f17)), pending18_(std::move(f18)), pending19_(std::move(f19)), pending20_(std::move(f20)), pending21_(std::move(f21)), pending22_(std::move(f22)) {}
  Join& operator=(const Join&) = delete;
  Join(const Join& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
    Construct(&pending15_, std::move(other.pending15_));
    Construct(&pending16_, std::move(other.pending16_));
    Construct(&pending17_, std::move(other.pending17_));
    Construct(&pending18_, std::move(other.pending18_));
    Construct(&pending19_, std::move(other.pending19_));
    Construct(&pending20_, std::move(other.pending20_));
    Construct(&pending21_, std::move(other.pending21_));
    Construct(&pending22_, std::move(other.pending22_));
  }
  Join(Join&& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
    Construct(&pending15_, std::move(other.pending15_));
    Construct(&pending16_, std::move(other.pending16_));
    Construct(&pending17_, std::move(other.pending17_));
    Construct(&pending18_, std::move(other.pending18_));
    Construct(&pending19_, std::move(other.pending19_));
    Construct(&pending20_, std::move(other.pending20_));
    Construct(&pending21_, std::move(other.pending21_));
    Construct(&pending22_, std::move(other.pending22_));
  }
  ~Join() {
    if (state_ & 1) Destruct(&ready0_); else Destruct(&pending0_);
    if (state_ & 2) Destruct(&ready1_); else Destruct(&pending1_);
    if (state_ & 4) Destruct(&ready2_); else Destruct(&pending2_);
    if (state_ & 8) Destruct(&ready3_); else Destruct(&pending3_);
    if (state_ & 16) Destruct(&ready4_); else Destruct(&pending4_);
    if (state_ & 32) Destruct(&ready5_); else Destruct(&pending5_);
    if (state_ & 64) Destruct(&ready6_); else Destruct(&pending6_);
    if (state_ & 128) Destruct(&ready7_); else Destruct(&pending7_);
    if (state_ & 256) Destruct(&ready8_); else Destruct(&pending8_);
    if (state_ & 512) Destruct(&ready9_); else Destruct(&pending9_);
    if (state_ & 1024) Destruct(&ready10_); else Destruct(&pending10_);
    if (state_ & 2048) Destruct(&ready11_); else Destruct(&pending11_);
    if (state_ & 4096) Destruct(&ready12_); else Destruct(&pending12_);
    if (state_ & 8192) Destruct(&ready13_); else Destruct(&pending13_);
    if (state_ & 16384) Destruct(&ready14_); else Destruct(&pending14_);
    if (state_ & 32768) Destruct(&ready15_); else Destruct(&pending15_);
    if (state_ & 65536) Destruct(&ready16_); else Destruct(&pending16_);
    if (state_ & 131072) Destruct(&ready17_); else Destruct(&pending17_);
    if (state_ & 262144) Destruct(&ready18_); else Destruct(&pending18_);
    if (state_ & 524288) Destruct(&ready19_); else Destruct(&pending19_);
    if (state_ & 1048576) Destruct(&ready20_); else Destruct(&pending20_);
    if (state_ & 2097152) Destruct(&ready21_); else Destruct(&pending21_);
    if (state_ & 4194304) Destruct(&ready22_); else Destruct(&pending22_);
  }
  Poll<std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16, R17, R18, R19, R20, R21, R22>> operator()() {
    if ((state_ & 1) == 0) {
      auto r = pending0_();
      if (auto* p = r.get_ready()) { state_ |= 1; Destruct(&pending0_); Construct(&ready0_, std::move(*p)); }
    }
    if ((state_ & 2) == 0) {
      auto r = pending1_();
      if (auto* p = r.get_ready()) { state_ |= 2; Destruct(&pending1_); Construct(&ready1_, std::move(*p)); }
    }
    if ((state_ & 4) == 0) {
      auto r = pending2_();
      if (auto* p = r.get_ready()) { state_ |= 4; Destruct(&pending2_); Construct(&ready2_, std::move(*p)); }
    }
    if ((state_ & 8) == 0) {
      auto r = pending3_();
      if (auto* p = r.get_ready()) { state_ |= 8; Destruct(&pending3_); Construct(&ready3_, std::move(*p)); }
    }
    if ((state_ & 16) == 0) {
      auto r = pending4_();
      if (auto* p = r.get_ready()) { state_ |= 16; Destruct(&pending4_); Construct(&ready4_, std::move(*p)); }
    }
    if ((state_ & 32) == 0) {
      auto r = pending5_();
      if (auto* p = r.get_ready()) { state_ |= 32; Destruct(&pending5_); Construct(&ready5_, std::move(*p)); }
    }
    if ((state_ & 64) == 0) {
      auto r = pending6_();
      if (auto* p = r.get_ready()) { state_ |= 64; Destruct(&pending6_); Construct(&ready6_, std::move(*p)); }
    }
    if ((state_ & 128) == 0) {
      auto r = pending7_();
      if (auto* p = r.get_ready()) { state_ |= 128; Destruct(&pending7_); Construct(&ready7_, std::move(*p)); }
    }
    if ((state_ & 256) == 0) {
      auto r = pending8_();
      if (auto* p = r.get_ready()) { state_ |= 256; Destruct(&pending8_); Construct(&ready8_, std::move(*p)); }
    }
    if ((state_ & 512) == 0) {
      auto r = pending9_();
      if (auto* p = r.get_ready()) { state_ |= 512; Destruct(&pending9_); Construct(&ready9_, std::move(*p)); }
    }
    if ((state_ & 1024) == 0) {
      auto r = pending10_();
      if (auto* p = r.get_ready()) { state_ |= 1024; Destruct(&pending10_); Construct(&ready10_, std::move(*p)); }
    }
    if ((state_ & 2048) == 0) {
      auto r = pending11_();
      if (auto* p = r.get_ready()) { state_ |= 2048; Destruct(&pending11_); Construct(&ready11_, std::move(*p)); }
    }
    if ((state_ & 4096) == 0) {
      auto r = pending12_();
      if (auto* p = r.get_ready()) { state_ |= 4096; Destruct(&pending12_); Construct(&ready12_, std::move(*p)); }
    }
    if ((state_ & 8192) == 0) {
      auto r = pending13_();
      if (auto* p = r.get_ready()) { state_ |= 8192; Destruct(&pending13_); Construct(&ready13_, std::move(*p)); }
    }
    if ((state_ & 16384) == 0) {
      auto r = pending14_();
      if (auto* p = r.get_ready()) { state_ |= 16384; Destruct(&pending14_); Construct(&ready14_, std::move(*p)); }
    }
    if ((state_ & 32768) == 0) {
      auto r = pending15_();
      if (auto* p = r.get_ready()) { state_ |= 32768; Destruct(&pending15_); Construct(&ready15_, std::move(*p)); }
    }
    if ((state_ & 65536) == 0) {
      auto r = pending16_();
      if (auto* p = r.get_ready()) { state_ |= 65536; Destruct(&pending16_); Construct(&ready16_, std::move(*p)); }
    }
    if ((state_ & 131072) == 0) {
      auto r = pending17_();
      if (auto* p = r.get_ready()) { state_ |= 131072; Destruct(&pending17_); Construct(&ready17_, std::move(*p)); }
    }
    if ((state_ & 262144) == 0) {
      auto r = pending18_();
      if (auto* p = r.get_ready()) { state_ |= 262144; Destruct(&pending18_); Construct(&ready18_, std::move(*p)); }
    }
    if ((state_ & 524288) == 0) {
      auto r = pending19_();
      if (auto* p = r.get_ready()) { state_ |= 524288; Destruct(&pending19_); Construct(&ready19_, std::move(*p)); }
    }
    if ((state_ & 1048576) == 0) {
      auto r = pending20_();
      if (auto* p = r.get_ready()) { state_ |= 1048576; Destruct(&pending20_); Construct(&ready20_, std::move(*p)); }
    }
    if ((state_ & 2097152) == 0) {
      auto r = pending21_();
      if (auto* p = r.get_ready()) { state_ |= 2097152; Destruct(&pending21_); Construct(&ready21_, std::move(*p)); }
    }
    if ((state_ & 4194304) == 0) {
      auto r = pending22_();
      if (auto* p = r.get_ready()) { state_ |= 4194304; Destruct(&pending22_); Construct(&ready22_, std::move(*p)); }
    }
    if (state_ != 8388607) return kPending;
    return ready(std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16, R17, R18, R19, R20, R21, R22>(std::move(ready0_), std::move(ready1_), std::move(ready2_), std::move(ready3_), std::move(ready4_), std::move(ready5_), std::move(ready6_), std::move(ready7_), std::move(ready8_), std::move(ready9_), std::move(ready10_), std::move(ready11_), std::move(ready12_), std::move(ready13_), std::move(ready14_), std::move(ready15_), std::move(ready16_), std::move(ready17_), std::move(ready18_), std::move(ready19_), std::move(ready20_), std::move(ready21_), std::move(ready22_)));
  }
};
template <typename F0, typename F1, typename F2, typename F3, typename F4, typename F5, typename F6, typename F7, typename F8, typename F9, typename F10, typename F11, typename F12, typename F13, typename F14, typename F15, typename F16, typename F17, typename F18, typename F19, typename F20, typename F21, typename F22, typename F23> class Join<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23> {
 private:
  [[no_unique_address]] uint32_t state_ = 0;
  using R0 = typename decltype(std::declval<F0>()())::Type;
  union { [[no_unique_address]] F0 pending0_; [[no_unique_address]] R0 ready0_; };
  using R1 = typename decltype(std::declval<F1>()())::Type;
  union { [[no_unique_address]] F1 pending1_; [[no_unique_address]] R1 ready1_; };
  using R2 = typename decltype(std::declval<F2>()())::Type;
  union { [[no_unique_address]] F2 pending2_; [[no_unique_address]] R2 ready2_; };
  using R3 = typename decltype(std::declval<F3>()())::Type;
  union { [[no_unique_address]] F3 pending3_; [[no_unique_address]] R3 ready3_; };
  using R4 = typename decltype(std::declval<F4>()())::Type;
  union { [[no_unique_address]] F4 pending4_; [[no_unique_address]] R4 ready4_; };
  using R5 = typename decltype(std::declval<F5>()())::Type;
  union { [[no_unique_address]] F5 pending5_; [[no_unique_address]] R5 ready5_; };
  using R6 = typename decltype(std::declval<F6>()())::Type;
  union { [[no_unique_address]] F6 pending6_; [[no_unique_address]] R6 ready6_; };
  using R7 = typename decltype(std::declval<F7>()())::Type;
  union { [[no_unique_address]] F7 pending7_; [[no_unique_address]] R7 ready7_; };
  using R8 = typename decltype(std::declval<F8>()())::Type;
  union { [[no_unique_address]] F8 pending8_; [[no_unique_address]] R8 ready8_; };
  using R9 = typename decltype(std::declval<F9>()())::Type;
  union { [[no_unique_address]] F9 pending9_; [[no_unique_address]] R9 ready9_; };
  using R10 = typename decltype(std::declval<F10>()())::Type;
  union { [[no_unique_address]] F10 pending10_; [[no_unique_address]] R10 ready10_; };
  using R11 = typename decltype(std::declval<F11>()())::Type;
  union { [[no_unique_address]] F11 pending11_; [[no_unique_address]] R11 ready11_; };
  using R12 = typename decltype(std::declval<F12>()())::Type;
  union { [[no_unique_address]] F12 pending12_; [[no_unique_address]] R12 ready12_; };
  using R13 = typename decltype(std::declval<F13>()())::Type;
  union { [[no_unique_address]] F13 pending13_; [[no_unique_address]] R13 ready13_; };
  using R14 = typename decltype(std::declval<F14>()())::Type;
  union { [[no_unique_address]] F14 pending14_; [[no_unique_address]] R14 ready14_; };
  using R15 = typename decltype(std::declval<F15>()())::Type;
  union { [[no_unique_address]] F15 pending15_; [[no_unique_address]] R15 ready15_; };
  using R16 = typename decltype(std::declval<F16>()())::Type;
  union { [[no_unique_address]] F16 pending16_; [[no_unique_address]] R16 ready16_; };
  using R17 = typename decltype(std::declval<F17>()())::Type;
  union { [[no_unique_address]] F17 pending17_; [[no_unique_address]] R17 ready17_; };
  using R18 = typename decltype(std::declval<F18>()())::Type;
  union { [[no_unique_address]] F18 pending18_; [[no_unique_address]] R18 ready18_; };
  using R19 = typename decltype(std::declval<F19>()())::Type;
  union { [[no_unique_address]] F19 pending19_; [[no_unique_address]] R19 ready19_; };
  using R20 = typename decltype(std::declval<F20>()())::Type;
  union { [[no_unique_address]] F20 pending20_; [[no_unique_address]] R20 ready20_; };
  using R21 = typename decltype(std::declval<F21>()())::Type;
  union { [[no_unique_address]] F21 pending21_; [[no_unique_address]] R21 ready21_; };
  using R22 = typename decltype(std::declval<F22>()())::Type;
  union { [[no_unique_address]] F22 pending22_; [[no_unique_address]] R22 ready22_; };
  using R23 = typename decltype(std::declval<F23>()())::Type;
  union { [[no_unique_address]] F23 pending23_; [[no_unique_address]] R23 ready23_; };
 public:
  Join(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7, F8 f8, F9 f9, F10 f10, F11 f11, F12 f12, F13 f13, F14 f14, F15 f15, F16 f16, F17 f17, F18 f18, F19 f19, F20 f20, F21 f21, F22 f22, F23 f23) : pending0_(std::move(f0)), pending1_(std::move(f1)), pending2_(std::move(f2)), pending3_(std::move(f3)), pending4_(std::move(f4)), pending5_(std::move(f5)), pending6_(std::move(f6)), pending7_(std::move(f7)), pending8_(std::move(f8)), pending9_(std::move(f9)), pending10_(std::move(f10)), pending11_(std::move(f11)), pending12_(std::move(f12)), pending13_(std::move(f13)), pending14_(std::move(f14)), pending15_(std::move(f15)), pending16_(std::move(f16)), pending17_(std::move(f17)), pending18_(std::move(f18)), pending19_(std::move(f19)), pending20_(std::move(f20)), pending21_(std::move(f21)), pending22_(std::move(f22)), pending23_(std::move(f23)) {}
  Join& operator=(const Join&) = delete;
  Join(const Join& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
    Construct(&pending15_, std::move(other.pending15_));
    Construct(&pending16_, std::move(other.pending16_));
    Construct(&pending17_, std::move(other.pending17_));
    Construct(&pending18_, std::move(other.pending18_));
    Construct(&pending19_, std::move(other.pending19_));
    Construct(&pending20_, std::move(other.pending20_));
    Construct(&pending21_, std::move(other.pending21_));
    Construct(&pending22_, std::move(other.pending22_));
    Construct(&pending23_, std::move(other.pending23_));
  }
  Join(Join&& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
    Construct(&pending15_, std::move(other.pending15_));
    Construct(&pending16_, std::move(other.pending16_));
    Construct(&pending17_, std::move(other.pending17_));
    Construct(&pending18_, std::move(other.pending18_));
    Construct(&pending19_, std::move(other.pending19_));
    Construct(&pending20_, std::move(other.pending20_));
    Construct(&pending21_, std::move(other.pending21_));
    Construct(&pending22_, std::move(other.pending22_));
    Construct(&pending23_, std::move(other.pending23_));
  }
  ~Join() {
    if (state_ & 1) Destruct(&ready0_); else Destruct(&pending0_);
    if (state_ & 2) Destruct(&ready1_); else Destruct(&pending1_);
    if (state_ & 4) Destruct(&ready2_); else Destruct(&pending2_);
    if (state_ & 8) Destruct(&ready3_); else Destruct(&pending3_);
    if (state_ & 16) Destruct(&ready4_); else Destruct(&pending4_);
    if (state_ & 32) Destruct(&ready5_); else Destruct(&pending5_);
    if (state_ & 64) Destruct(&ready6_); else Destruct(&pending6_);
    if (state_ & 128) Destruct(&ready7_); else Destruct(&pending7_);
    if (state_ & 256) Destruct(&ready8_); else Destruct(&pending8_);
    if (state_ & 512) Destruct(&ready9_); else Destruct(&pending9_);
    if (state_ & 1024) Destruct(&ready10_); else Destruct(&pending10_);
    if (state_ & 2048) Destruct(&ready11_); else Destruct(&pending11_);
    if (state_ & 4096) Destruct(&ready12_); else Destruct(&pending12_);
    if (state_ & 8192) Destruct(&ready13_); else Destruct(&pending13_);
    if (state_ & 16384) Destruct(&ready14_); else Destruct(&pending14_);
    if (state_ & 32768) Destruct(&ready15_); else Destruct(&pending15_);
    if (state_ & 65536) Destruct(&ready16_); else Destruct(&pending16_);
    if (state_ & 131072) Destruct(&ready17_); else Destruct(&pending17_);
    if (state_ & 262144) Destruct(&ready18_); else Destruct(&pending18_);
    if (state_ & 524288) Destruct(&ready19_); else Destruct(&pending19_);
    if (state_ & 1048576) Destruct(&ready20_); else Destruct(&pending20_);
    if (state_ & 2097152) Destruct(&ready21_); else Destruct(&pending21_);
    if (state_ & 4194304) Destruct(&ready22_); else Destruct(&pending22_);
    if (state_ & 8388608) Destruct(&ready23_); else Destruct(&pending23_);
  }
  Poll<std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16, R17, R18, R19, R20, R21, R22, R23>> operator()() {
    if ((state_ & 1) == 0) {
      auto r = pending0_();
      if (auto* p = r.get_ready()) { state_ |= 1; Destruct(&pending0_); Construct(&ready0_, std::move(*p)); }
    }
    if ((state_ & 2) == 0) {
      auto r = pending1_();
      if (auto* p = r.get_ready()) { state_ |= 2; Destruct(&pending1_); Construct(&ready1_, std::move(*p)); }
    }
    if ((state_ & 4) == 0) {
      auto r = pending2_();
      if (auto* p = r.get_ready()) { state_ |= 4; Destruct(&pending2_); Construct(&ready2_, std::move(*p)); }
    }
    if ((state_ & 8) == 0) {
      auto r = pending3_();
      if (auto* p = r.get_ready()) { state_ |= 8; Destruct(&pending3_); Construct(&ready3_, std::move(*p)); }
    }
    if ((state_ & 16) == 0) {
      auto r = pending4_();
      if (auto* p = r.get_ready()) { state_ |= 16; Destruct(&pending4_); Construct(&ready4_, std::move(*p)); }
    }
    if ((state_ & 32) == 0) {
      auto r = pending5_();
      if (auto* p = r.get_ready()) { state_ |= 32; Destruct(&pending5_); Construct(&ready5_, std::move(*p)); }
    }
    if ((state_ & 64) == 0) {
      auto r = pending6_();
      if (auto* p = r.get_ready()) { state_ |= 64; Destruct(&pending6_); Construct(&ready6_, std::move(*p)); }
    }
    if ((state_ & 128) == 0) {
      auto r = pending7_();
      if (auto* p = r.get_ready()) { state_ |= 128; Destruct(&pending7_); Construct(&ready7_, std::move(*p)); }
    }
    if ((state_ & 256) == 0) {
      auto r = pending8_();
      if (auto* p = r.get_ready()) { state_ |= 256; Destruct(&pending8_); Construct(&ready8_, std::move(*p)); }
    }
    if ((state_ & 512) == 0) {
      auto r = pending9_();
      if (auto* p = r.get_ready()) { state_ |= 512; Destruct(&pending9_); Construct(&ready9_, std::move(*p)); }
    }
    if ((state_ & 1024) == 0) {
      auto r = pending10_();
      if (auto* p = r.get_ready()) { state_ |= 1024; Destruct(&pending10_); Construct(&ready10_, std::move(*p)); }
    }
    if ((state_ & 2048) == 0) {
      auto r = pending11_();
      if (auto* p = r.get_ready()) { state_ |= 2048; Destruct(&pending11_); Construct(&ready11_, std::move(*p)); }
    }
    if ((state_ & 4096) == 0) {
      auto r = pending12_();
      if (auto* p = r.get_ready()) { state_ |= 4096; Destruct(&pending12_); Construct(&ready12_, std::move(*p)); }
    }
    if ((state_ & 8192) == 0) {
      auto r = pending13_();
      if (auto* p = r.get_ready()) { state_ |= 8192; Destruct(&pending13_); Construct(&ready13_, std::move(*p)); }
    }
    if ((state_ & 16384) == 0) {
      auto r = pending14_();
      if (auto* p = r.get_ready()) { state_ |= 16384; Destruct(&pending14_); Construct(&ready14_, std::move(*p)); }
    }
    if ((state_ & 32768) == 0) {
      auto r = pending15_();
      if (auto* p = r.get_ready()) { state_ |= 32768; Destruct(&pending15_); Construct(&ready15_, std::move(*p)); }
    }
    if ((state_ & 65536) == 0) {
      auto r = pending16_();
      if (auto* p = r.get_ready()) { state_ |= 65536; Destruct(&pending16_); Construct(&ready16_, std::move(*p)); }
    }
    if ((state_ & 131072) == 0) {
      auto r = pending17_();
      if (auto* p = r.get_ready()) { state_ |= 131072; Destruct(&pending17_); Construct(&ready17_, std::move(*p)); }
    }
    if ((state_ & 262144) == 0) {
      auto r = pending18_();
      if (auto* p = r.get_ready()) { state_ |= 262144; Destruct(&pending18_); Construct(&ready18_, std::move(*p)); }
    }
    if ((state_ & 524288) == 0) {
      auto r = pending19_();
      if (auto* p = r.get_ready()) { state_ |= 524288; Destruct(&pending19_); Construct(&ready19_, std::move(*p)); }
    }
    if ((state_ & 1048576) == 0) {
      auto r = pending20_();
      if (auto* p = r.get_ready()) { state_ |= 1048576; Destruct(&pending20_); Construct(&ready20_, std::move(*p)); }
    }
    if ((state_ & 2097152) == 0) {
      auto r = pending21_();
      if (auto* p = r.get_ready()) { state_ |= 2097152; Destruct(&pending21_); Construct(&ready21_, std::move(*p)); }
    }
    if ((state_ & 4194304) == 0) {
      auto r = pending22_();
      if (auto* p = r.get_ready()) { state_ |= 4194304; Destruct(&pending22_); Construct(&ready22_, std::move(*p)); }
    }
    if ((state_ & 8388608) == 0) {
      auto r = pending23_();
      if (auto* p = r.get_ready()) { state_ |= 8388608; Destruct(&pending23_); Construct(&ready23_, std::move(*p)); }
    }
    if (state_ != 16777215) return kPending;
    return ready(std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16, R17, R18, R19, R20, R21, R22, R23>(std::move(ready0_), std::move(ready1_), std::move(ready2_), std::move(ready3_), std::move(ready4_), std::move(ready5_), std::move(ready6_), std::move(ready7_), std::move(ready8_), std::move(ready9_), std::move(ready10_), std::move(ready11_), std::move(ready12_), std::move(ready13_), std::move(ready14_), std::move(ready15_), std::move(ready16_), std::move(ready17_), std::move(ready18_), std::move(ready19_), std::move(ready20_), std::move(ready21_), std::move(ready22_), std::move(ready23_)));
  }
};
template <typename F0, typename F1, typename F2, typename F3, typename F4, typename F5, typename F6, typename F7, typename F8, typename F9, typename F10, typename F11, typename F12, typename F13, typename F14, typename F15, typename F16, typename F17, typename F18, typename F19, typename F20, typename F21, typename F22, typename F23, typename F24> class Join<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24> {
 private:
  [[no_unique_address]] uint32_t state_ = 0;
  using R0 = typename decltype(std::declval<F0>()())::Type;
  union { [[no_unique_address]] F0 pending0_; [[no_unique_address]] R0 ready0_; };
  using R1 = typename decltype(std::declval<F1>()())::Type;
  union { [[no_unique_address]] F1 pending1_; [[no_unique_address]] R1 ready1_; };
  using R2 = typename decltype(std::declval<F2>()())::Type;
  union { [[no_unique_address]] F2 pending2_; [[no_unique_address]] R2 ready2_; };
  using R3 = typename decltype(std::declval<F3>()())::Type;
  union { [[no_unique_address]] F3 pending3_; [[no_unique_address]] R3 ready3_; };
  using R4 = typename decltype(std::declval<F4>()())::Type;
  union { [[no_unique_address]] F4 pending4_; [[no_unique_address]] R4 ready4_; };
  using R5 = typename decltype(std::declval<F5>()())::Type;
  union { [[no_unique_address]] F5 pending5_; [[no_unique_address]] R5 ready5_; };
  using R6 = typename decltype(std::declval<F6>()())::Type;
  union { [[no_unique_address]] F6 pending6_; [[no_unique_address]] R6 ready6_; };
  using R7 = typename decltype(std::declval<F7>()())::Type;
  union { [[no_unique_address]] F7 pending7_; [[no_unique_address]] R7 ready7_; };
  using R8 = typename decltype(std::declval<F8>()())::Type;
  union { [[no_unique_address]] F8 pending8_; [[no_unique_address]] R8 ready8_; };
  using R9 = typename decltype(std::declval<F9>()())::Type;
  union { [[no_unique_address]] F9 pending9_; [[no_unique_address]] R9 ready9_; };
  using R10 = typename decltype(std::declval<F10>()())::Type;
  union { [[no_unique_address]] F10 pending10_; [[no_unique_address]] R10 ready10_; };
  using R11 = typename decltype(std::declval<F11>()())::Type;
  union { [[no_unique_address]] F11 pending11_; [[no_unique_address]] R11 ready11_; };
  using R12 = typename decltype(std::declval<F12>()())::Type;
  union { [[no_unique_address]] F12 pending12_; [[no_unique_address]] R12 ready12_; };
  using R13 = typename decltype(std::declval<F13>()())::Type;
  union { [[no_unique_address]] F13 pending13_; [[no_unique_address]] R13 ready13_; };
  using R14 = typename decltype(std::declval<F14>()())::Type;
  union { [[no_unique_address]] F14 pending14_; [[no_unique_address]] R14 ready14_; };
  using R15 = typename decltype(std::declval<F15>()())::Type;
  union { [[no_unique_address]] F15 pending15_; [[no_unique_address]] R15 ready15_; };
  using R16 = typename decltype(std::declval<F16>()())::Type;
  union { [[no_unique_address]] F16 pending16_; [[no_unique_address]] R16 ready16_; };
  using R17 = typename decltype(std::declval<F17>()())::Type;
  union { [[no_unique_address]] F17 pending17_; [[no_unique_address]] R17 ready17_; };
  using R18 = typename decltype(std::declval<F18>()())::Type;
  union { [[no_unique_address]] F18 pending18_; [[no_unique_address]] R18 ready18_; };
  using R19 = typename decltype(std::declval<F19>()())::Type;
  union { [[no_unique_address]] F19 pending19_; [[no_unique_address]] R19 ready19_; };
  using R20 = typename decltype(std::declval<F20>()())::Type;
  union { [[no_unique_address]] F20 pending20_; [[no_unique_address]] R20 ready20_; };
  using R21 = typename decltype(std::declval<F21>()())::Type;
  union { [[no_unique_address]] F21 pending21_; [[no_unique_address]] R21 ready21_; };
  using R22 = typename decltype(std::declval<F22>()())::Type;
  union { [[no_unique_address]] F22 pending22_; [[no_unique_address]] R22 ready22_; };
  using R23 = typename decltype(std::declval<F23>()())::Type;
  union { [[no_unique_address]] F23 pending23_; [[no_unique_address]] R23 ready23_; };
  using R24 = typename decltype(std::declval<F24>()())::Type;
  union { [[no_unique_address]] F24 pending24_; [[no_unique_address]] R24 ready24_; };
 public:
  Join(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7, F8 f8, F9 f9, F10 f10, F11 f11, F12 f12, F13 f13, F14 f14, F15 f15, F16 f16, F17 f17, F18 f18, F19 f19, F20 f20, F21 f21, F22 f22, F23 f23, F24 f24) : pending0_(std::move(f0)), pending1_(std::move(f1)), pending2_(std::move(f2)), pending3_(std::move(f3)), pending4_(std::move(f4)), pending5_(std::move(f5)), pending6_(std::move(f6)), pending7_(std::move(f7)), pending8_(std::move(f8)), pending9_(std::move(f9)), pending10_(std::move(f10)), pending11_(std::move(f11)), pending12_(std::move(f12)), pending13_(std::move(f13)), pending14_(std::move(f14)), pending15_(std::move(f15)), pending16_(std::move(f16)), pending17_(std::move(f17)), pending18_(std::move(f18)), pending19_(std::move(f19)), pending20_(std::move(f20)), pending21_(std::move(f21)), pending22_(std::move(f22)), pending23_(std::move(f23)), pending24_(std::move(f24)) {}
  Join& operator=(const Join&) = delete;
  Join(const Join& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
    Construct(&pending15_, std::move(other.pending15_));
    Construct(&pending16_, std::move(other.pending16_));
    Construct(&pending17_, std::move(other.pending17_));
    Construct(&pending18_, std::move(other.pending18_));
    Construct(&pending19_, std::move(other.pending19_));
    Construct(&pending20_, std::move(other.pending20_));
    Construct(&pending21_, std::move(other.pending21_));
    Construct(&pending22_, std::move(other.pending22_));
    Construct(&pending23_, std::move(other.pending23_));
    Construct(&pending24_, std::move(other.pending24_));
  }
  Join(Join&& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
    Construct(&pending15_, std::move(other.pending15_));
    Construct(&pending16_, std::move(other.pending16_));
    Construct(&pending17_, std::move(other.pending17_));
    Construct(&pending18_, std::move(other.pending18_));
    Construct(&pending19_, std::move(other.pending19_));
    Construct(&pending20_, std::move(other.pending20_));
    Construct(&pending21_, std::move(other.pending21_));
    Construct(&pending22_, std::move(other.pending22_));
    Construct(&pending23_, std::move(other.pending23_));
    Construct(&pending24_, std::move(other.pending24_));
  }
  ~Join() {
    if (state_ & 1) Destruct(&ready0_); else Destruct(&pending0_);
    if (state_ & 2) Destruct(&ready1_); else Destruct(&pending1_);
    if (state_ & 4) Destruct(&ready2_); else Destruct(&pending2_);
    if (state_ & 8) Destruct(&ready3_); else Destruct(&pending3_);
    if (state_ & 16) Destruct(&ready4_); else Destruct(&pending4_);
    if (state_ & 32) Destruct(&ready5_); else Destruct(&pending5_);
    if (state_ & 64) Destruct(&ready6_); else Destruct(&pending6_);
    if (state_ & 128) Destruct(&ready7_); else Destruct(&pending7_);
    if (state_ & 256) Destruct(&ready8_); else Destruct(&pending8_);
    if (state_ & 512) Destruct(&ready9_); else Destruct(&pending9_);
    if (state_ & 1024) Destruct(&ready10_); else Destruct(&pending10_);
    if (state_ & 2048) Destruct(&ready11_); else Destruct(&pending11_);
    if (state_ & 4096) Destruct(&ready12_); else Destruct(&pending12_);
    if (state_ & 8192) Destruct(&ready13_); else Destruct(&pending13_);
    if (state_ & 16384) Destruct(&ready14_); else Destruct(&pending14_);
    if (state_ & 32768) Destruct(&ready15_); else Destruct(&pending15_);
    if (state_ & 65536) Destruct(&ready16_); else Destruct(&pending16_);
    if (state_ & 131072) Destruct(&ready17_); else Destruct(&pending17_);
    if (state_ & 262144) Destruct(&ready18_); else Destruct(&pending18_);
    if (state_ & 524288) Destruct(&ready19_); else Destruct(&pending19_);
    if (state_ & 1048576) Destruct(&ready20_); else Destruct(&pending20_);
    if (state_ & 2097152) Destruct(&ready21_); else Destruct(&pending21_);
    if (state_ & 4194304) Destruct(&ready22_); else Destruct(&pending22_);
    if (state_ & 8388608) Destruct(&ready23_); else Destruct(&pending23_);
    if (state_ & 16777216) Destruct(&ready24_); else Destruct(&pending24_);
  }
  Poll<std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16, R17, R18, R19, R20, R21, R22, R23, R24>> operator()() {
    if ((state_ & 1) == 0) {
      auto r = pending0_();
      if (auto* p = r.get_ready()) { state_ |= 1; Destruct(&pending0_); Construct(&ready0_, std::move(*p)); }
    }
    if ((state_ & 2) == 0) {
      auto r = pending1_();
      if (auto* p = r.get_ready()) { state_ |= 2; Destruct(&pending1_); Construct(&ready1_, std::move(*p)); }
    }
    if ((state_ & 4) == 0) {
      auto r = pending2_();
      if (auto* p = r.get_ready()) { state_ |= 4; Destruct(&pending2_); Construct(&ready2_, std::move(*p)); }
    }
    if ((state_ & 8) == 0) {
      auto r = pending3_();
      if (auto* p = r.get_ready()) { state_ |= 8; Destruct(&pending3_); Construct(&ready3_, std::move(*p)); }
    }
    if ((state_ & 16) == 0) {
      auto r = pending4_();
      if (auto* p = r.get_ready()) { state_ |= 16; Destruct(&pending4_); Construct(&ready4_, std::move(*p)); }
    }
    if ((state_ & 32) == 0) {
      auto r = pending5_();
      if (auto* p = r.get_ready()) { state_ |= 32; Destruct(&pending5_); Construct(&ready5_, std::move(*p)); }
    }
    if ((state_ & 64) == 0) {
      auto r = pending6_();
      if (auto* p = r.get_ready()) { state_ |= 64; Destruct(&pending6_); Construct(&ready6_, std::move(*p)); }
    }
    if ((state_ & 128) == 0) {
      auto r = pending7_();
      if (auto* p = r.get_ready()) { state_ |= 128; Destruct(&pending7_); Construct(&ready7_, std::move(*p)); }
    }
    if ((state_ & 256) == 0) {
      auto r = pending8_();
      if (auto* p = r.get_ready()) { state_ |= 256; Destruct(&pending8_); Construct(&ready8_, std::move(*p)); }
    }
    if ((state_ & 512) == 0) {
      auto r = pending9_();
      if (auto* p = r.get_ready()) { state_ |= 512; Destruct(&pending9_); Construct(&ready9_, std::move(*p)); }
    }
    if ((state_ & 1024) == 0) {
      auto r = pending10_();
      if (auto* p = r.get_ready()) { state_ |= 1024; Destruct(&pending10_); Construct(&ready10_, std::move(*p)); }
    }
    if ((state_ & 2048) == 0) {
      auto r = pending11_();
      if (auto* p = r.get_ready()) { state_ |= 2048; Destruct(&pending11_); Construct(&ready11_, std::move(*p)); }
    }
    if ((state_ & 4096) == 0) {
      auto r = pending12_();
      if (auto* p = r.get_ready()) { state_ |= 4096; Destruct(&pending12_); Construct(&ready12_, std::move(*p)); }
    }
    if ((state_ & 8192) == 0) {
      auto r = pending13_();
      if (auto* p = r.get_ready()) { state_ |= 8192; Destruct(&pending13_); Construct(&ready13_, std::move(*p)); }
    }
    if ((state_ & 16384) == 0) {
      auto r = pending14_();
      if (auto* p = r.get_ready()) { state_ |= 16384; Destruct(&pending14_); Construct(&ready14_, std::move(*p)); }
    }
    if ((state_ & 32768) == 0) {
      auto r = pending15_();
      if (auto* p = r.get_ready()) { state_ |= 32768; Destruct(&pending15_); Construct(&ready15_, std::move(*p)); }
    }
    if ((state_ & 65536) == 0) {
      auto r = pending16_();
      if (auto* p = r.get_ready()) { state_ |= 65536; Destruct(&pending16_); Construct(&ready16_, std::move(*p)); }
    }
    if ((state_ & 131072) == 0) {
      auto r = pending17_();
      if (auto* p = r.get_ready()) { state_ |= 131072; Destruct(&pending17_); Construct(&ready17_, std::move(*p)); }
    }
    if ((state_ & 262144) == 0) {
      auto r = pending18_();
      if (auto* p = r.get_ready()) { state_ |= 262144; Destruct(&pending18_); Construct(&ready18_, std::move(*p)); }
    }
    if ((state_ & 524288) == 0) {
      auto r = pending19_();
      if (auto* p = r.get_ready()) { state_ |= 524288; Destruct(&pending19_); Construct(&ready19_, std::move(*p)); }
    }
    if ((state_ & 1048576) == 0) {
      auto r = pending20_();
      if (auto* p = r.get_ready()) { state_ |= 1048576; Destruct(&pending20_); Construct(&ready20_, std::move(*p)); }
    }
    if ((state_ & 2097152) == 0) {
      auto r = pending21_();
      if (auto* p = r.get_ready()) { state_ |= 2097152; Destruct(&pending21_); Construct(&ready21_, std::move(*p)); }
    }
    if ((state_ & 4194304) == 0) {
      auto r = pending22_();
      if (auto* p = r.get_ready()) { state_ |= 4194304; Destruct(&pending22_); Construct(&ready22_, std::move(*p)); }
    }
    if ((state_ & 8388608) == 0) {
      auto r = pending23_();
      if (auto* p = r.get_ready()) { state_ |= 8388608; Destruct(&pending23_); Construct(&ready23_, std::move(*p)); }
    }
    if ((state_ & 16777216) == 0) {
      auto r = pending24_();
      if (auto* p = r.get_ready()) { state_ |= 16777216; Destruct(&pending24_); Construct(&ready24_, std::move(*p)); }
    }
    if (state_ != 33554431) return kPending;
    return ready(std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16, R17, R18, R19, R20, R21, R22, R23, R24>(std::move(ready0_), std::move(ready1_), std::move(ready2_), std::move(ready3_), std::move(ready4_), std::move(ready5_), std::move(ready6_), std::move(ready7_), std::move(ready8_), std::move(ready9_), std::move(ready10_), std::move(ready11_), std::move(ready12_), std::move(ready13_), std::move(ready14_), std::move(ready15_), std::move(ready16_), std::move(ready17_), std::move(ready18_), std::move(ready19_), std::move(ready20_), std::move(ready21_), std::move(ready22_), std::move(ready23_), std::move(ready24_)));
  }
};
template <typename F0, typename F1, typename F2, typename F3, typename F4, typename F5, typename F6, typename F7, typename F8, typename F9, typename F10, typename F11, typename F12, typename F13, typename F14, typename F15, typename F16, typename F17, typename F18, typename F19, typename F20, typename F21, typename F22, typename F23, typename F24, typename F25> class Join<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24, F25> {
 private:
  [[no_unique_address]] uint32_t state_ = 0;
  using R0 = typename decltype(std::declval<F0>()())::Type;
  union { [[no_unique_address]] F0 pending0_; [[no_unique_address]] R0 ready0_; };
  using R1 = typename decltype(std::declval<F1>()())::Type;
  union { [[no_unique_address]] F1 pending1_; [[no_unique_address]] R1 ready1_; };
  using R2 = typename decltype(std::declval<F2>()())::Type;
  union { [[no_unique_address]] F2 pending2_; [[no_unique_address]] R2 ready2_; };
  using R3 = typename decltype(std::declval<F3>()())::Type;
  union { [[no_unique_address]] F3 pending3_; [[no_unique_address]] R3 ready3_; };
  using R4 = typename decltype(std::declval<F4>()())::Type;
  union { [[no_unique_address]] F4 pending4_; [[no_unique_address]] R4 ready4_; };
  using R5 = typename decltype(std::declval<F5>()())::Type;
  union { [[no_unique_address]] F5 pending5_; [[no_unique_address]] R5 ready5_; };
  using R6 = typename decltype(std::declval<F6>()())::Type;
  union { [[no_unique_address]] F6 pending6_; [[no_unique_address]] R6 ready6_; };
  using R7 = typename decltype(std::declval<F7>()())::Type;
  union { [[no_unique_address]] F7 pending7_; [[no_unique_address]] R7 ready7_; };
  using R8 = typename decltype(std::declval<F8>()())::Type;
  union { [[no_unique_address]] F8 pending8_; [[no_unique_address]] R8 ready8_; };
  using R9 = typename decltype(std::declval<F9>()())::Type;
  union { [[no_unique_address]] F9 pending9_; [[no_unique_address]] R9 ready9_; };
  using R10 = typename decltype(std::declval<F10>()())::Type;
  union { [[no_unique_address]] F10 pending10_; [[no_unique_address]] R10 ready10_; };
  using R11 = typename decltype(std::declval<F11>()())::Type;
  union { [[no_unique_address]] F11 pending11_; [[no_unique_address]] R11 ready11_; };
  using R12 = typename decltype(std::declval<F12>()())::Type;
  union { [[no_unique_address]] F12 pending12_; [[no_unique_address]] R12 ready12_; };
  using R13 = typename decltype(std::declval<F13>()())::Type;
  union { [[no_unique_address]] F13 pending13_; [[no_unique_address]] R13 ready13_; };
  using R14 = typename decltype(std::declval<F14>()())::Type;
  union { [[no_unique_address]] F14 pending14_; [[no_unique_address]] R14 ready14_; };
  using R15 = typename decltype(std::declval<F15>()())::Type;
  union { [[no_unique_address]] F15 pending15_; [[no_unique_address]] R15 ready15_; };
  using R16 = typename decltype(std::declval<F16>()())::Type;
  union { [[no_unique_address]] F16 pending16_; [[no_unique_address]] R16 ready16_; };
  using R17 = typename decltype(std::declval<F17>()())::Type;
  union { [[no_unique_address]] F17 pending17_; [[no_unique_address]] R17 ready17_; };
  using R18 = typename decltype(std::declval<F18>()())::Type;
  union { [[no_unique_address]] F18 pending18_; [[no_unique_address]] R18 ready18_; };
  using R19 = typename decltype(std::declval<F19>()())::Type;
  union { [[no_unique_address]] F19 pending19_; [[no_unique_address]] R19 ready19_; };
  using R20 = typename decltype(std::declval<F20>()())::Type;
  union { [[no_unique_address]] F20 pending20_; [[no_unique_address]] R20 ready20_; };
  using R21 = typename decltype(std::declval<F21>()())::Type;
  union { [[no_unique_address]] F21 pending21_; [[no_unique_address]] R21 ready21_; };
  using R22 = typename decltype(std::declval<F22>()())::Type;
  union { [[no_unique_address]] F22 pending22_; [[no_unique_address]] R22 ready22_; };
  using R23 = typename decltype(std::declval<F23>()())::Type;
  union { [[no_unique_address]] F23 pending23_; [[no_unique_address]] R23 ready23_; };
  using R24 = typename decltype(std::declval<F24>()())::Type;
  union { [[no_unique_address]] F24 pending24_; [[no_unique_address]] R24 ready24_; };
  using R25 = typename decltype(std::declval<F25>()())::Type;
  union { [[no_unique_address]] F25 pending25_; [[no_unique_address]] R25 ready25_; };
 public:
  Join(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7, F8 f8, F9 f9, F10 f10, F11 f11, F12 f12, F13 f13, F14 f14, F15 f15, F16 f16, F17 f17, F18 f18, F19 f19, F20 f20, F21 f21, F22 f22, F23 f23, F24 f24, F25 f25) : pending0_(std::move(f0)), pending1_(std::move(f1)), pending2_(std::move(f2)), pending3_(std::move(f3)), pending4_(std::move(f4)), pending5_(std::move(f5)), pending6_(std::move(f6)), pending7_(std::move(f7)), pending8_(std::move(f8)), pending9_(std::move(f9)), pending10_(std::move(f10)), pending11_(std::move(f11)), pending12_(std::move(f12)), pending13_(std::move(f13)), pending14_(std::move(f14)), pending15_(std::move(f15)), pending16_(std::move(f16)), pending17_(std::move(f17)), pending18_(std::move(f18)), pending19_(std::move(f19)), pending20_(std::move(f20)), pending21_(std::move(f21)), pending22_(std::move(f22)), pending23_(std::move(f23)), pending24_(std::move(f24)), pending25_(std::move(f25)) {}
  Join& operator=(const Join&) = delete;
  Join(const Join& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
    Construct(&pending15_, std::move(other.pending15_));
    Construct(&pending16_, std::move(other.pending16_));
    Construct(&pending17_, std::move(other.pending17_));
    Construct(&pending18_, std::move(other.pending18_));
    Construct(&pending19_, std::move(other.pending19_));
    Construct(&pending20_, std::move(other.pending20_));
    Construct(&pending21_, std::move(other.pending21_));
    Construct(&pending22_, std::move(other.pending22_));
    Construct(&pending23_, std::move(other.pending23_));
    Construct(&pending24_, std::move(other.pending24_));
    Construct(&pending25_, std::move(other.pending25_));
  }
  Join(Join&& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
    Construct(&pending15_, std::move(other.pending15_));
    Construct(&pending16_, std::move(other.pending16_));
    Construct(&pending17_, std::move(other.pending17_));
    Construct(&pending18_, std::move(other.pending18_));
    Construct(&pending19_, std::move(other.pending19_));
    Construct(&pending20_, std::move(other.pending20_));
    Construct(&pending21_, std::move(other.pending21_));
    Construct(&pending22_, std::move(other.pending22_));
    Construct(&pending23_, std::move(other.pending23_));
    Construct(&pending24_, std::move(other.pending24_));
    Construct(&pending25_, std::move(other.pending25_));
  }
  ~Join() {
    if (state_ & 1) Destruct(&ready0_); else Destruct(&pending0_);
    if (state_ & 2) Destruct(&ready1_); else Destruct(&pending1_);
    if (state_ & 4) Destruct(&ready2_); else Destruct(&pending2_);
    if (state_ & 8) Destruct(&ready3_); else Destruct(&pending3_);
    if (state_ & 16) Destruct(&ready4_); else Destruct(&pending4_);
    if (state_ & 32) Destruct(&ready5_); else Destruct(&pending5_);
    if (state_ & 64) Destruct(&ready6_); else Destruct(&pending6_);
    if (state_ & 128) Destruct(&ready7_); else Destruct(&pending7_);
    if (state_ & 256) Destruct(&ready8_); else Destruct(&pending8_);
    if (state_ & 512) Destruct(&ready9_); else Destruct(&pending9_);
    if (state_ & 1024) Destruct(&ready10_); else Destruct(&pending10_);
    if (state_ & 2048) Destruct(&ready11_); else Destruct(&pending11_);
    if (state_ & 4096) Destruct(&ready12_); else Destruct(&pending12_);
    if (state_ & 8192) Destruct(&ready13_); else Destruct(&pending13_);
    if (state_ & 16384) Destruct(&ready14_); else Destruct(&pending14_);
    if (state_ & 32768) Destruct(&ready15_); else Destruct(&pending15_);
    if (state_ & 65536) Destruct(&ready16_); else Destruct(&pending16_);
    if (state_ & 131072) Destruct(&ready17_); else Destruct(&pending17_);
    if (state_ & 262144) Destruct(&ready18_); else Destruct(&pending18_);
    if (state_ & 524288) Destruct(&ready19_); else Destruct(&pending19_);
    if (state_ & 1048576) Destruct(&ready20_); else Destruct(&pending20_);
    if (state_ & 2097152) Destruct(&ready21_); else Destruct(&pending21_);
    if (state_ & 4194304) Destruct(&ready22_); else Destruct(&pending22_);
    if (state_ & 8388608) Destruct(&ready23_); else Destruct(&pending23_);
    if (state_ & 16777216) Destruct(&ready24_); else Destruct(&pending24_);
    if (state_ & 33554432) Destruct(&ready25_); else Destruct(&pending25_);
  }
  Poll<std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16, R17, R18, R19, R20, R21, R22, R23, R24, R25>> operator()() {
    if ((state_ & 1) == 0) {
      auto r = pending0_();
      if (auto* p = r.get_ready()) { state_ |= 1; Destruct(&pending0_); Construct(&ready0_, std::move(*p)); }
    }
    if ((state_ & 2) == 0) {
      auto r = pending1_();
      if (auto* p = r.get_ready()) { state_ |= 2; Destruct(&pending1_); Construct(&ready1_, std::move(*p)); }
    }
    if ((state_ & 4) == 0) {
      auto r = pending2_();
      if (auto* p = r.get_ready()) { state_ |= 4; Destruct(&pending2_); Construct(&ready2_, std::move(*p)); }
    }
    if ((state_ & 8) == 0) {
      auto r = pending3_();
      if (auto* p = r.get_ready()) { state_ |= 8; Destruct(&pending3_); Construct(&ready3_, std::move(*p)); }
    }
    if ((state_ & 16) == 0) {
      auto r = pending4_();
      if (auto* p = r.get_ready()) { state_ |= 16; Destruct(&pending4_); Construct(&ready4_, std::move(*p)); }
    }
    if ((state_ & 32) == 0) {
      auto r = pending5_();
      if (auto* p = r.get_ready()) { state_ |= 32; Destruct(&pending5_); Construct(&ready5_, std::move(*p)); }
    }
    if ((state_ & 64) == 0) {
      auto r = pending6_();
      if (auto* p = r.get_ready()) { state_ |= 64; Destruct(&pending6_); Construct(&ready6_, std::move(*p)); }
    }
    if ((state_ & 128) == 0) {
      auto r = pending7_();
      if (auto* p = r.get_ready()) { state_ |= 128; Destruct(&pending7_); Construct(&ready7_, std::move(*p)); }
    }
    if ((state_ & 256) == 0) {
      auto r = pending8_();
      if (auto* p = r.get_ready()) { state_ |= 256; Destruct(&pending8_); Construct(&ready8_, std::move(*p)); }
    }
    if ((state_ & 512) == 0) {
      auto r = pending9_();
      if (auto* p = r.get_ready()) { state_ |= 512; Destruct(&pending9_); Construct(&ready9_, std::move(*p)); }
    }
    if ((state_ & 1024) == 0) {
      auto r = pending10_();
      if (auto* p = r.get_ready()) { state_ |= 1024; Destruct(&pending10_); Construct(&ready10_, std::move(*p)); }
    }
    if ((state_ & 2048) == 0) {
      auto r = pending11_();
      if (auto* p = r.get_ready()) { state_ |= 2048; Destruct(&pending11_); Construct(&ready11_, std::move(*p)); }
    }
    if ((state_ & 4096) == 0) {
      auto r = pending12_();
      if (auto* p = r.get_ready()) { state_ |= 4096; Destruct(&pending12_); Construct(&ready12_, std::move(*p)); }
    }
    if ((state_ & 8192) == 0) {
      auto r = pending13_();
      if (auto* p = r.get_ready()) { state_ |= 8192; Destruct(&pending13_); Construct(&ready13_, std::move(*p)); }
    }
    if ((state_ & 16384) == 0) {
      auto r = pending14_();
      if (auto* p = r.get_ready()) { state_ |= 16384; Destruct(&pending14_); Construct(&ready14_, std::move(*p)); }
    }
    if ((state_ & 32768) == 0) {
      auto r = pending15_();
      if (auto* p = r.get_ready()) { state_ |= 32768; Destruct(&pending15_); Construct(&ready15_, std::move(*p)); }
    }
    if ((state_ & 65536) == 0) {
      auto r = pending16_();
      if (auto* p = r.get_ready()) { state_ |= 65536; Destruct(&pending16_); Construct(&ready16_, std::move(*p)); }
    }
    if ((state_ & 131072) == 0) {
      auto r = pending17_();
      if (auto* p = r.get_ready()) { state_ |= 131072; Destruct(&pending17_); Construct(&ready17_, std::move(*p)); }
    }
    if ((state_ & 262144) == 0) {
      auto r = pending18_();
      if (auto* p = r.get_ready()) { state_ |= 262144; Destruct(&pending18_); Construct(&ready18_, std::move(*p)); }
    }
    if ((state_ & 524288) == 0) {
      auto r = pending19_();
      if (auto* p = r.get_ready()) { state_ |= 524288; Destruct(&pending19_); Construct(&ready19_, std::move(*p)); }
    }
    if ((state_ & 1048576) == 0) {
      auto r = pending20_();
      if (auto* p = r.get_ready()) { state_ |= 1048576; Destruct(&pending20_); Construct(&ready20_, std::move(*p)); }
    }
    if ((state_ & 2097152) == 0) {
      auto r = pending21_();
      if (auto* p = r.get_ready()) { state_ |= 2097152; Destruct(&pending21_); Construct(&ready21_, std::move(*p)); }
    }
    if ((state_ & 4194304) == 0) {
      auto r = pending22_();
      if (auto* p = r.get_ready()) { state_ |= 4194304; Destruct(&pending22_); Construct(&ready22_, std::move(*p)); }
    }
    if ((state_ & 8388608) == 0) {
      auto r = pending23_();
      if (auto* p = r.get_ready()) { state_ |= 8388608; Destruct(&pending23_); Construct(&ready23_, std::move(*p)); }
    }
    if ((state_ & 16777216) == 0) {
      auto r = pending24_();
      if (auto* p = r.get_ready()) { state_ |= 16777216; Destruct(&pending24_); Construct(&ready24_, std::move(*p)); }
    }
    if ((state_ & 33554432) == 0) {
      auto r = pending25_();
      if (auto* p = r.get_ready()) { state_ |= 33554432; Destruct(&pending25_); Construct(&ready25_, std::move(*p)); }
    }
    if (state_ != 67108863) return kPending;
    return ready(std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16, R17, R18, R19, R20, R21, R22, R23, R24, R25>(std::move(ready0_), std::move(ready1_), std::move(ready2_), std::move(ready3_), std::move(ready4_), std::move(ready5_), std::move(ready6_), std::move(ready7_), std::move(ready8_), std::move(ready9_), std::move(ready10_), std::move(ready11_), std::move(ready12_), std::move(ready13_), std::move(ready14_), std::move(ready15_), std::move(ready16_), std::move(ready17_), std::move(ready18_), std::move(ready19_), std::move(ready20_), std::move(ready21_), std::move(ready22_), std::move(ready23_), std::move(ready24_), std::move(ready25_)));
  }
};
template <typename F0, typename F1, typename F2, typename F3, typename F4, typename F5, typename F6, typename F7, typename F8, typename F9, typename F10, typename F11, typename F12, typename F13, typename F14, typename F15, typename F16, typename F17, typename F18, typename F19, typename F20, typename F21, typename F22, typename F23, typename F24, typename F25, typename F26> class Join<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24, F25, F26> {
 private:
  [[no_unique_address]] uint32_t state_ = 0;
  using R0 = typename decltype(std::declval<F0>()())::Type;
  union { [[no_unique_address]] F0 pending0_; [[no_unique_address]] R0 ready0_; };
  using R1 = typename decltype(std::declval<F1>()())::Type;
  union { [[no_unique_address]] F1 pending1_; [[no_unique_address]] R1 ready1_; };
  using R2 = typename decltype(std::declval<F2>()())::Type;
  union { [[no_unique_address]] F2 pending2_; [[no_unique_address]] R2 ready2_; };
  using R3 = typename decltype(std::declval<F3>()())::Type;
  union { [[no_unique_address]] F3 pending3_; [[no_unique_address]] R3 ready3_; };
  using R4 = typename decltype(std::declval<F4>()())::Type;
  union { [[no_unique_address]] F4 pending4_; [[no_unique_address]] R4 ready4_; };
  using R5 = typename decltype(std::declval<F5>()())::Type;
  union { [[no_unique_address]] F5 pending5_; [[no_unique_address]] R5 ready5_; };
  using R6 = typename decltype(std::declval<F6>()())::Type;
  union { [[no_unique_address]] F6 pending6_; [[no_unique_address]] R6 ready6_; };
  using R7 = typename decltype(std::declval<F7>()())::Type;
  union { [[no_unique_address]] F7 pending7_; [[no_unique_address]] R7 ready7_; };
  using R8 = typename decltype(std::declval<F8>()())::Type;
  union { [[no_unique_address]] F8 pending8_; [[no_unique_address]] R8 ready8_; };
  using R9 = typename decltype(std::declval<F9>()())::Type;
  union { [[no_unique_address]] F9 pending9_; [[no_unique_address]] R9 ready9_; };
  using R10 = typename decltype(std::declval<F10>()())::Type;
  union { [[no_unique_address]] F10 pending10_; [[no_unique_address]] R10 ready10_; };
  using R11 = typename decltype(std::declval<F11>()())::Type;
  union { [[no_unique_address]] F11 pending11_; [[no_unique_address]] R11 ready11_; };
  using R12 = typename decltype(std::declval<F12>()())::Type;
  union { [[no_unique_address]] F12 pending12_; [[no_unique_address]] R12 ready12_; };
  using R13 = typename decltype(std::declval<F13>()())::Type;
  union { [[no_unique_address]] F13 pending13_; [[no_unique_address]] R13 ready13_; };
  using R14 = typename decltype(std::declval<F14>()())::Type;
  union { [[no_unique_address]] F14 pending14_; [[no_unique_address]] R14 ready14_; };
  using R15 = typename decltype(std::declval<F15>()())::Type;
  union { [[no_unique_address]] F15 pending15_; [[no_unique_address]] R15 ready15_; };
  using R16 = typename decltype(std::declval<F16>()())::Type;
  union { [[no_unique_address]] F16 pending16_; [[no_unique_address]] R16 ready16_; };
  using R17 = typename decltype(std::declval<F17>()())::Type;
  union { [[no_unique_address]] F17 pending17_; [[no_unique_address]] R17 ready17_; };
  using R18 = typename decltype(std::declval<F18>()())::Type;
  union { [[no_unique_address]] F18 pending18_; [[no_unique_address]] R18 ready18_; };
  using R19 = typename decltype(std::declval<F19>()())::Type;
  union { [[no_unique_address]] F19 pending19_; [[no_unique_address]] R19 ready19_; };
  using R20 = typename decltype(std::declval<F20>()())::Type;
  union { [[no_unique_address]] F20 pending20_; [[no_unique_address]] R20 ready20_; };
  using R21 = typename decltype(std::declval<F21>()())::Type;
  union { [[no_unique_address]] F21 pending21_; [[no_unique_address]] R21 ready21_; };
  using R22 = typename decltype(std::declval<F22>()())::Type;
  union { [[no_unique_address]] F22 pending22_; [[no_unique_address]] R22 ready22_; };
  using R23 = typename decltype(std::declval<F23>()())::Type;
  union { [[no_unique_address]] F23 pending23_; [[no_unique_address]] R23 ready23_; };
  using R24 = typename decltype(std::declval<F24>()())::Type;
  union { [[no_unique_address]] F24 pending24_; [[no_unique_address]] R24 ready24_; };
  using R25 = typename decltype(std::declval<F25>()())::Type;
  union { [[no_unique_address]] F25 pending25_; [[no_unique_address]] R25 ready25_; };
  using R26 = typename decltype(std::declval<F26>()())::Type;
  union { [[no_unique_address]] F26 pending26_; [[no_unique_address]] R26 ready26_; };
 public:
  Join(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7, F8 f8, F9 f9, F10 f10, F11 f11, F12 f12, F13 f13, F14 f14, F15 f15, F16 f16, F17 f17, F18 f18, F19 f19, F20 f20, F21 f21, F22 f22, F23 f23, F24 f24, F25 f25, F26 f26) : pending0_(std::move(f0)), pending1_(std::move(f1)), pending2_(std::move(f2)), pending3_(std::move(f3)), pending4_(std::move(f4)), pending5_(std::move(f5)), pending6_(std::move(f6)), pending7_(std::move(f7)), pending8_(std::move(f8)), pending9_(std::move(f9)), pending10_(std::move(f10)), pending11_(std::move(f11)), pending12_(std::move(f12)), pending13_(std::move(f13)), pending14_(std::move(f14)), pending15_(std::move(f15)), pending16_(std::move(f16)), pending17_(std::move(f17)), pending18_(std::move(f18)), pending19_(std::move(f19)), pending20_(std::move(f20)), pending21_(std::move(f21)), pending22_(std::move(f22)), pending23_(std::move(f23)), pending24_(std::move(f24)), pending25_(std::move(f25)), pending26_(std::move(f26)) {}
  Join& operator=(const Join&) = delete;
  Join(const Join& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
    Construct(&pending15_, std::move(other.pending15_));
    Construct(&pending16_, std::move(other.pending16_));
    Construct(&pending17_, std::move(other.pending17_));
    Construct(&pending18_, std::move(other.pending18_));
    Construct(&pending19_, std::move(other.pending19_));
    Construct(&pending20_, std::move(other.pending20_));
    Construct(&pending21_, std::move(other.pending21_));
    Construct(&pending22_, std::move(other.pending22_));
    Construct(&pending23_, std::move(other.pending23_));
    Construct(&pending24_, std::move(other.pending24_));
    Construct(&pending25_, std::move(other.pending25_));
    Construct(&pending26_, std::move(other.pending26_));
  }
  Join(Join&& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
    Construct(&pending15_, std::move(other.pending15_));
    Construct(&pending16_, std::move(other.pending16_));
    Construct(&pending17_, std::move(other.pending17_));
    Construct(&pending18_, std::move(other.pending18_));
    Construct(&pending19_, std::move(other.pending19_));
    Construct(&pending20_, std::move(other.pending20_));
    Construct(&pending21_, std::move(other.pending21_));
    Construct(&pending22_, std::move(other.pending22_));
    Construct(&pending23_, std::move(other.pending23_));
    Construct(&pending24_, std::move(other.pending24_));
    Construct(&pending25_, std::move(other.pending25_));
    Construct(&pending26_, std::move(other.pending26_));
  }
  ~Join() {
    if (state_ & 1) Destruct(&ready0_); else Destruct(&pending0_);
    if (state_ & 2) Destruct(&ready1_); else Destruct(&pending1_);
    if (state_ & 4) Destruct(&ready2_); else Destruct(&pending2_);
    if (state_ & 8) Destruct(&ready3_); else Destruct(&pending3_);
    if (state_ & 16) Destruct(&ready4_); else Destruct(&pending4_);
    if (state_ & 32) Destruct(&ready5_); else Destruct(&pending5_);
    if (state_ & 64) Destruct(&ready6_); else Destruct(&pending6_);
    if (state_ & 128) Destruct(&ready7_); else Destruct(&pending7_);
    if (state_ & 256) Destruct(&ready8_); else Destruct(&pending8_);
    if (state_ & 512) Destruct(&ready9_); else Destruct(&pending9_);
    if (state_ & 1024) Destruct(&ready10_); else Destruct(&pending10_);
    if (state_ & 2048) Destruct(&ready11_); else Destruct(&pending11_);
    if (state_ & 4096) Destruct(&ready12_); else Destruct(&pending12_);
    if (state_ & 8192) Destruct(&ready13_); else Destruct(&pending13_);
    if (state_ & 16384) Destruct(&ready14_); else Destruct(&pending14_);
    if (state_ & 32768) Destruct(&ready15_); else Destruct(&pending15_);
    if (state_ & 65536) Destruct(&ready16_); else Destruct(&pending16_);
    if (state_ & 131072) Destruct(&ready17_); else Destruct(&pending17_);
    if (state_ & 262144) Destruct(&ready18_); else Destruct(&pending18_);
    if (state_ & 524288) Destruct(&ready19_); else Destruct(&pending19_);
    if (state_ & 1048576) Destruct(&ready20_); else Destruct(&pending20_);
    if (state_ & 2097152) Destruct(&ready21_); else Destruct(&pending21_);
    if (state_ & 4194304) Destruct(&ready22_); else Destruct(&pending22_);
    if (state_ & 8388608) Destruct(&ready23_); else Destruct(&pending23_);
    if (state_ & 16777216) Destruct(&ready24_); else Destruct(&pending24_);
    if (state_ & 33554432) Destruct(&ready25_); else Destruct(&pending25_);
    if (state_ & 67108864) Destruct(&ready26_); else Destruct(&pending26_);
  }
  Poll<std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16, R17, R18, R19, R20, R21, R22, R23, R24, R25, R26>> operator()() {
    if ((state_ & 1) == 0) {
      auto r = pending0_();
      if (auto* p = r.get_ready()) { state_ |= 1; Destruct(&pending0_); Construct(&ready0_, std::move(*p)); }
    }
    if ((state_ & 2) == 0) {
      auto r = pending1_();
      if (auto* p = r.get_ready()) { state_ |= 2; Destruct(&pending1_); Construct(&ready1_, std::move(*p)); }
    }
    if ((state_ & 4) == 0) {
      auto r = pending2_();
      if (auto* p = r.get_ready()) { state_ |= 4; Destruct(&pending2_); Construct(&ready2_, std::move(*p)); }
    }
    if ((state_ & 8) == 0) {
      auto r = pending3_();
      if (auto* p = r.get_ready()) { state_ |= 8; Destruct(&pending3_); Construct(&ready3_, std::move(*p)); }
    }
    if ((state_ & 16) == 0) {
      auto r = pending4_();
      if (auto* p = r.get_ready()) { state_ |= 16; Destruct(&pending4_); Construct(&ready4_, std::move(*p)); }
    }
    if ((state_ & 32) == 0) {
      auto r = pending5_();
      if (auto* p = r.get_ready()) { state_ |= 32; Destruct(&pending5_); Construct(&ready5_, std::move(*p)); }
    }
    if ((state_ & 64) == 0) {
      auto r = pending6_();
      if (auto* p = r.get_ready()) { state_ |= 64; Destruct(&pending6_); Construct(&ready6_, std::move(*p)); }
    }
    if ((state_ & 128) == 0) {
      auto r = pending7_();
      if (auto* p = r.get_ready()) { state_ |= 128; Destruct(&pending7_); Construct(&ready7_, std::move(*p)); }
    }
    if ((state_ & 256) == 0) {
      auto r = pending8_();
      if (auto* p = r.get_ready()) { state_ |= 256; Destruct(&pending8_); Construct(&ready8_, std::move(*p)); }
    }
    if ((state_ & 512) == 0) {
      auto r = pending9_();
      if (auto* p = r.get_ready()) { state_ |= 512; Destruct(&pending9_); Construct(&ready9_, std::move(*p)); }
    }
    if ((state_ & 1024) == 0) {
      auto r = pending10_();
      if (auto* p = r.get_ready()) { state_ |= 1024; Destruct(&pending10_); Construct(&ready10_, std::move(*p)); }
    }
    if ((state_ & 2048) == 0) {
      auto r = pending11_();
      if (auto* p = r.get_ready()) { state_ |= 2048; Destruct(&pending11_); Construct(&ready11_, std::move(*p)); }
    }
    if ((state_ & 4096) == 0) {
      auto r = pending12_();
      if (auto* p = r.get_ready()) { state_ |= 4096; Destruct(&pending12_); Construct(&ready12_, std::move(*p)); }
    }
    if ((state_ & 8192) == 0) {
      auto r = pending13_();
      if (auto* p = r.get_ready()) { state_ |= 8192; Destruct(&pending13_); Construct(&ready13_, std::move(*p)); }
    }
    if ((state_ & 16384) == 0) {
      auto r = pending14_();
      if (auto* p = r.get_ready()) { state_ |= 16384; Destruct(&pending14_); Construct(&ready14_, std::move(*p)); }
    }
    if ((state_ & 32768) == 0) {
      auto r = pending15_();
      if (auto* p = r.get_ready()) { state_ |= 32768; Destruct(&pending15_); Construct(&ready15_, std::move(*p)); }
    }
    if ((state_ & 65536) == 0) {
      auto r = pending16_();
      if (auto* p = r.get_ready()) { state_ |= 65536; Destruct(&pending16_); Construct(&ready16_, std::move(*p)); }
    }
    if ((state_ & 131072) == 0) {
      auto r = pending17_();
      if (auto* p = r.get_ready()) { state_ |= 131072; Destruct(&pending17_); Construct(&ready17_, std::move(*p)); }
    }
    if ((state_ & 262144) == 0) {
      auto r = pending18_();
      if (auto* p = r.get_ready()) { state_ |= 262144; Destruct(&pending18_); Construct(&ready18_, std::move(*p)); }
    }
    if ((state_ & 524288) == 0) {
      auto r = pending19_();
      if (auto* p = r.get_ready()) { state_ |= 524288; Destruct(&pending19_); Construct(&ready19_, std::move(*p)); }
    }
    if ((state_ & 1048576) == 0) {
      auto r = pending20_();
      if (auto* p = r.get_ready()) { state_ |= 1048576; Destruct(&pending20_); Construct(&ready20_, std::move(*p)); }
    }
    if ((state_ & 2097152) == 0) {
      auto r = pending21_();
      if (auto* p = r.get_ready()) { state_ |= 2097152; Destruct(&pending21_); Construct(&ready21_, std::move(*p)); }
    }
    if ((state_ & 4194304) == 0) {
      auto r = pending22_();
      if (auto* p = r.get_ready()) { state_ |= 4194304; Destruct(&pending22_); Construct(&ready22_, std::move(*p)); }
    }
    if ((state_ & 8388608) == 0) {
      auto r = pending23_();
      if (auto* p = r.get_ready()) { state_ |= 8388608; Destruct(&pending23_); Construct(&ready23_, std::move(*p)); }
    }
    if ((state_ & 16777216) == 0) {
      auto r = pending24_();
      if (auto* p = r.get_ready()) { state_ |= 16777216; Destruct(&pending24_); Construct(&ready24_, std::move(*p)); }
    }
    if ((state_ & 33554432) == 0) {
      auto r = pending25_();
      if (auto* p = r.get_ready()) { state_ |= 33554432; Destruct(&pending25_); Construct(&ready25_, std::move(*p)); }
    }
    if ((state_ & 67108864) == 0) {
      auto r = pending26_();
      if (auto* p = r.get_ready()) { state_ |= 67108864; Destruct(&pending26_); Construct(&ready26_, std::move(*p)); }
    }
    if (state_ != 134217727) return kPending;
    return ready(std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16, R17, R18, R19, R20, R21, R22, R23, R24, R25, R26>(std::move(ready0_), std::move(ready1_), std::move(ready2_), std::move(ready3_), std::move(ready4_), std::move(ready5_), std::move(ready6_), std::move(ready7_), std::move(ready8_), std::move(ready9_), std::move(ready10_), std::move(ready11_), std::move(ready12_), std::move(ready13_), std::move(ready14_), std::move(ready15_), std::move(ready16_), std::move(ready17_), std::move(ready18_), std::move(ready19_), std::move(ready20_), std::move(ready21_), std::move(ready22_), std::move(ready23_), std::move(ready24_), std::move(ready25_), std::move(ready26_)));
  }
};
template <typename F0, typename F1, typename F2, typename F3, typename F4, typename F5, typename F6, typename F7, typename F8, typename F9, typename F10, typename F11, typename F12, typename F13, typename F14, typename F15, typename F16, typename F17, typename F18, typename F19, typename F20, typename F21, typename F22, typename F23, typename F24, typename F25, typename F26, typename F27> class Join<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24, F25, F26, F27> {
 private:
  [[no_unique_address]] uint32_t state_ = 0;
  using R0 = typename decltype(std::declval<F0>()())::Type;
  union { [[no_unique_address]] F0 pending0_; [[no_unique_address]] R0 ready0_; };
  using R1 = typename decltype(std::declval<F1>()())::Type;
  union { [[no_unique_address]] F1 pending1_; [[no_unique_address]] R1 ready1_; };
  using R2 = typename decltype(std::declval<F2>()())::Type;
  union { [[no_unique_address]] F2 pending2_; [[no_unique_address]] R2 ready2_; };
  using R3 = typename decltype(std::declval<F3>()())::Type;
  union { [[no_unique_address]] F3 pending3_; [[no_unique_address]] R3 ready3_; };
  using R4 = typename decltype(std::declval<F4>()())::Type;
  union { [[no_unique_address]] F4 pending4_; [[no_unique_address]] R4 ready4_; };
  using R5 = typename decltype(std::declval<F5>()())::Type;
  union { [[no_unique_address]] F5 pending5_; [[no_unique_address]] R5 ready5_; };
  using R6 = typename decltype(std::declval<F6>()())::Type;
  union { [[no_unique_address]] F6 pending6_; [[no_unique_address]] R6 ready6_; };
  using R7 = typename decltype(std::declval<F7>()())::Type;
  union { [[no_unique_address]] F7 pending7_; [[no_unique_address]] R7 ready7_; };
  using R8 = typename decltype(std::declval<F8>()())::Type;
  union { [[no_unique_address]] F8 pending8_; [[no_unique_address]] R8 ready8_; };
  using R9 = typename decltype(std::declval<F9>()())::Type;
  union { [[no_unique_address]] F9 pending9_; [[no_unique_address]] R9 ready9_; };
  using R10 = typename decltype(std::declval<F10>()())::Type;
  union { [[no_unique_address]] F10 pending10_; [[no_unique_address]] R10 ready10_; };
  using R11 = typename decltype(std::declval<F11>()())::Type;
  union { [[no_unique_address]] F11 pending11_; [[no_unique_address]] R11 ready11_; };
  using R12 = typename decltype(std::declval<F12>()())::Type;
  union { [[no_unique_address]] F12 pending12_; [[no_unique_address]] R12 ready12_; };
  using R13 = typename decltype(std::declval<F13>()())::Type;
  union { [[no_unique_address]] F13 pending13_; [[no_unique_address]] R13 ready13_; };
  using R14 = typename decltype(std::declval<F14>()())::Type;
  union { [[no_unique_address]] F14 pending14_; [[no_unique_address]] R14 ready14_; };
  using R15 = typename decltype(std::declval<F15>()())::Type;
  union { [[no_unique_address]] F15 pending15_; [[no_unique_address]] R15 ready15_; };
  using R16 = typename decltype(std::declval<F16>()())::Type;
  union { [[no_unique_address]] F16 pending16_; [[no_unique_address]] R16 ready16_; };
  using R17 = typename decltype(std::declval<F17>()())::Type;
  union { [[no_unique_address]] F17 pending17_; [[no_unique_address]] R17 ready17_; };
  using R18 = typename decltype(std::declval<F18>()())::Type;
  union { [[no_unique_address]] F18 pending18_; [[no_unique_address]] R18 ready18_; };
  using R19 = typename decltype(std::declval<F19>()())::Type;
  union { [[no_unique_address]] F19 pending19_; [[no_unique_address]] R19 ready19_; };
  using R20 = typename decltype(std::declval<F20>()())::Type;
  union { [[no_unique_address]] F20 pending20_; [[no_unique_address]] R20 ready20_; };
  using R21 = typename decltype(std::declval<F21>()())::Type;
  union { [[no_unique_address]] F21 pending21_; [[no_unique_address]] R21 ready21_; };
  using R22 = typename decltype(std::declval<F22>()())::Type;
  union { [[no_unique_address]] F22 pending22_; [[no_unique_address]] R22 ready22_; };
  using R23 = typename decltype(std::declval<F23>()())::Type;
  union { [[no_unique_address]] F23 pending23_; [[no_unique_address]] R23 ready23_; };
  using R24 = typename decltype(std::declval<F24>()())::Type;
  union { [[no_unique_address]] F24 pending24_; [[no_unique_address]] R24 ready24_; };
  using R25 = typename decltype(std::declval<F25>()())::Type;
  union { [[no_unique_address]] F25 pending25_; [[no_unique_address]] R25 ready25_; };
  using R26 = typename decltype(std::declval<F26>()())::Type;
  union { [[no_unique_address]] F26 pending26_; [[no_unique_address]] R26 ready26_; };
  using R27 = typename decltype(std::declval<F27>()())::Type;
  union { [[no_unique_address]] F27 pending27_; [[no_unique_address]] R27 ready27_; };
 public:
  Join(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7, F8 f8, F9 f9, F10 f10, F11 f11, F12 f12, F13 f13, F14 f14, F15 f15, F16 f16, F17 f17, F18 f18, F19 f19, F20 f20, F21 f21, F22 f22, F23 f23, F24 f24, F25 f25, F26 f26, F27 f27) : pending0_(std::move(f0)), pending1_(std::move(f1)), pending2_(std::move(f2)), pending3_(std::move(f3)), pending4_(std::move(f4)), pending5_(std::move(f5)), pending6_(std::move(f6)), pending7_(std::move(f7)), pending8_(std::move(f8)), pending9_(std::move(f9)), pending10_(std::move(f10)), pending11_(std::move(f11)), pending12_(std::move(f12)), pending13_(std::move(f13)), pending14_(std::move(f14)), pending15_(std::move(f15)), pending16_(std::move(f16)), pending17_(std::move(f17)), pending18_(std::move(f18)), pending19_(std::move(f19)), pending20_(std::move(f20)), pending21_(std::move(f21)), pending22_(std::move(f22)), pending23_(std::move(f23)), pending24_(std::move(f24)), pending25_(std::move(f25)), pending26_(std::move(f26)), pending27_(std::move(f27)) {}
  Join& operator=(const Join&) = delete;
  Join(const Join& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
    Construct(&pending15_, std::move(other.pending15_));
    Construct(&pending16_, std::move(other.pending16_));
    Construct(&pending17_, std::move(other.pending17_));
    Construct(&pending18_, std::move(other.pending18_));
    Construct(&pending19_, std::move(other.pending19_));
    Construct(&pending20_, std::move(other.pending20_));
    Construct(&pending21_, std::move(other.pending21_));
    Construct(&pending22_, std::move(other.pending22_));
    Construct(&pending23_, std::move(other.pending23_));
    Construct(&pending24_, std::move(other.pending24_));
    Construct(&pending25_, std::move(other.pending25_));
    Construct(&pending26_, std::move(other.pending26_));
    Construct(&pending27_, std::move(other.pending27_));
  }
  Join(Join&& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
    Construct(&pending15_, std::move(other.pending15_));
    Construct(&pending16_, std::move(other.pending16_));
    Construct(&pending17_, std::move(other.pending17_));
    Construct(&pending18_, std::move(other.pending18_));
    Construct(&pending19_, std::move(other.pending19_));
    Construct(&pending20_, std::move(other.pending20_));
    Construct(&pending21_, std::move(other.pending21_));
    Construct(&pending22_, std::move(other.pending22_));
    Construct(&pending23_, std::move(other.pending23_));
    Construct(&pending24_, std::move(other.pending24_));
    Construct(&pending25_, std::move(other.pending25_));
    Construct(&pending26_, std::move(other.pending26_));
    Construct(&pending27_, std::move(other.pending27_));
  }
  ~Join() {
    if (state_ & 1) Destruct(&ready0_); else Destruct(&pending0_);
    if (state_ & 2) Destruct(&ready1_); else Destruct(&pending1_);
    if (state_ & 4) Destruct(&ready2_); else Destruct(&pending2_);
    if (state_ & 8) Destruct(&ready3_); else Destruct(&pending3_);
    if (state_ & 16) Destruct(&ready4_); else Destruct(&pending4_);
    if (state_ & 32) Destruct(&ready5_); else Destruct(&pending5_);
    if (state_ & 64) Destruct(&ready6_); else Destruct(&pending6_);
    if (state_ & 128) Destruct(&ready7_); else Destruct(&pending7_);
    if (state_ & 256) Destruct(&ready8_); else Destruct(&pending8_);
    if (state_ & 512) Destruct(&ready9_); else Destruct(&pending9_);
    if (state_ & 1024) Destruct(&ready10_); else Destruct(&pending10_);
    if (state_ & 2048) Destruct(&ready11_); else Destruct(&pending11_);
    if (state_ & 4096) Destruct(&ready12_); else Destruct(&pending12_);
    if (state_ & 8192) Destruct(&ready13_); else Destruct(&pending13_);
    if (state_ & 16384) Destruct(&ready14_); else Destruct(&pending14_);
    if (state_ & 32768) Destruct(&ready15_); else Destruct(&pending15_);
    if (state_ & 65536) Destruct(&ready16_); else Destruct(&pending16_);
    if (state_ & 131072) Destruct(&ready17_); else Destruct(&pending17_);
    if (state_ & 262144) Destruct(&ready18_); else Destruct(&pending18_);
    if (state_ & 524288) Destruct(&ready19_); else Destruct(&pending19_);
    if (state_ & 1048576) Destruct(&ready20_); else Destruct(&pending20_);
    if (state_ & 2097152) Destruct(&ready21_); else Destruct(&pending21_);
    if (state_ & 4194304) Destruct(&ready22_); else Destruct(&pending22_);
    if (state_ & 8388608) Destruct(&ready23_); else Destruct(&pending23_);
    if (state_ & 16777216) Destruct(&ready24_); else Destruct(&pending24_);
    if (state_ & 33554432) Destruct(&ready25_); else Destruct(&pending25_);
    if (state_ & 67108864) Destruct(&ready26_); else Destruct(&pending26_);
    if (state_ & 134217728) Destruct(&ready27_); else Destruct(&pending27_);
  }
  Poll<std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16, R17, R18, R19, R20, R21, R22, R23, R24, R25, R26, R27>> operator()() {
    if ((state_ & 1) == 0) {
      auto r = pending0_();
      if (auto* p = r.get_ready()) { state_ |= 1; Destruct(&pending0_); Construct(&ready0_, std::move(*p)); }
    }
    if ((state_ & 2) == 0) {
      auto r = pending1_();
      if (auto* p = r.get_ready()) { state_ |= 2; Destruct(&pending1_); Construct(&ready1_, std::move(*p)); }
    }
    if ((state_ & 4) == 0) {
      auto r = pending2_();
      if (auto* p = r.get_ready()) { state_ |= 4; Destruct(&pending2_); Construct(&ready2_, std::move(*p)); }
    }
    if ((state_ & 8) == 0) {
      auto r = pending3_();
      if (auto* p = r.get_ready()) { state_ |= 8; Destruct(&pending3_); Construct(&ready3_, std::move(*p)); }
    }
    if ((state_ & 16) == 0) {
      auto r = pending4_();
      if (auto* p = r.get_ready()) { state_ |= 16; Destruct(&pending4_); Construct(&ready4_, std::move(*p)); }
    }
    if ((state_ & 32) == 0) {
      auto r = pending5_();
      if (auto* p = r.get_ready()) { state_ |= 32; Destruct(&pending5_); Construct(&ready5_, std::move(*p)); }
    }
    if ((state_ & 64) == 0) {
      auto r = pending6_();
      if (auto* p = r.get_ready()) { state_ |= 64; Destruct(&pending6_); Construct(&ready6_, std::move(*p)); }
    }
    if ((state_ & 128) == 0) {
      auto r = pending7_();
      if (auto* p = r.get_ready()) { state_ |= 128; Destruct(&pending7_); Construct(&ready7_, std::move(*p)); }
    }
    if ((state_ & 256) == 0) {
      auto r = pending8_();
      if (auto* p = r.get_ready()) { state_ |= 256; Destruct(&pending8_); Construct(&ready8_, std::move(*p)); }
    }
    if ((state_ & 512) == 0) {
      auto r = pending9_();
      if (auto* p = r.get_ready()) { state_ |= 512; Destruct(&pending9_); Construct(&ready9_, std::move(*p)); }
    }
    if ((state_ & 1024) == 0) {
      auto r = pending10_();
      if (auto* p = r.get_ready()) { state_ |= 1024; Destruct(&pending10_); Construct(&ready10_, std::move(*p)); }
    }
    if ((state_ & 2048) == 0) {
      auto r = pending11_();
      if (auto* p = r.get_ready()) { state_ |= 2048; Destruct(&pending11_); Construct(&ready11_, std::move(*p)); }
    }
    if ((state_ & 4096) == 0) {
      auto r = pending12_();
      if (auto* p = r.get_ready()) { state_ |= 4096; Destruct(&pending12_); Construct(&ready12_, std::move(*p)); }
    }
    if ((state_ & 8192) == 0) {
      auto r = pending13_();
      if (auto* p = r.get_ready()) { state_ |= 8192; Destruct(&pending13_); Construct(&ready13_, std::move(*p)); }
    }
    if ((state_ & 16384) == 0) {
      auto r = pending14_();
      if (auto* p = r.get_ready()) { state_ |= 16384; Destruct(&pending14_); Construct(&ready14_, std::move(*p)); }
    }
    if ((state_ & 32768) == 0) {
      auto r = pending15_();
      if (auto* p = r.get_ready()) { state_ |= 32768; Destruct(&pending15_); Construct(&ready15_, std::move(*p)); }
    }
    if ((state_ & 65536) == 0) {
      auto r = pending16_();
      if (auto* p = r.get_ready()) { state_ |= 65536; Destruct(&pending16_); Construct(&ready16_, std::move(*p)); }
    }
    if ((state_ & 131072) == 0) {
      auto r = pending17_();
      if (auto* p = r.get_ready()) { state_ |= 131072; Destruct(&pending17_); Construct(&ready17_, std::move(*p)); }
    }
    if ((state_ & 262144) == 0) {
      auto r = pending18_();
      if (auto* p = r.get_ready()) { state_ |= 262144; Destruct(&pending18_); Construct(&ready18_, std::move(*p)); }
    }
    if ((state_ & 524288) == 0) {
      auto r = pending19_();
      if (auto* p = r.get_ready()) { state_ |= 524288; Destruct(&pending19_); Construct(&ready19_, std::move(*p)); }
    }
    if ((state_ & 1048576) == 0) {
      auto r = pending20_();
      if (auto* p = r.get_ready()) { state_ |= 1048576; Destruct(&pending20_); Construct(&ready20_, std::move(*p)); }
    }
    if ((state_ & 2097152) == 0) {
      auto r = pending21_();
      if (auto* p = r.get_ready()) { state_ |= 2097152; Destruct(&pending21_); Construct(&ready21_, std::move(*p)); }
    }
    if ((state_ & 4194304) == 0) {
      auto r = pending22_();
      if (auto* p = r.get_ready()) { state_ |= 4194304; Destruct(&pending22_); Construct(&ready22_, std::move(*p)); }
    }
    if ((state_ & 8388608) == 0) {
      auto r = pending23_();
      if (auto* p = r.get_ready()) { state_ |= 8388608; Destruct(&pending23_); Construct(&ready23_, std::move(*p)); }
    }
    if ((state_ & 16777216) == 0) {
      auto r = pending24_();
      if (auto* p = r.get_ready()) { state_ |= 16777216; Destruct(&pending24_); Construct(&ready24_, std::move(*p)); }
    }
    if ((state_ & 33554432) == 0) {
      auto r = pending25_();
      if (auto* p = r.get_ready()) { state_ |= 33554432; Destruct(&pending25_); Construct(&ready25_, std::move(*p)); }
    }
    if ((state_ & 67108864) == 0) {
      auto r = pending26_();
      if (auto* p = r.get_ready()) { state_ |= 67108864; Destruct(&pending26_); Construct(&ready26_, std::move(*p)); }
    }
    if ((state_ & 134217728) == 0) {
      auto r = pending27_();
      if (auto* p = r.get_ready()) { state_ |= 134217728; Destruct(&pending27_); Construct(&ready27_, std::move(*p)); }
    }
    if (state_ != 268435455) return kPending;
    return ready(std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16, R17, R18, R19, R20, R21, R22, R23, R24, R25, R26, R27>(std::move(ready0_), std::move(ready1_), std::move(ready2_), std::move(ready3_), std::move(ready4_), std::move(ready5_), std::move(ready6_), std::move(ready7_), std::move(ready8_), std::move(ready9_), std::move(ready10_), std::move(ready11_), std::move(ready12_), std::move(ready13_), std::move(ready14_), std::move(ready15_), std::move(ready16_), std::move(ready17_), std::move(ready18_), std::move(ready19_), std::move(ready20_), std::move(ready21_), std::move(ready22_), std::move(ready23_), std::move(ready24_), std::move(ready25_), std::move(ready26_), std::move(ready27_)));
  }
};
template <typename F0, typename F1, typename F2, typename F3, typename F4, typename F5, typename F6, typename F7, typename F8, typename F9, typename F10, typename F11, typename F12, typename F13, typename F14, typename F15, typename F16, typename F17, typename F18, typename F19, typename F20, typename F21, typename F22, typename F23, typename F24, typename F25, typename F26, typename F27, typename F28> class Join<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24, F25, F26, F27, F28> {
 private:
  [[no_unique_address]] uint32_t state_ = 0;
  using R0 = typename decltype(std::declval<F0>()())::Type;
  union { [[no_unique_address]] F0 pending0_; [[no_unique_address]] R0 ready0_; };
  using R1 = typename decltype(std::declval<F1>()())::Type;
  union { [[no_unique_address]] F1 pending1_; [[no_unique_address]] R1 ready1_; };
  using R2 = typename decltype(std::declval<F2>()())::Type;
  union { [[no_unique_address]] F2 pending2_; [[no_unique_address]] R2 ready2_; };
  using R3 = typename decltype(std::declval<F3>()())::Type;
  union { [[no_unique_address]] F3 pending3_; [[no_unique_address]] R3 ready3_; };
  using R4 = typename decltype(std::declval<F4>()())::Type;
  union { [[no_unique_address]] F4 pending4_; [[no_unique_address]] R4 ready4_; };
  using R5 = typename decltype(std::declval<F5>()())::Type;
  union { [[no_unique_address]] F5 pending5_; [[no_unique_address]] R5 ready5_; };
  using R6 = typename decltype(std::declval<F6>()())::Type;
  union { [[no_unique_address]] F6 pending6_; [[no_unique_address]] R6 ready6_; };
  using R7 = typename decltype(std::declval<F7>()())::Type;
  union { [[no_unique_address]] F7 pending7_; [[no_unique_address]] R7 ready7_; };
  using R8 = typename decltype(std::declval<F8>()())::Type;
  union { [[no_unique_address]] F8 pending8_; [[no_unique_address]] R8 ready8_; };
  using R9 = typename decltype(std::declval<F9>()())::Type;
  union { [[no_unique_address]] F9 pending9_; [[no_unique_address]] R9 ready9_; };
  using R10 = typename decltype(std::declval<F10>()())::Type;
  union { [[no_unique_address]] F10 pending10_; [[no_unique_address]] R10 ready10_; };
  using R11 = typename decltype(std::declval<F11>()())::Type;
  union { [[no_unique_address]] F11 pending11_; [[no_unique_address]] R11 ready11_; };
  using R12 = typename decltype(std::declval<F12>()())::Type;
  union { [[no_unique_address]] F12 pending12_; [[no_unique_address]] R12 ready12_; };
  using R13 = typename decltype(std::declval<F13>()())::Type;
  union { [[no_unique_address]] F13 pending13_; [[no_unique_address]] R13 ready13_; };
  using R14 = typename decltype(std::declval<F14>()())::Type;
  union { [[no_unique_address]] F14 pending14_; [[no_unique_address]] R14 ready14_; };
  using R15 = typename decltype(std::declval<F15>()())::Type;
  union { [[no_unique_address]] F15 pending15_; [[no_unique_address]] R15 ready15_; };
  using R16 = typename decltype(std::declval<F16>()())::Type;
  union { [[no_unique_address]] F16 pending16_; [[no_unique_address]] R16 ready16_; };
  using R17 = typename decltype(std::declval<F17>()())::Type;
  union { [[no_unique_address]] F17 pending17_; [[no_unique_address]] R17 ready17_; };
  using R18 = typename decltype(std::declval<F18>()())::Type;
  union { [[no_unique_address]] F18 pending18_; [[no_unique_address]] R18 ready18_; };
  using R19 = typename decltype(std::declval<F19>()())::Type;
  union { [[no_unique_address]] F19 pending19_; [[no_unique_address]] R19 ready19_; };
  using R20 = typename decltype(std::declval<F20>()())::Type;
  union { [[no_unique_address]] F20 pending20_; [[no_unique_address]] R20 ready20_; };
  using R21 = typename decltype(std::declval<F21>()())::Type;
  union { [[no_unique_address]] F21 pending21_; [[no_unique_address]] R21 ready21_; };
  using R22 = typename decltype(std::declval<F22>()())::Type;
  union { [[no_unique_address]] F22 pending22_; [[no_unique_address]] R22 ready22_; };
  using R23 = typename decltype(std::declval<F23>()())::Type;
  union { [[no_unique_address]] F23 pending23_; [[no_unique_address]] R23 ready23_; };
  using R24 = typename decltype(std::declval<F24>()())::Type;
  union { [[no_unique_address]] F24 pending24_; [[no_unique_address]] R24 ready24_; };
  using R25 = typename decltype(std::declval<F25>()())::Type;
  union { [[no_unique_address]] F25 pending25_; [[no_unique_address]] R25 ready25_; };
  using R26 = typename decltype(std::declval<F26>()())::Type;
  union { [[no_unique_address]] F26 pending26_; [[no_unique_address]] R26 ready26_; };
  using R27 = typename decltype(std::declval<F27>()())::Type;
  union { [[no_unique_address]] F27 pending27_; [[no_unique_address]] R27 ready27_; };
  using R28 = typename decltype(std::declval<F28>()())::Type;
  union { [[no_unique_address]] F28 pending28_; [[no_unique_address]] R28 ready28_; };
 public:
  Join(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7, F8 f8, F9 f9, F10 f10, F11 f11, F12 f12, F13 f13, F14 f14, F15 f15, F16 f16, F17 f17, F18 f18, F19 f19, F20 f20, F21 f21, F22 f22, F23 f23, F24 f24, F25 f25, F26 f26, F27 f27, F28 f28) : pending0_(std::move(f0)), pending1_(std::move(f1)), pending2_(std::move(f2)), pending3_(std::move(f3)), pending4_(std::move(f4)), pending5_(std::move(f5)), pending6_(std::move(f6)), pending7_(std::move(f7)), pending8_(std::move(f8)), pending9_(std::move(f9)), pending10_(std::move(f10)), pending11_(std::move(f11)), pending12_(std::move(f12)), pending13_(std::move(f13)), pending14_(std::move(f14)), pending15_(std::move(f15)), pending16_(std::move(f16)), pending17_(std::move(f17)), pending18_(std::move(f18)), pending19_(std::move(f19)), pending20_(std::move(f20)), pending21_(std::move(f21)), pending22_(std::move(f22)), pending23_(std::move(f23)), pending24_(std::move(f24)), pending25_(std::move(f25)), pending26_(std::move(f26)), pending27_(std::move(f27)), pending28_(std::move(f28)) {}
  Join& operator=(const Join&) = delete;
  Join(const Join& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
    Construct(&pending15_, std::move(other.pending15_));
    Construct(&pending16_, std::move(other.pending16_));
    Construct(&pending17_, std::move(other.pending17_));
    Construct(&pending18_, std::move(other.pending18_));
    Construct(&pending19_, std::move(other.pending19_));
    Construct(&pending20_, std::move(other.pending20_));
    Construct(&pending21_, std::move(other.pending21_));
    Construct(&pending22_, std::move(other.pending22_));
    Construct(&pending23_, std::move(other.pending23_));
    Construct(&pending24_, std::move(other.pending24_));
    Construct(&pending25_, std::move(other.pending25_));
    Construct(&pending26_, std::move(other.pending26_));
    Construct(&pending27_, std::move(other.pending27_));
    Construct(&pending28_, std::move(other.pending28_));
  }
  Join(Join&& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
    Construct(&pending15_, std::move(other.pending15_));
    Construct(&pending16_, std::move(other.pending16_));
    Construct(&pending17_, std::move(other.pending17_));
    Construct(&pending18_, std::move(other.pending18_));
    Construct(&pending19_, std::move(other.pending19_));
    Construct(&pending20_, std::move(other.pending20_));
    Construct(&pending21_, std::move(other.pending21_));
    Construct(&pending22_, std::move(other.pending22_));
    Construct(&pending23_, std::move(other.pending23_));
    Construct(&pending24_, std::move(other.pending24_));
    Construct(&pending25_, std::move(other.pending25_));
    Construct(&pending26_, std::move(other.pending26_));
    Construct(&pending27_, std::move(other.pending27_));
    Construct(&pending28_, std::move(other.pending28_));
  }
  ~Join() {
    if (state_ & 1) Destruct(&ready0_); else Destruct(&pending0_);
    if (state_ & 2) Destruct(&ready1_); else Destruct(&pending1_);
    if (state_ & 4) Destruct(&ready2_); else Destruct(&pending2_);
    if (state_ & 8) Destruct(&ready3_); else Destruct(&pending3_);
    if (state_ & 16) Destruct(&ready4_); else Destruct(&pending4_);
    if (state_ & 32) Destruct(&ready5_); else Destruct(&pending5_);
    if (state_ & 64) Destruct(&ready6_); else Destruct(&pending6_);
    if (state_ & 128) Destruct(&ready7_); else Destruct(&pending7_);
    if (state_ & 256) Destruct(&ready8_); else Destruct(&pending8_);
    if (state_ & 512) Destruct(&ready9_); else Destruct(&pending9_);
    if (state_ & 1024) Destruct(&ready10_); else Destruct(&pending10_);
    if (state_ & 2048) Destruct(&ready11_); else Destruct(&pending11_);
    if (state_ & 4096) Destruct(&ready12_); else Destruct(&pending12_);
    if (state_ & 8192) Destruct(&ready13_); else Destruct(&pending13_);
    if (state_ & 16384) Destruct(&ready14_); else Destruct(&pending14_);
    if (state_ & 32768) Destruct(&ready15_); else Destruct(&pending15_);
    if (state_ & 65536) Destruct(&ready16_); else Destruct(&pending16_);
    if (state_ & 131072) Destruct(&ready17_); else Destruct(&pending17_);
    if (state_ & 262144) Destruct(&ready18_); else Destruct(&pending18_);
    if (state_ & 524288) Destruct(&ready19_); else Destruct(&pending19_);
    if (state_ & 1048576) Destruct(&ready20_); else Destruct(&pending20_);
    if (state_ & 2097152) Destruct(&ready21_); else Destruct(&pending21_);
    if (state_ & 4194304) Destruct(&ready22_); else Destruct(&pending22_);
    if (state_ & 8388608) Destruct(&ready23_); else Destruct(&pending23_);
    if (state_ & 16777216) Destruct(&ready24_); else Destruct(&pending24_);
    if (state_ & 33554432) Destruct(&ready25_); else Destruct(&pending25_);
    if (state_ & 67108864) Destruct(&ready26_); else Destruct(&pending26_);
    if (state_ & 134217728) Destruct(&ready27_); else Destruct(&pending27_);
    if (state_ & 268435456) Destruct(&ready28_); else Destruct(&pending28_);
  }
  Poll<std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16, R17, R18, R19, R20, R21, R22, R23, R24, R25, R26, R27, R28>> operator()() {
    if ((state_ & 1) == 0) {
      auto r = pending0_();
      if (auto* p = r.get_ready()) { state_ |= 1; Destruct(&pending0_); Construct(&ready0_, std::move(*p)); }
    }
    if ((state_ & 2) == 0) {
      auto r = pending1_();
      if (auto* p = r.get_ready()) { state_ |= 2; Destruct(&pending1_); Construct(&ready1_, std::move(*p)); }
    }
    if ((state_ & 4) == 0) {
      auto r = pending2_();
      if (auto* p = r.get_ready()) { state_ |= 4; Destruct(&pending2_); Construct(&ready2_, std::move(*p)); }
    }
    if ((state_ & 8) == 0) {
      auto r = pending3_();
      if (auto* p = r.get_ready()) { state_ |= 8; Destruct(&pending3_); Construct(&ready3_, std::move(*p)); }
    }
    if ((state_ & 16) == 0) {
      auto r = pending4_();
      if (auto* p = r.get_ready()) { state_ |= 16; Destruct(&pending4_); Construct(&ready4_, std::move(*p)); }
    }
    if ((state_ & 32) == 0) {
      auto r = pending5_();
      if (auto* p = r.get_ready()) { state_ |= 32; Destruct(&pending5_); Construct(&ready5_, std::move(*p)); }
    }
    if ((state_ & 64) == 0) {
      auto r = pending6_();
      if (auto* p = r.get_ready()) { state_ |= 64; Destruct(&pending6_); Construct(&ready6_, std::move(*p)); }
    }
    if ((state_ & 128) == 0) {
      auto r = pending7_();
      if (auto* p = r.get_ready()) { state_ |= 128; Destruct(&pending7_); Construct(&ready7_, std::move(*p)); }
    }
    if ((state_ & 256) == 0) {
      auto r = pending8_();
      if (auto* p = r.get_ready()) { state_ |= 256; Destruct(&pending8_); Construct(&ready8_, std::move(*p)); }
    }
    if ((state_ & 512) == 0) {
      auto r = pending9_();
      if (auto* p = r.get_ready()) { state_ |= 512; Destruct(&pending9_); Construct(&ready9_, std::move(*p)); }
    }
    if ((state_ & 1024) == 0) {
      auto r = pending10_();
      if (auto* p = r.get_ready()) { state_ |= 1024; Destruct(&pending10_); Construct(&ready10_, std::move(*p)); }
    }
    if ((state_ & 2048) == 0) {
      auto r = pending11_();
      if (auto* p = r.get_ready()) { state_ |= 2048; Destruct(&pending11_); Construct(&ready11_, std::move(*p)); }
    }
    if ((state_ & 4096) == 0) {
      auto r = pending12_();
      if (auto* p = r.get_ready()) { state_ |= 4096; Destruct(&pending12_); Construct(&ready12_, std::move(*p)); }
    }
    if ((state_ & 8192) == 0) {
      auto r = pending13_();
      if (auto* p = r.get_ready()) { state_ |= 8192; Destruct(&pending13_); Construct(&ready13_, std::move(*p)); }
    }
    if ((state_ & 16384) == 0) {
      auto r = pending14_();
      if (auto* p = r.get_ready()) { state_ |= 16384; Destruct(&pending14_); Construct(&ready14_, std::move(*p)); }
    }
    if ((state_ & 32768) == 0) {
      auto r = pending15_();
      if (auto* p = r.get_ready()) { state_ |= 32768; Destruct(&pending15_); Construct(&ready15_, std::move(*p)); }
    }
    if ((state_ & 65536) == 0) {
      auto r = pending16_();
      if (auto* p = r.get_ready()) { state_ |= 65536; Destruct(&pending16_); Construct(&ready16_, std::move(*p)); }
    }
    if ((state_ & 131072) == 0) {
      auto r = pending17_();
      if (auto* p = r.get_ready()) { state_ |= 131072; Destruct(&pending17_); Construct(&ready17_, std::move(*p)); }
    }
    if ((state_ & 262144) == 0) {
      auto r = pending18_();
      if (auto* p = r.get_ready()) { state_ |= 262144; Destruct(&pending18_); Construct(&ready18_, std::move(*p)); }
    }
    if ((state_ & 524288) == 0) {
      auto r = pending19_();
      if (auto* p = r.get_ready()) { state_ |= 524288; Destruct(&pending19_); Construct(&ready19_, std::move(*p)); }
    }
    if ((state_ & 1048576) == 0) {
      auto r = pending20_();
      if (auto* p = r.get_ready()) { state_ |= 1048576; Destruct(&pending20_); Construct(&ready20_, std::move(*p)); }
    }
    if ((state_ & 2097152) == 0) {
      auto r = pending21_();
      if (auto* p = r.get_ready()) { state_ |= 2097152; Destruct(&pending21_); Construct(&ready21_, std::move(*p)); }
    }
    if ((state_ & 4194304) == 0) {
      auto r = pending22_();
      if (auto* p = r.get_ready()) { state_ |= 4194304; Destruct(&pending22_); Construct(&ready22_, std::move(*p)); }
    }
    if ((state_ & 8388608) == 0) {
      auto r = pending23_();
      if (auto* p = r.get_ready()) { state_ |= 8388608; Destruct(&pending23_); Construct(&ready23_, std::move(*p)); }
    }
    if ((state_ & 16777216) == 0) {
      auto r = pending24_();
      if (auto* p = r.get_ready()) { state_ |= 16777216; Destruct(&pending24_); Construct(&ready24_, std::move(*p)); }
    }
    if ((state_ & 33554432) == 0) {
      auto r = pending25_();
      if (auto* p = r.get_ready()) { state_ |= 33554432; Destruct(&pending25_); Construct(&ready25_, std::move(*p)); }
    }
    if ((state_ & 67108864) == 0) {
      auto r = pending26_();
      if (auto* p = r.get_ready()) { state_ |= 67108864; Destruct(&pending26_); Construct(&ready26_, std::move(*p)); }
    }
    if ((state_ & 134217728) == 0) {
      auto r = pending27_();
      if (auto* p = r.get_ready()) { state_ |= 134217728; Destruct(&pending27_); Construct(&ready27_, std::move(*p)); }
    }
    if ((state_ & 268435456) == 0) {
      auto r = pending28_();
      if (auto* p = r.get_ready()) { state_ |= 268435456; Destruct(&pending28_); Construct(&ready28_, std::move(*p)); }
    }
    if (state_ != 536870911) return kPending;
    return ready(std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16, R17, R18, R19, R20, R21, R22, R23, R24, R25, R26, R27, R28>(std::move(ready0_), std::move(ready1_), std::move(ready2_), std::move(ready3_), std::move(ready4_), std::move(ready5_), std::move(ready6_), std::move(ready7_), std::move(ready8_), std::move(ready9_), std::move(ready10_), std::move(ready11_), std::move(ready12_), std::move(ready13_), std::move(ready14_), std::move(ready15_), std::move(ready16_), std::move(ready17_), std::move(ready18_), std::move(ready19_), std::move(ready20_), std::move(ready21_), std::move(ready22_), std::move(ready23_), std::move(ready24_), std::move(ready25_), std::move(ready26_), std::move(ready27_), std::move(ready28_)));
  }
};
template <typename F0, typename F1, typename F2, typename F3, typename F4, typename F5, typename F6, typename F7, typename F8, typename F9, typename F10, typename F11, typename F12, typename F13, typename F14, typename F15, typename F16, typename F17, typename F18, typename F19, typename F20, typename F21, typename F22, typename F23, typename F24, typename F25, typename F26, typename F27, typename F28, typename F29> class Join<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24, F25, F26, F27, F28, F29> {
 private:
  [[no_unique_address]] uint32_t state_ = 0;
  using R0 = typename decltype(std::declval<F0>()())::Type;
  union { [[no_unique_address]] F0 pending0_; [[no_unique_address]] R0 ready0_; };
  using R1 = typename decltype(std::declval<F1>()())::Type;
  union { [[no_unique_address]] F1 pending1_; [[no_unique_address]] R1 ready1_; };
  using R2 = typename decltype(std::declval<F2>()())::Type;
  union { [[no_unique_address]] F2 pending2_; [[no_unique_address]] R2 ready2_; };
  using R3 = typename decltype(std::declval<F3>()())::Type;
  union { [[no_unique_address]] F3 pending3_; [[no_unique_address]] R3 ready3_; };
  using R4 = typename decltype(std::declval<F4>()())::Type;
  union { [[no_unique_address]] F4 pending4_; [[no_unique_address]] R4 ready4_; };
  using R5 = typename decltype(std::declval<F5>()())::Type;
  union { [[no_unique_address]] F5 pending5_; [[no_unique_address]] R5 ready5_; };
  using R6 = typename decltype(std::declval<F6>()())::Type;
  union { [[no_unique_address]] F6 pending6_; [[no_unique_address]] R6 ready6_; };
  using R7 = typename decltype(std::declval<F7>()())::Type;
  union { [[no_unique_address]] F7 pending7_; [[no_unique_address]] R7 ready7_; };
  using R8 = typename decltype(std::declval<F8>()())::Type;
  union { [[no_unique_address]] F8 pending8_; [[no_unique_address]] R8 ready8_; };
  using R9 = typename decltype(std::declval<F9>()())::Type;
  union { [[no_unique_address]] F9 pending9_; [[no_unique_address]] R9 ready9_; };
  using R10 = typename decltype(std::declval<F10>()())::Type;
  union { [[no_unique_address]] F10 pending10_; [[no_unique_address]] R10 ready10_; };
  using R11 = typename decltype(std::declval<F11>()())::Type;
  union { [[no_unique_address]] F11 pending11_; [[no_unique_address]] R11 ready11_; };
  using R12 = typename decltype(std::declval<F12>()())::Type;
  union { [[no_unique_address]] F12 pending12_; [[no_unique_address]] R12 ready12_; };
  using R13 = typename decltype(std::declval<F13>()())::Type;
  union { [[no_unique_address]] F13 pending13_; [[no_unique_address]] R13 ready13_; };
  using R14 = typename decltype(std::declval<F14>()())::Type;
  union { [[no_unique_address]] F14 pending14_; [[no_unique_address]] R14 ready14_; };
  using R15 = typename decltype(std::declval<F15>()())::Type;
  union { [[no_unique_address]] F15 pending15_; [[no_unique_address]] R15 ready15_; };
  using R16 = typename decltype(std::declval<F16>()())::Type;
  union { [[no_unique_address]] F16 pending16_; [[no_unique_address]] R16 ready16_; };
  using R17 = typename decltype(std::declval<F17>()())::Type;
  union { [[no_unique_address]] F17 pending17_; [[no_unique_address]] R17 ready17_; };
  using R18 = typename decltype(std::declval<F18>()())::Type;
  union { [[no_unique_address]] F18 pending18_; [[no_unique_address]] R18 ready18_; };
  using R19 = typename decltype(std::declval<F19>()())::Type;
  union { [[no_unique_address]] F19 pending19_; [[no_unique_address]] R19 ready19_; };
  using R20 = typename decltype(std::declval<F20>()())::Type;
  union { [[no_unique_address]] F20 pending20_; [[no_unique_address]] R20 ready20_; };
  using R21 = typename decltype(std::declval<F21>()())::Type;
  union { [[no_unique_address]] F21 pending21_; [[no_unique_address]] R21 ready21_; };
  using R22 = typename decltype(std::declval<F22>()())::Type;
  union { [[no_unique_address]] F22 pending22_; [[no_unique_address]] R22 ready22_; };
  using R23 = typename decltype(std::declval<F23>()())::Type;
  union { [[no_unique_address]] F23 pending23_; [[no_unique_address]] R23 ready23_; };
  using R24 = typename decltype(std::declval<F24>()())::Type;
  union { [[no_unique_address]] F24 pending24_; [[no_unique_address]] R24 ready24_; };
  using R25 = typename decltype(std::declval<F25>()())::Type;
  union { [[no_unique_address]] F25 pending25_; [[no_unique_address]] R25 ready25_; };
  using R26 = typename decltype(std::declval<F26>()())::Type;
  union { [[no_unique_address]] F26 pending26_; [[no_unique_address]] R26 ready26_; };
  using R27 = typename decltype(std::declval<F27>()())::Type;
  union { [[no_unique_address]] F27 pending27_; [[no_unique_address]] R27 ready27_; };
  using R28 = typename decltype(std::declval<F28>()())::Type;
  union { [[no_unique_address]] F28 pending28_; [[no_unique_address]] R28 ready28_; };
  using R29 = typename decltype(std::declval<F29>()())::Type;
  union { [[no_unique_address]] F29 pending29_; [[no_unique_address]] R29 ready29_; };
 public:
  Join(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7, F8 f8, F9 f9, F10 f10, F11 f11, F12 f12, F13 f13, F14 f14, F15 f15, F16 f16, F17 f17, F18 f18, F19 f19, F20 f20, F21 f21, F22 f22, F23 f23, F24 f24, F25 f25, F26 f26, F27 f27, F28 f28, F29 f29) : pending0_(std::move(f0)), pending1_(std::move(f1)), pending2_(std::move(f2)), pending3_(std::move(f3)), pending4_(std::move(f4)), pending5_(std::move(f5)), pending6_(std::move(f6)), pending7_(std::move(f7)), pending8_(std::move(f8)), pending9_(std::move(f9)), pending10_(std::move(f10)), pending11_(std::move(f11)), pending12_(std::move(f12)), pending13_(std::move(f13)), pending14_(std::move(f14)), pending15_(std::move(f15)), pending16_(std::move(f16)), pending17_(std::move(f17)), pending18_(std::move(f18)), pending19_(std::move(f19)), pending20_(std::move(f20)), pending21_(std::move(f21)), pending22_(std::move(f22)), pending23_(std::move(f23)), pending24_(std::move(f24)), pending25_(std::move(f25)), pending26_(std::move(f26)), pending27_(std::move(f27)), pending28_(std::move(f28)), pending29_(std::move(f29)) {}
  Join& operator=(const Join&) = delete;
  Join(const Join& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
    Construct(&pending15_, std::move(other.pending15_));
    Construct(&pending16_, std::move(other.pending16_));
    Construct(&pending17_, std::move(other.pending17_));
    Construct(&pending18_, std::move(other.pending18_));
    Construct(&pending19_, std::move(other.pending19_));
    Construct(&pending20_, std::move(other.pending20_));
    Construct(&pending21_, std::move(other.pending21_));
    Construct(&pending22_, std::move(other.pending22_));
    Construct(&pending23_, std::move(other.pending23_));
    Construct(&pending24_, std::move(other.pending24_));
    Construct(&pending25_, std::move(other.pending25_));
    Construct(&pending26_, std::move(other.pending26_));
    Construct(&pending27_, std::move(other.pending27_));
    Construct(&pending28_, std::move(other.pending28_));
    Construct(&pending29_, std::move(other.pending29_));
  }
  Join(Join&& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
    Construct(&pending15_, std::move(other.pending15_));
    Construct(&pending16_, std::move(other.pending16_));
    Construct(&pending17_, std::move(other.pending17_));
    Construct(&pending18_, std::move(other.pending18_));
    Construct(&pending19_, std::move(other.pending19_));
    Construct(&pending20_, std::move(other.pending20_));
    Construct(&pending21_, std::move(other.pending21_));
    Construct(&pending22_, std::move(other.pending22_));
    Construct(&pending23_, std::move(other.pending23_));
    Construct(&pending24_, std::move(other.pending24_));
    Construct(&pending25_, std::move(other.pending25_));
    Construct(&pending26_, std::move(other.pending26_));
    Construct(&pending27_, std::move(other.pending27_));
    Construct(&pending28_, std::move(other.pending28_));
    Construct(&pending29_, std::move(other.pending29_));
  }
  ~Join() {
    if (state_ & 1) Destruct(&ready0_); else Destruct(&pending0_);
    if (state_ & 2) Destruct(&ready1_); else Destruct(&pending1_);
    if (state_ & 4) Destruct(&ready2_); else Destruct(&pending2_);
    if (state_ & 8) Destruct(&ready3_); else Destruct(&pending3_);
    if (state_ & 16) Destruct(&ready4_); else Destruct(&pending4_);
    if (state_ & 32) Destruct(&ready5_); else Destruct(&pending5_);
    if (state_ & 64) Destruct(&ready6_); else Destruct(&pending6_);
    if (state_ & 128) Destruct(&ready7_); else Destruct(&pending7_);
    if (state_ & 256) Destruct(&ready8_); else Destruct(&pending8_);
    if (state_ & 512) Destruct(&ready9_); else Destruct(&pending9_);
    if (state_ & 1024) Destruct(&ready10_); else Destruct(&pending10_);
    if (state_ & 2048) Destruct(&ready11_); else Destruct(&pending11_);
    if (state_ & 4096) Destruct(&ready12_); else Destruct(&pending12_);
    if (state_ & 8192) Destruct(&ready13_); else Destruct(&pending13_);
    if (state_ & 16384) Destruct(&ready14_); else Destruct(&pending14_);
    if (state_ & 32768) Destruct(&ready15_); else Destruct(&pending15_);
    if (state_ & 65536) Destruct(&ready16_); else Destruct(&pending16_);
    if (state_ & 131072) Destruct(&ready17_); else Destruct(&pending17_);
    if (state_ & 262144) Destruct(&ready18_); else Destruct(&pending18_);
    if (state_ & 524288) Destruct(&ready19_); else Destruct(&pending19_);
    if (state_ & 1048576) Destruct(&ready20_); else Destruct(&pending20_);
    if (state_ & 2097152) Destruct(&ready21_); else Destruct(&pending21_);
    if (state_ & 4194304) Destruct(&ready22_); else Destruct(&pending22_);
    if (state_ & 8388608) Destruct(&ready23_); else Destruct(&pending23_);
    if (state_ & 16777216) Destruct(&ready24_); else Destruct(&pending24_);
    if (state_ & 33554432) Destruct(&ready25_); else Destruct(&pending25_);
    if (state_ & 67108864) Destruct(&ready26_); else Destruct(&pending26_);
    if (state_ & 134217728) Destruct(&ready27_); else Destruct(&pending27_);
    if (state_ & 268435456) Destruct(&ready28_); else Destruct(&pending28_);
    if (state_ & 536870912) Destruct(&ready29_); else Destruct(&pending29_);
  }
  Poll<std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16, R17, R18, R19, R20, R21, R22, R23, R24, R25, R26, R27, R28, R29>> operator()() {
    if ((state_ & 1) == 0) {
      auto r = pending0_();
      if (auto* p = r.get_ready()) { state_ |= 1; Destruct(&pending0_); Construct(&ready0_, std::move(*p)); }
    }
    if ((state_ & 2) == 0) {
      auto r = pending1_();
      if (auto* p = r.get_ready()) { state_ |= 2; Destruct(&pending1_); Construct(&ready1_, std::move(*p)); }
    }
    if ((state_ & 4) == 0) {
      auto r = pending2_();
      if (auto* p = r.get_ready()) { state_ |= 4; Destruct(&pending2_); Construct(&ready2_, std::move(*p)); }
    }
    if ((state_ & 8) == 0) {
      auto r = pending3_();
      if (auto* p = r.get_ready()) { state_ |= 8; Destruct(&pending3_); Construct(&ready3_, std::move(*p)); }
    }
    if ((state_ & 16) == 0) {
      auto r = pending4_();
      if (auto* p = r.get_ready()) { state_ |= 16; Destruct(&pending4_); Construct(&ready4_, std::move(*p)); }
    }
    if ((state_ & 32) == 0) {
      auto r = pending5_();
      if (auto* p = r.get_ready()) { state_ |= 32; Destruct(&pending5_); Construct(&ready5_, std::move(*p)); }
    }
    if ((state_ & 64) == 0) {
      auto r = pending6_();
      if (auto* p = r.get_ready()) { state_ |= 64; Destruct(&pending6_); Construct(&ready6_, std::move(*p)); }
    }
    if ((state_ & 128) == 0) {
      auto r = pending7_();
      if (auto* p = r.get_ready()) { state_ |= 128; Destruct(&pending7_); Construct(&ready7_, std::move(*p)); }
    }
    if ((state_ & 256) == 0) {
      auto r = pending8_();
      if (auto* p = r.get_ready()) { state_ |= 256; Destruct(&pending8_); Construct(&ready8_, std::move(*p)); }
    }
    if ((state_ & 512) == 0) {
      auto r = pending9_();
      if (auto* p = r.get_ready()) { state_ |= 512; Destruct(&pending9_); Construct(&ready9_, std::move(*p)); }
    }
    if ((state_ & 1024) == 0) {
      auto r = pending10_();
      if (auto* p = r.get_ready()) { state_ |= 1024; Destruct(&pending10_); Construct(&ready10_, std::move(*p)); }
    }
    if ((state_ & 2048) == 0) {
      auto r = pending11_();
      if (auto* p = r.get_ready()) { state_ |= 2048; Destruct(&pending11_); Construct(&ready11_, std::move(*p)); }
    }
    if ((state_ & 4096) == 0) {
      auto r = pending12_();
      if (auto* p = r.get_ready()) { state_ |= 4096; Destruct(&pending12_); Construct(&ready12_, std::move(*p)); }
    }
    if ((state_ & 8192) == 0) {
      auto r = pending13_();
      if (auto* p = r.get_ready()) { state_ |= 8192; Destruct(&pending13_); Construct(&ready13_, std::move(*p)); }
    }
    if ((state_ & 16384) == 0) {
      auto r = pending14_();
      if (auto* p = r.get_ready()) { state_ |= 16384; Destruct(&pending14_); Construct(&ready14_, std::move(*p)); }
    }
    if ((state_ & 32768) == 0) {
      auto r = pending15_();
      if (auto* p = r.get_ready()) { state_ |= 32768; Destruct(&pending15_); Construct(&ready15_, std::move(*p)); }
    }
    if ((state_ & 65536) == 0) {
      auto r = pending16_();
      if (auto* p = r.get_ready()) { state_ |= 65536; Destruct(&pending16_); Construct(&ready16_, std::move(*p)); }
    }
    if ((state_ & 131072) == 0) {
      auto r = pending17_();
      if (auto* p = r.get_ready()) { state_ |= 131072; Destruct(&pending17_); Construct(&ready17_, std::move(*p)); }
    }
    if ((state_ & 262144) == 0) {
      auto r = pending18_();
      if (auto* p = r.get_ready()) { state_ |= 262144; Destruct(&pending18_); Construct(&ready18_, std::move(*p)); }
    }
    if ((state_ & 524288) == 0) {
      auto r = pending19_();
      if (auto* p = r.get_ready()) { state_ |= 524288; Destruct(&pending19_); Construct(&ready19_, std::move(*p)); }
    }
    if ((state_ & 1048576) == 0) {
      auto r = pending20_();
      if (auto* p = r.get_ready()) { state_ |= 1048576; Destruct(&pending20_); Construct(&ready20_, std::move(*p)); }
    }
    if ((state_ & 2097152) == 0) {
      auto r = pending21_();
      if (auto* p = r.get_ready()) { state_ |= 2097152; Destruct(&pending21_); Construct(&ready21_, std::move(*p)); }
    }
    if ((state_ & 4194304) == 0) {
      auto r = pending22_();
      if (auto* p = r.get_ready()) { state_ |= 4194304; Destruct(&pending22_); Construct(&ready22_, std::move(*p)); }
    }
    if ((state_ & 8388608) == 0) {
      auto r = pending23_();
      if (auto* p = r.get_ready()) { state_ |= 8388608; Destruct(&pending23_); Construct(&ready23_, std::move(*p)); }
    }
    if ((state_ & 16777216) == 0) {
      auto r = pending24_();
      if (auto* p = r.get_ready()) { state_ |= 16777216; Destruct(&pending24_); Construct(&ready24_, std::move(*p)); }
    }
    if ((state_ & 33554432) == 0) {
      auto r = pending25_();
      if (auto* p = r.get_ready()) { state_ |= 33554432; Destruct(&pending25_); Construct(&ready25_, std::move(*p)); }
    }
    if ((state_ & 67108864) == 0) {
      auto r = pending26_();
      if (auto* p = r.get_ready()) { state_ |= 67108864; Destruct(&pending26_); Construct(&ready26_, std::move(*p)); }
    }
    if ((state_ & 134217728) == 0) {
      auto r = pending27_();
      if (auto* p = r.get_ready()) { state_ |= 134217728; Destruct(&pending27_); Construct(&ready27_, std::move(*p)); }
    }
    if ((state_ & 268435456) == 0) {
      auto r = pending28_();
      if (auto* p = r.get_ready()) { state_ |= 268435456; Destruct(&pending28_); Construct(&ready28_, std::move(*p)); }
    }
    if ((state_ & 536870912) == 0) {
      auto r = pending29_();
      if (auto* p = r.get_ready()) { state_ |= 536870912; Destruct(&pending29_); Construct(&ready29_, std::move(*p)); }
    }
    if (state_ != 1073741823) return kPending;
    return ready(std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16, R17, R18, R19, R20, R21, R22, R23, R24, R25, R26, R27, R28, R29>(std::move(ready0_), std::move(ready1_), std::move(ready2_), std::move(ready3_), std::move(ready4_), std::move(ready5_), std::move(ready6_), std::move(ready7_), std::move(ready8_), std::move(ready9_), std::move(ready10_), std::move(ready11_), std::move(ready12_), std::move(ready13_), std::move(ready14_), std::move(ready15_), std::move(ready16_), std::move(ready17_), std::move(ready18_), std::move(ready19_), std::move(ready20_), std::move(ready21_), std::move(ready22_), std::move(ready23_), std::move(ready24_), std::move(ready25_), std::move(ready26_), std::move(ready27_), std::move(ready28_), std::move(ready29_)));
  }
};
template <typename F0, typename F1, typename F2, typename F3, typename F4, typename F5, typename F6, typename F7, typename F8, typename F9, typename F10, typename F11, typename F12, typename F13, typename F14, typename F15, typename F16, typename F17, typename F18, typename F19, typename F20, typename F21, typename F22, typename F23, typename F24, typename F25, typename F26, typename F27, typename F28, typename F29, typename F30> class Join<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24, F25, F26, F27, F28, F29, F30> {
 private:
  [[no_unique_address]] uint32_t state_ = 0;
  using R0 = typename decltype(std::declval<F0>()())::Type;
  union { [[no_unique_address]] F0 pending0_; [[no_unique_address]] R0 ready0_; };
  using R1 = typename decltype(std::declval<F1>()())::Type;
  union { [[no_unique_address]] F1 pending1_; [[no_unique_address]] R1 ready1_; };
  using R2 = typename decltype(std::declval<F2>()())::Type;
  union { [[no_unique_address]] F2 pending2_; [[no_unique_address]] R2 ready2_; };
  using R3 = typename decltype(std::declval<F3>()())::Type;
  union { [[no_unique_address]] F3 pending3_; [[no_unique_address]] R3 ready3_; };
  using R4 = typename decltype(std::declval<F4>()())::Type;
  union { [[no_unique_address]] F4 pending4_; [[no_unique_address]] R4 ready4_; };
  using R5 = typename decltype(std::declval<F5>()())::Type;
  union { [[no_unique_address]] F5 pending5_; [[no_unique_address]] R5 ready5_; };
  using R6 = typename decltype(std::declval<F6>()())::Type;
  union { [[no_unique_address]] F6 pending6_; [[no_unique_address]] R6 ready6_; };
  using R7 = typename decltype(std::declval<F7>()())::Type;
  union { [[no_unique_address]] F7 pending7_; [[no_unique_address]] R7 ready7_; };
  using R8 = typename decltype(std::declval<F8>()())::Type;
  union { [[no_unique_address]] F8 pending8_; [[no_unique_address]] R8 ready8_; };
  using R9 = typename decltype(std::declval<F9>()())::Type;
  union { [[no_unique_address]] F9 pending9_; [[no_unique_address]] R9 ready9_; };
  using R10 = typename decltype(std::declval<F10>()())::Type;
  union { [[no_unique_address]] F10 pending10_; [[no_unique_address]] R10 ready10_; };
  using R11 = typename decltype(std::declval<F11>()())::Type;
  union { [[no_unique_address]] F11 pending11_; [[no_unique_address]] R11 ready11_; };
  using R12 = typename decltype(std::declval<F12>()())::Type;
  union { [[no_unique_address]] F12 pending12_; [[no_unique_address]] R12 ready12_; };
  using R13 = typename decltype(std::declval<F13>()())::Type;
  union { [[no_unique_address]] F13 pending13_; [[no_unique_address]] R13 ready13_; };
  using R14 = typename decltype(std::declval<F14>()())::Type;
  union { [[no_unique_address]] F14 pending14_; [[no_unique_address]] R14 ready14_; };
  using R15 = typename decltype(std::declval<F15>()())::Type;
  union { [[no_unique_address]] F15 pending15_; [[no_unique_address]] R15 ready15_; };
  using R16 = typename decltype(std::declval<F16>()())::Type;
  union { [[no_unique_address]] F16 pending16_; [[no_unique_address]] R16 ready16_; };
  using R17 = typename decltype(std::declval<F17>()())::Type;
  union { [[no_unique_address]] F17 pending17_; [[no_unique_address]] R17 ready17_; };
  using R18 = typename decltype(std::declval<F18>()())::Type;
  union { [[no_unique_address]] F18 pending18_; [[no_unique_address]] R18 ready18_; };
  using R19 = typename decltype(std::declval<F19>()())::Type;
  union { [[no_unique_address]] F19 pending19_; [[no_unique_address]] R19 ready19_; };
  using R20 = typename decltype(std::declval<F20>()())::Type;
  union { [[no_unique_address]] F20 pending20_; [[no_unique_address]] R20 ready20_; };
  using R21 = typename decltype(std::declval<F21>()())::Type;
  union { [[no_unique_address]] F21 pending21_; [[no_unique_address]] R21 ready21_; };
  using R22 = typename decltype(std::declval<F22>()())::Type;
  union { [[no_unique_address]] F22 pending22_; [[no_unique_address]] R22 ready22_; };
  using R23 = typename decltype(std::declval<F23>()())::Type;
  union { [[no_unique_address]] F23 pending23_; [[no_unique_address]] R23 ready23_; };
  using R24 = typename decltype(std::declval<F24>()())::Type;
  union { [[no_unique_address]] F24 pending24_; [[no_unique_address]] R24 ready24_; };
  using R25 = typename decltype(std::declval<F25>()())::Type;
  union { [[no_unique_address]] F25 pending25_; [[no_unique_address]] R25 ready25_; };
  using R26 = typename decltype(std::declval<F26>()())::Type;
  union { [[no_unique_address]] F26 pending26_; [[no_unique_address]] R26 ready26_; };
  using R27 = typename decltype(std::declval<F27>()())::Type;
  union { [[no_unique_address]] F27 pending27_; [[no_unique_address]] R27 ready27_; };
  using R28 = typename decltype(std::declval<F28>()())::Type;
  union { [[no_unique_address]] F28 pending28_; [[no_unique_address]] R28 ready28_; };
  using R29 = typename decltype(std::declval<F29>()())::Type;
  union { [[no_unique_address]] F29 pending29_; [[no_unique_address]] R29 ready29_; };
  using R30 = typename decltype(std::declval<F30>()())::Type;
  union { [[no_unique_address]] F30 pending30_; [[no_unique_address]] R30 ready30_; };
 public:
  Join(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7, F8 f8, F9 f9, F10 f10, F11 f11, F12 f12, F13 f13, F14 f14, F15 f15, F16 f16, F17 f17, F18 f18, F19 f19, F20 f20, F21 f21, F22 f22, F23 f23, F24 f24, F25 f25, F26 f26, F27 f27, F28 f28, F29 f29, F30 f30) : pending0_(std::move(f0)), pending1_(std::move(f1)), pending2_(std::move(f2)), pending3_(std::move(f3)), pending4_(std::move(f4)), pending5_(std::move(f5)), pending6_(std::move(f6)), pending7_(std::move(f7)), pending8_(std::move(f8)), pending9_(std::move(f9)), pending10_(std::move(f10)), pending11_(std::move(f11)), pending12_(std::move(f12)), pending13_(std::move(f13)), pending14_(std::move(f14)), pending15_(std::move(f15)), pending16_(std::move(f16)), pending17_(std::move(f17)), pending18_(std::move(f18)), pending19_(std::move(f19)), pending20_(std::move(f20)), pending21_(std::move(f21)), pending22_(std::move(f22)), pending23_(std::move(f23)), pending24_(std::move(f24)), pending25_(std::move(f25)), pending26_(std::move(f26)), pending27_(std::move(f27)), pending28_(std::move(f28)), pending29_(std::move(f29)), pending30_(std::move(f30)) {}
  Join& operator=(const Join&) = delete;
  Join(const Join& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
    Construct(&pending15_, std::move(other.pending15_));
    Construct(&pending16_, std::move(other.pending16_));
    Construct(&pending17_, std::move(other.pending17_));
    Construct(&pending18_, std::move(other.pending18_));
    Construct(&pending19_, std::move(other.pending19_));
    Construct(&pending20_, std::move(other.pending20_));
    Construct(&pending21_, std::move(other.pending21_));
    Construct(&pending22_, std::move(other.pending22_));
    Construct(&pending23_, std::move(other.pending23_));
    Construct(&pending24_, std::move(other.pending24_));
    Construct(&pending25_, std::move(other.pending25_));
    Construct(&pending26_, std::move(other.pending26_));
    Construct(&pending27_, std::move(other.pending27_));
    Construct(&pending28_, std::move(other.pending28_));
    Construct(&pending29_, std::move(other.pending29_));
    Construct(&pending30_, std::move(other.pending30_));
  }
  Join(Join&& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
    Construct(&pending15_, std::move(other.pending15_));
    Construct(&pending16_, std::move(other.pending16_));
    Construct(&pending17_, std::move(other.pending17_));
    Construct(&pending18_, std::move(other.pending18_));
    Construct(&pending19_, std::move(other.pending19_));
    Construct(&pending20_, std::move(other.pending20_));
    Construct(&pending21_, std::move(other.pending21_));
    Construct(&pending22_, std::move(other.pending22_));
    Construct(&pending23_, std::move(other.pending23_));
    Construct(&pending24_, std::move(other.pending24_));
    Construct(&pending25_, std::move(other.pending25_));
    Construct(&pending26_, std::move(other.pending26_));
    Construct(&pending27_, std::move(other.pending27_));
    Construct(&pending28_, std::move(other.pending28_));
    Construct(&pending29_, std::move(other.pending29_));
    Construct(&pending30_, std::move(other.pending30_));
  }
  ~Join() {
    if (state_ & 1) Destruct(&ready0_); else Destruct(&pending0_);
    if (state_ & 2) Destruct(&ready1_); else Destruct(&pending1_);
    if (state_ & 4) Destruct(&ready2_); else Destruct(&pending2_);
    if (state_ & 8) Destruct(&ready3_); else Destruct(&pending3_);
    if (state_ & 16) Destruct(&ready4_); else Destruct(&pending4_);
    if (state_ & 32) Destruct(&ready5_); else Destruct(&pending5_);
    if (state_ & 64) Destruct(&ready6_); else Destruct(&pending6_);
    if (state_ & 128) Destruct(&ready7_); else Destruct(&pending7_);
    if (state_ & 256) Destruct(&ready8_); else Destruct(&pending8_);
    if (state_ & 512) Destruct(&ready9_); else Destruct(&pending9_);
    if (state_ & 1024) Destruct(&ready10_); else Destruct(&pending10_);
    if (state_ & 2048) Destruct(&ready11_); else Destruct(&pending11_);
    if (state_ & 4096) Destruct(&ready12_); else Destruct(&pending12_);
    if (state_ & 8192) Destruct(&ready13_); else Destruct(&pending13_);
    if (state_ & 16384) Destruct(&ready14_); else Destruct(&pending14_);
    if (state_ & 32768) Destruct(&ready15_); else Destruct(&pending15_);
    if (state_ & 65536) Destruct(&ready16_); else Destruct(&pending16_);
    if (state_ & 131072) Destruct(&ready17_); else Destruct(&pending17_);
    if (state_ & 262144) Destruct(&ready18_); else Destruct(&pending18_);
    if (state_ & 524288) Destruct(&ready19_); else Destruct(&pending19_);
    if (state_ & 1048576) Destruct(&ready20_); else Destruct(&pending20_);
    if (state_ & 2097152) Destruct(&ready21_); else Destruct(&pending21_);
    if (state_ & 4194304) Destruct(&ready22_); else Destruct(&pending22_);
    if (state_ & 8388608) Destruct(&ready23_); else Destruct(&pending23_);
    if (state_ & 16777216) Destruct(&ready24_); else Destruct(&pending24_);
    if (state_ & 33554432) Destruct(&ready25_); else Destruct(&pending25_);
    if (state_ & 67108864) Destruct(&ready26_); else Destruct(&pending26_);
    if (state_ & 134217728) Destruct(&ready27_); else Destruct(&pending27_);
    if (state_ & 268435456) Destruct(&ready28_); else Destruct(&pending28_);
    if (state_ & 536870912) Destruct(&ready29_); else Destruct(&pending29_);
    if (state_ & 1073741824) Destruct(&ready30_); else Destruct(&pending30_);
  }
  Poll<std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16, R17, R18, R19, R20, R21, R22, R23, R24, R25, R26, R27, R28, R29, R30>> operator()() {
    if ((state_ & 1) == 0) {
      auto r = pending0_();
      if (auto* p = r.get_ready()) { state_ |= 1; Destruct(&pending0_); Construct(&ready0_, std::move(*p)); }
    }
    if ((state_ & 2) == 0) {
      auto r = pending1_();
      if (auto* p = r.get_ready()) { state_ |= 2; Destruct(&pending1_); Construct(&ready1_, std::move(*p)); }
    }
    if ((state_ & 4) == 0) {
      auto r = pending2_();
      if (auto* p = r.get_ready()) { state_ |= 4; Destruct(&pending2_); Construct(&ready2_, std::move(*p)); }
    }
    if ((state_ & 8) == 0) {
      auto r = pending3_();
      if (auto* p = r.get_ready()) { state_ |= 8; Destruct(&pending3_); Construct(&ready3_, std::move(*p)); }
    }
    if ((state_ & 16) == 0) {
      auto r = pending4_();
      if (auto* p = r.get_ready()) { state_ |= 16; Destruct(&pending4_); Construct(&ready4_, std::move(*p)); }
    }
    if ((state_ & 32) == 0) {
      auto r = pending5_();
      if (auto* p = r.get_ready()) { state_ |= 32; Destruct(&pending5_); Construct(&ready5_, std::move(*p)); }
    }
    if ((state_ & 64) == 0) {
      auto r = pending6_();
      if (auto* p = r.get_ready()) { state_ |= 64; Destruct(&pending6_); Construct(&ready6_, std::move(*p)); }
    }
    if ((state_ & 128) == 0) {
      auto r = pending7_();
      if (auto* p = r.get_ready()) { state_ |= 128; Destruct(&pending7_); Construct(&ready7_, std::move(*p)); }
    }
    if ((state_ & 256) == 0) {
      auto r = pending8_();
      if (auto* p = r.get_ready()) { state_ |= 256; Destruct(&pending8_); Construct(&ready8_, std::move(*p)); }
    }
    if ((state_ & 512) == 0) {
      auto r = pending9_();
      if (auto* p = r.get_ready()) { state_ |= 512; Destruct(&pending9_); Construct(&ready9_, std::move(*p)); }
    }
    if ((state_ & 1024) == 0) {
      auto r = pending10_();
      if (auto* p = r.get_ready()) { state_ |= 1024; Destruct(&pending10_); Construct(&ready10_, std::move(*p)); }
    }
    if ((state_ & 2048) == 0) {
      auto r = pending11_();
      if (auto* p = r.get_ready()) { state_ |= 2048; Destruct(&pending11_); Construct(&ready11_, std::move(*p)); }
    }
    if ((state_ & 4096) == 0) {
      auto r = pending12_();
      if (auto* p = r.get_ready()) { state_ |= 4096; Destruct(&pending12_); Construct(&ready12_, std::move(*p)); }
    }
    if ((state_ & 8192) == 0) {
      auto r = pending13_();
      if (auto* p = r.get_ready()) { state_ |= 8192; Destruct(&pending13_); Construct(&ready13_, std::move(*p)); }
    }
    if ((state_ & 16384) == 0) {
      auto r = pending14_();
      if (auto* p = r.get_ready()) { state_ |= 16384; Destruct(&pending14_); Construct(&ready14_, std::move(*p)); }
    }
    if ((state_ & 32768) == 0) {
      auto r = pending15_();
      if (auto* p = r.get_ready()) { state_ |= 32768; Destruct(&pending15_); Construct(&ready15_, std::move(*p)); }
    }
    if ((state_ & 65536) == 0) {
      auto r = pending16_();
      if (auto* p = r.get_ready()) { state_ |= 65536; Destruct(&pending16_); Construct(&ready16_, std::move(*p)); }
    }
    if ((state_ & 131072) == 0) {
      auto r = pending17_();
      if (auto* p = r.get_ready()) { state_ |= 131072; Destruct(&pending17_); Construct(&ready17_, std::move(*p)); }
    }
    if ((state_ & 262144) == 0) {
      auto r = pending18_();
      if (auto* p = r.get_ready()) { state_ |= 262144; Destruct(&pending18_); Construct(&ready18_, std::move(*p)); }
    }
    if ((state_ & 524288) == 0) {
      auto r = pending19_();
      if (auto* p = r.get_ready()) { state_ |= 524288; Destruct(&pending19_); Construct(&ready19_, std::move(*p)); }
    }
    if ((state_ & 1048576) == 0) {
      auto r = pending20_();
      if (auto* p = r.get_ready()) { state_ |= 1048576; Destruct(&pending20_); Construct(&ready20_, std::move(*p)); }
    }
    if ((state_ & 2097152) == 0) {
      auto r = pending21_();
      if (auto* p = r.get_ready()) { state_ |= 2097152; Destruct(&pending21_); Construct(&ready21_, std::move(*p)); }
    }
    if ((state_ & 4194304) == 0) {
      auto r = pending22_();
      if (auto* p = r.get_ready()) { state_ |= 4194304; Destruct(&pending22_); Construct(&ready22_, std::move(*p)); }
    }
    if ((state_ & 8388608) == 0) {
      auto r = pending23_();
      if (auto* p = r.get_ready()) { state_ |= 8388608; Destruct(&pending23_); Construct(&ready23_, std::move(*p)); }
    }
    if ((state_ & 16777216) == 0) {
      auto r = pending24_();
      if (auto* p = r.get_ready()) { state_ |= 16777216; Destruct(&pending24_); Construct(&ready24_, std::move(*p)); }
    }
    if ((state_ & 33554432) == 0) {
      auto r = pending25_();
      if (auto* p = r.get_ready()) { state_ |= 33554432; Destruct(&pending25_); Construct(&ready25_, std::move(*p)); }
    }
    if ((state_ & 67108864) == 0) {
      auto r = pending26_();
      if (auto* p = r.get_ready()) { state_ |= 67108864; Destruct(&pending26_); Construct(&ready26_, std::move(*p)); }
    }
    if ((state_ & 134217728) == 0) {
      auto r = pending27_();
      if (auto* p = r.get_ready()) { state_ |= 134217728; Destruct(&pending27_); Construct(&ready27_, std::move(*p)); }
    }
    if ((state_ & 268435456) == 0) {
      auto r = pending28_();
      if (auto* p = r.get_ready()) { state_ |= 268435456; Destruct(&pending28_); Construct(&ready28_, std::move(*p)); }
    }
    if ((state_ & 536870912) == 0) {
      auto r = pending29_();
      if (auto* p = r.get_ready()) { state_ |= 536870912; Destruct(&pending29_); Construct(&ready29_, std::move(*p)); }
    }
    if ((state_ & 1073741824) == 0) {
      auto r = pending30_();
      if (auto* p = r.get_ready()) { state_ |= 1073741824; Destruct(&pending30_); Construct(&ready30_, std::move(*p)); }
    }
    if (state_ != 2147483647) return kPending;
    return ready(std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16, R17, R18, R19, R20, R21, R22, R23, R24, R25, R26, R27, R28, R29, R30>(std::move(ready0_), std::move(ready1_), std::move(ready2_), std::move(ready3_), std::move(ready4_), std::move(ready5_), std::move(ready6_), std::move(ready7_), std::move(ready8_), std::move(ready9_), std::move(ready10_), std::move(ready11_), std::move(ready12_), std::move(ready13_), std::move(ready14_), std::move(ready15_), std::move(ready16_), std::move(ready17_), std::move(ready18_), std::move(ready19_), std::move(ready20_), std::move(ready21_), std::move(ready22_), std::move(ready23_), std::move(ready24_), std::move(ready25_), std::move(ready26_), std::move(ready27_), std::move(ready28_), std::move(ready29_), std::move(ready30_)));
  }
};
template <typename F0, typename F1, typename F2, typename F3, typename F4, typename F5, typename F6, typename F7, typename F8, typename F9, typename F10, typename F11, typename F12, typename F13, typename F14, typename F15, typename F16, typename F17, typename F18, typename F19, typename F20, typename F21, typename F22, typename F23, typename F24, typename F25, typename F26, typename F27, typename F28, typename F29, typename F30, typename F31> class Join<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24, F25, F26, F27, F28, F29, F30, F31> {
 private:
  [[no_unique_address]] uint32_t state_ = 0;
  using R0 = typename decltype(std::declval<F0>()())::Type;
  union { [[no_unique_address]] F0 pending0_; [[no_unique_address]] R0 ready0_; };
  using R1 = typename decltype(std::declval<F1>()())::Type;
  union { [[no_unique_address]] F1 pending1_; [[no_unique_address]] R1 ready1_; };
  using R2 = typename decltype(std::declval<F2>()())::Type;
  union { [[no_unique_address]] F2 pending2_; [[no_unique_address]] R2 ready2_; };
  using R3 = typename decltype(std::declval<F3>()())::Type;
  union { [[no_unique_address]] F3 pending3_; [[no_unique_address]] R3 ready3_; };
  using R4 = typename decltype(std::declval<F4>()())::Type;
  union { [[no_unique_address]] F4 pending4_; [[no_unique_address]] R4 ready4_; };
  using R5 = typename decltype(std::declval<F5>()())::Type;
  union { [[no_unique_address]] F5 pending5_; [[no_unique_address]] R5 ready5_; };
  using R6 = typename decltype(std::declval<F6>()())::Type;
  union { [[no_unique_address]] F6 pending6_; [[no_unique_address]] R6 ready6_; };
  using R7 = typename decltype(std::declval<F7>()())::Type;
  union { [[no_unique_address]] F7 pending7_; [[no_unique_address]] R7 ready7_; };
  using R8 = typename decltype(std::declval<F8>()())::Type;
  union { [[no_unique_address]] F8 pending8_; [[no_unique_address]] R8 ready8_; };
  using R9 = typename decltype(std::declval<F9>()())::Type;
  union { [[no_unique_address]] F9 pending9_; [[no_unique_address]] R9 ready9_; };
  using R10 = typename decltype(std::declval<F10>()())::Type;
  union { [[no_unique_address]] F10 pending10_; [[no_unique_address]] R10 ready10_; };
  using R11 = typename decltype(std::declval<F11>()())::Type;
  union { [[no_unique_address]] F11 pending11_; [[no_unique_address]] R11 ready11_; };
  using R12 = typename decltype(std::declval<F12>()())::Type;
  union { [[no_unique_address]] F12 pending12_; [[no_unique_address]] R12 ready12_; };
  using R13 = typename decltype(std::declval<F13>()())::Type;
  union { [[no_unique_address]] F13 pending13_; [[no_unique_address]] R13 ready13_; };
  using R14 = typename decltype(std::declval<F14>()())::Type;
  union { [[no_unique_address]] F14 pending14_; [[no_unique_address]] R14 ready14_; };
  using R15 = typename decltype(std::declval<F15>()())::Type;
  union { [[no_unique_address]] F15 pending15_; [[no_unique_address]] R15 ready15_; };
  using R16 = typename decltype(std::declval<F16>()())::Type;
  union { [[no_unique_address]] F16 pending16_; [[no_unique_address]] R16 ready16_; };
  using R17 = typename decltype(std::declval<F17>()())::Type;
  union { [[no_unique_address]] F17 pending17_; [[no_unique_address]] R17 ready17_; };
  using R18 = typename decltype(std::declval<F18>()())::Type;
  union { [[no_unique_address]] F18 pending18_; [[no_unique_address]] R18 ready18_; };
  using R19 = typename decltype(std::declval<F19>()())::Type;
  union { [[no_unique_address]] F19 pending19_; [[no_unique_address]] R19 ready19_; };
  using R20 = typename decltype(std::declval<F20>()())::Type;
  union { [[no_unique_address]] F20 pending20_; [[no_unique_address]] R20 ready20_; };
  using R21 = typename decltype(std::declval<F21>()())::Type;
  union { [[no_unique_address]] F21 pending21_; [[no_unique_address]] R21 ready21_; };
  using R22 = typename decltype(std::declval<F22>()())::Type;
  union { [[no_unique_address]] F22 pending22_; [[no_unique_address]] R22 ready22_; };
  using R23 = typename decltype(std::declval<F23>()())::Type;
  union { [[no_unique_address]] F23 pending23_; [[no_unique_address]] R23 ready23_; };
  using R24 = typename decltype(std::declval<F24>()())::Type;
  union { [[no_unique_address]] F24 pending24_; [[no_unique_address]] R24 ready24_; };
  using R25 = typename decltype(std::declval<F25>()())::Type;
  union { [[no_unique_address]] F25 pending25_; [[no_unique_address]] R25 ready25_; };
  using R26 = typename decltype(std::declval<F26>()())::Type;
  union { [[no_unique_address]] F26 pending26_; [[no_unique_address]] R26 ready26_; };
  using R27 = typename decltype(std::declval<F27>()())::Type;
  union { [[no_unique_address]] F27 pending27_; [[no_unique_address]] R27 ready27_; };
  using R28 = typename decltype(std::declval<F28>()())::Type;
  union { [[no_unique_address]] F28 pending28_; [[no_unique_address]] R28 ready28_; };
  using R29 = typename decltype(std::declval<F29>()())::Type;
  union { [[no_unique_address]] F29 pending29_; [[no_unique_address]] R29 ready29_; };
  using R30 = typename decltype(std::declval<F30>()())::Type;
  union { [[no_unique_address]] F30 pending30_; [[no_unique_address]] R30 ready30_; };
  using R31 = typename decltype(std::declval<F31>()())::Type;
  union { [[no_unique_address]] F31 pending31_; [[no_unique_address]] R31 ready31_; };
 public:
  Join(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7, F8 f8, F9 f9, F10 f10, F11 f11, F12 f12, F13 f13, F14 f14, F15 f15, F16 f16, F17 f17, F18 f18, F19 f19, F20 f20, F21 f21, F22 f22, F23 f23, F24 f24, F25 f25, F26 f26, F27 f27, F28 f28, F29 f29, F30 f30, F31 f31) : pending0_(std::move(f0)), pending1_(std::move(f1)), pending2_(std::move(f2)), pending3_(std::move(f3)), pending4_(std::move(f4)), pending5_(std::move(f5)), pending6_(std::move(f6)), pending7_(std::move(f7)), pending8_(std::move(f8)), pending9_(std::move(f9)), pending10_(std::move(f10)), pending11_(std::move(f11)), pending12_(std::move(f12)), pending13_(std::move(f13)), pending14_(std::move(f14)), pending15_(std::move(f15)), pending16_(std::move(f16)), pending17_(std::move(f17)), pending18_(std::move(f18)), pending19_(std::move(f19)), pending20_(std::move(f20)), pending21_(std::move(f21)), pending22_(std::move(f22)), pending23_(std::move(f23)), pending24_(std::move(f24)), pending25_(std::move(f25)), pending26_(std::move(f26)), pending27_(std::move(f27)), pending28_(std::move(f28)), pending29_(std::move(f29)), pending30_(std::move(f30)), pending31_(std::move(f31)) {}
  Join& operator=(const Join&) = delete;
  Join(const Join& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
    Construct(&pending15_, std::move(other.pending15_));
    Construct(&pending16_, std::move(other.pending16_));
    Construct(&pending17_, std::move(other.pending17_));
    Construct(&pending18_, std::move(other.pending18_));
    Construct(&pending19_, std::move(other.pending19_));
    Construct(&pending20_, std::move(other.pending20_));
    Construct(&pending21_, std::move(other.pending21_));
    Construct(&pending22_, std::move(other.pending22_));
    Construct(&pending23_, std::move(other.pending23_));
    Construct(&pending24_, std::move(other.pending24_));
    Construct(&pending25_, std::move(other.pending25_));
    Construct(&pending26_, std::move(other.pending26_));
    Construct(&pending27_, std::move(other.pending27_));
    Construct(&pending28_, std::move(other.pending28_));
    Construct(&pending29_, std::move(other.pending29_));
    Construct(&pending30_, std::move(other.pending30_));
    Construct(&pending31_, std::move(other.pending31_));
  }
  Join(Join&& other) {
    assert(other.state_ == 0);
    Construct(&pending0_, std::move(other.pending0_));
    Construct(&pending1_, std::move(other.pending1_));
    Construct(&pending2_, std::move(other.pending2_));
    Construct(&pending3_, std::move(other.pending3_));
    Construct(&pending4_, std::move(other.pending4_));
    Construct(&pending5_, std::move(other.pending5_));
    Construct(&pending6_, std::move(other.pending6_));
    Construct(&pending7_, std::move(other.pending7_));
    Construct(&pending8_, std::move(other.pending8_));
    Construct(&pending9_, std::move(other.pending9_));
    Construct(&pending10_, std::move(other.pending10_));
    Construct(&pending11_, std::move(other.pending11_));
    Construct(&pending12_, std::move(other.pending12_));
    Construct(&pending13_, std::move(other.pending13_));
    Construct(&pending14_, std::move(other.pending14_));
    Construct(&pending15_, std::move(other.pending15_));
    Construct(&pending16_, std::move(other.pending16_));
    Construct(&pending17_, std::move(other.pending17_));
    Construct(&pending18_, std::move(other.pending18_));
    Construct(&pending19_, std::move(other.pending19_));
    Construct(&pending20_, std::move(other.pending20_));
    Construct(&pending21_, std::move(other.pending21_));
    Construct(&pending22_, std::move(other.pending22_));
    Construct(&pending23_, std::move(other.pending23_));
    Construct(&pending24_, std::move(other.pending24_));
    Construct(&pending25_, std::move(other.pending25_));
    Construct(&pending26_, std::move(other.pending26_));
    Construct(&pending27_, std::move(other.pending27_));
    Construct(&pending28_, std::move(other.pending28_));
    Construct(&pending29_, std::move(other.pending29_));
    Construct(&pending30_, std::move(other.pending30_));
    Construct(&pending31_, std::move(other.pending31_));
  }
  ~Join() {
    if (state_ & 1) Destruct(&ready0_); else Destruct(&pending0_);
    if (state_ & 2) Destruct(&ready1_); else Destruct(&pending1_);
    if (state_ & 4) Destruct(&ready2_); else Destruct(&pending2_);
    if (state_ & 8) Destruct(&ready3_); else Destruct(&pending3_);
    if (state_ & 16) Destruct(&ready4_); else Destruct(&pending4_);
    if (state_ & 32) Destruct(&ready5_); else Destruct(&pending5_);
    if (state_ & 64) Destruct(&ready6_); else Destruct(&pending6_);
    if (state_ & 128) Destruct(&ready7_); else Destruct(&pending7_);
    if (state_ & 256) Destruct(&ready8_); else Destruct(&pending8_);
    if (state_ & 512) Destruct(&ready9_); else Destruct(&pending9_);
    if (state_ & 1024) Destruct(&ready10_); else Destruct(&pending10_);
    if (state_ & 2048) Destruct(&ready11_); else Destruct(&pending11_);
    if (state_ & 4096) Destruct(&ready12_); else Destruct(&pending12_);
    if (state_ & 8192) Destruct(&ready13_); else Destruct(&pending13_);
    if (state_ & 16384) Destruct(&ready14_); else Destruct(&pending14_);
    if (state_ & 32768) Destruct(&ready15_); else Destruct(&pending15_);
    if (state_ & 65536) Destruct(&ready16_); else Destruct(&pending16_);
    if (state_ & 131072) Destruct(&ready17_); else Destruct(&pending17_);
    if (state_ & 262144) Destruct(&ready18_); else Destruct(&pending18_);
    if (state_ & 524288) Destruct(&ready19_); else Destruct(&pending19_);
    if (state_ & 1048576) Destruct(&ready20_); else Destruct(&pending20_);
    if (state_ & 2097152) Destruct(&ready21_); else Destruct(&pending21_);
    if (state_ & 4194304) Destruct(&ready22_); else Destruct(&pending22_);
    if (state_ & 8388608) Destruct(&ready23_); else Destruct(&pending23_);
    if (state_ & 16777216) Destruct(&ready24_); else Destruct(&pending24_);
    if (state_ & 33554432) Destruct(&ready25_); else Destruct(&pending25_);
    if (state_ & 67108864) Destruct(&ready26_); else Destruct(&pending26_);
    if (state_ & 134217728) Destruct(&ready27_); else Destruct(&pending27_);
    if (state_ & 268435456) Destruct(&ready28_); else Destruct(&pending28_);
    if (state_ & 536870912) Destruct(&ready29_); else Destruct(&pending29_);
    if (state_ & 1073741824) Destruct(&ready30_); else Destruct(&pending30_);
    if (state_ & 2147483648) Destruct(&ready31_); else Destruct(&pending31_);
  }
  Poll<std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16, R17, R18, R19, R20, R21, R22, R23, R24, R25, R26, R27, R28, R29, R30, R31>> operator()() {
    if ((state_ & 1) == 0) {
      auto r = pending0_();
      if (auto* p = r.get_ready()) { state_ |= 1; Destruct(&pending0_); Construct(&ready0_, std::move(*p)); }
    }
    if ((state_ & 2) == 0) {
      auto r = pending1_();
      if (auto* p = r.get_ready()) { state_ |= 2; Destruct(&pending1_); Construct(&ready1_, std::move(*p)); }
    }
    if ((state_ & 4) == 0) {
      auto r = pending2_();
      if (auto* p = r.get_ready()) { state_ |= 4; Destruct(&pending2_); Construct(&ready2_, std::move(*p)); }
    }
    if ((state_ & 8) == 0) {
      auto r = pending3_();
      if (auto* p = r.get_ready()) { state_ |= 8; Destruct(&pending3_); Construct(&ready3_, std::move(*p)); }
    }
    if ((state_ & 16) == 0) {
      auto r = pending4_();
      if (auto* p = r.get_ready()) { state_ |= 16; Destruct(&pending4_); Construct(&ready4_, std::move(*p)); }
    }
    if ((state_ & 32) == 0) {
      auto r = pending5_();
      if (auto* p = r.get_ready()) { state_ |= 32; Destruct(&pending5_); Construct(&ready5_, std::move(*p)); }
    }
    if ((state_ & 64) == 0) {
      auto r = pending6_();
      if (auto* p = r.get_ready()) { state_ |= 64; Destruct(&pending6_); Construct(&ready6_, std::move(*p)); }
    }
    if ((state_ & 128) == 0) {
      auto r = pending7_();
      if (auto* p = r.get_ready()) { state_ |= 128; Destruct(&pending7_); Construct(&ready7_, std::move(*p)); }
    }
    if ((state_ & 256) == 0) {
      auto r = pending8_();
      if (auto* p = r.get_ready()) { state_ |= 256; Destruct(&pending8_); Construct(&ready8_, std::move(*p)); }
    }
    if ((state_ & 512) == 0) {
      auto r = pending9_();
      if (auto* p = r.get_ready()) { state_ |= 512; Destruct(&pending9_); Construct(&ready9_, std::move(*p)); }
    }
    if ((state_ & 1024) == 0) {
      auto r = pending10_();
      if (auto* p = r.get_ready()) { state_ |= 1024; Destruct(&pending10_); Construct(&ready10_, std::move(*p)); }
    }
    if ((state_ & 2048) == 0) {
      auto r = pending11_();
      if (auto* p = r.get_ready()) { state_ |= 2048; Destruct(&pending11_); Construct(&ready11_, std::move(*p)); }
    }
    if ((state_ & 4096) == 0) {
      auto r = pending12_();
      if (auto* p = r.get_ready()) { state_ |= 4096; Destruct(&pending12_); Construct(&ready12_, std::move(*p)); }
    }
    if ((state_ & 8192) == 0) {
      auto r = pending13_();
      if (auto* p = r.get_ready()) { state_ |= 8192; Destruct(&pending13_); Construct(&ready13_, std::move(*p)); }
    }
    if ((state_ & 16384) == 0) {
      auto r = pending14_();
      if (auto* p = r.get_ready()) { state_ |= 16384; Destruct(&pending14_); Construct(&ready14_, std::move(*p)); }
    }
    if ((state_ & 32768) == 0) {
      auto r = pending15_();
      if (auto* p = r.get_ready()) { state_ |= 32768; Destruct(&pending15_); Construct(&ready15_, std::move(*p)); }
    }
    if ((state_ & 65536) == 0) {
      auto r = pending16_();
      if (auto* p = r.get_ready()) { state_ |= 65536; Destruct(&pending16_); Construct(&ready16_, std::move(*p)); }
    }
    if ((state_ & 131072) == 0) {
      auto r = pending17_();
      if (auto* p = r.get_ready()) { state_ |= 131072; Destruct(&pending17_); Construct(&ready17_, std::move(*p)); }
    }
    if ((state_ & 262144) == 0) {
      auto r = pending18_();
      if (auto* p = r.get_ready()) { state_ |= 262144; Destruct(&pending18_); Construct(&ready18_, std::move(*p)); }
    }
    if ((state_ & 524288) == 0) {
      auto r = pending19_();
      if (auto* p = r.get_ready()) { state_ |= 524288; Destruct(&pending19_); Construct(&ready19_, std::move(*p)); }
    }
    if ((state_ & 1048576) == 0) {
      auto r = pending20_();
      if (auto* p = r.get_ready()) { state_ |= 1048576; Destruct(&pending20_); Construct(&ready20_, std::move(*p)); }
    }
    if ((state_ & 2097152) == 0) {
      auto r = pending21_();
      if (auto* p = r.get_ready()) { state_ |= 2097152; Destruct(&pending21_); Construct(&ready21_, std::move(*p)); }
    }
    if ((state_ & 4194304) == 0) {
      auto r = pending22_();
      if (auto* p = r.get_ready()) { state_ |= 4194304; Destruct(&pending22_); Construct(&ready22_, std::move(*p)); }
    }
    if ((state_ & 8388608) == 0) {
      auto r = pending23_();
      if (auto* p = r.get_ready()) { state_ |= 8388608; Destruct(&pending23_); Construct(&ready23_, std::move(*p)); }
    }
    if ((state_ & 16777216) == 0) {
      auto r = pending24_();
      if (auto* p = r.get_ready()) { state_ |= 16777216; Destruct(&pending24_); Construct(&ready24_, std::move(*p)); }
    }
    if ((state_ & 33554432) == 0) {
      auto r = pending25_();
      if (auto* p = r.get_ready()) { state_ |= 33554432; Destruct(&pending25_); Construct(&ready25_, std::move(*p)); }
    }
    if ((state_ & 67108864) == 0) {
      auto r = pending26_();
      if (auto* p = r.get_ready()) { state_ |= 67108864; Destruct(&pending26_); Construct(&ready26_, std::move(*p)); }
    }
    if ((state_ & 134217728) == 0) {
      auto r = pending27_();
      if (auto* p = r.get_ready()) { state_ |= 134217728; Destruct(&pending27_); Construct(&ready27_, std::move(*p)); }
    }
    if ((state_ & 268435456) == 0) {
      auto r = pending28_();
      if (auto* p = r.get_ready()) { state_ |= 268435456; Destruct(&pending28_); Construct(&ready28_, std::move(*p)); }
    }
    if ((state_ & 536870912) == 0) {
      auto r = pending29_();
      if (auto* p = r.get_ready()) { state_ |= 536870912; Destruct(&pending29_); Construct(&ready29_, std::move(*p)); }
    }
    if ((state_ & 1073741824) == 0) {
      auto r = pending30_();
      if (auto* p = r.get_ready()) { state_ |= 1073741824; Destruct(&pending30_); Construct(&ready30_, std::move(*p)); }
    }
    if ((state_ & 2147483648) == 0) {
      auto r = pending31_();
      if (auto* p = r.get_ready()) { state_ |= 2147483648; Destruct(&pending31_); Construct(&ready31_, std::move(*p)); }
    }
    if (state_ != 4294967295) return kPending;
    return ready(std::tuple<R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16, R17, R18, R19, R20, R21, R22, R23, R24, R25, R26, R27, R28, R29, R30, R31>(std::move(ready0_), std::move(ready1_), std::move(ready2_), std::move(ready3_), std::move(ready4_), std::move(ready5_), std::move(ready6_), std::move(ready7_), std::move(ready8_), std::move(ready9_), std::move(ready10_), std::move(ready11_), std::move(ready12_), std::move(ready13_), std::move(ready14_), std::move(ready15_), std::move(ready16_), std::move(ready17_), std::move(ready18_), std::move(ready19_), std::move(ready20_), std::move(ready21_), std::move(ready22_), std::move(ready23_), std::move(ready24_), std::move(ready25_), std::move(ready26_), std::move(ready27_), std::move(ready28_), std::move(ready29_), std::move(ready30_), std::move(ready31_)));
  }
};
