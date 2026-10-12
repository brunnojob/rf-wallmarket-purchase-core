#include "market.hpp"
#include <cassert>
#include <atomic>
#include <thread>
struct Verifier final : market::PaymentVerifier {
    bool verify(const market::Order& order, const std::string& receipt) override { return order.total == 990 && receipt == "provider-receipt"; }
};
struct FailingRepository final : market::Repository {
    void commit(const std::map<std::string, market::Product>&, const std::map<std::string, market::Order>&, const market::Audit&) override { throw std::runtime_error("write failed"); }
};
template<class F> void rejected(F f) { bool denied = false; try { f(); } catch (const std::exception&) { denied = true; } assert(denied); }
int main() {
    market::MemoryRepository repository; market::Checkout checkout(repository); Verifier verifier;
    checkout.catalogue({"water", "u1", 990, 3, false});
    checkout.catalogue({"wine", "u1", 3000, 2, true});
    checkout.catalogue({"foreign", "u2", 100, 2, false});
    market::Principal p{"m1", "h1", "u1", market::Role::owner, true, true, false, false};
    auto minor = p; minor.role = market::Role::minor; minor.pin_verified = true;
    rejected([&] { checkout.reserve(minor, "u1", "minor", {{"wine", 1}}, 100, 300); });
    rejected([&] { checkout.reserve(p, "u1", "cross", {{"foreign", 1}}, 100, 300); });
    rejected([&] { checkout.reserve(p, "u1", "partial", {{"water", 1}, {"wine", 3}}, 100, 300); });
    assert(checkout.stock("water") == 3);
    auto order = checkout.reserve(p, "u1", "request", {{"water", 1}}, 100, 300);
    assert(order.total == 990 && checkout.stock("water") == 2);
    assert(checkout.reserve(p, "u1", "request", {{"water", 1}}, 100, 300).id == order.id);
    rejected([&] { checkout.reserve(p, "u1", "request", {{"water", 2}}, 100, 300); });
    auto stranger = p; stranger.member = "m2";
    rejected([&] { checkout.pay(stranger, order.id, "provider-receipt", 101, verifier); });
    rejected([&] { checkout.pay(p, order.id, "bad", 101, verifier); });
    assert(checkout.pay(p, order.id, "provider-receipt", 101, verifier).state == market::State::paid);
    assert(checkout.collect(p, order.id).state == market::State::collected);
    rejected([&] { checkout.collect(p, order.id); });
    checkout.reserve(p, "u1", "expires", {{"water", 2}}, 100, 1);
    checkout.expire(101); checkout.expire(102);
    assert(checkout.stock("water") == 2);
    FailingRepository failed; market::Checkout transaction(failed);
    transaction.catalogue({"p", "u1", 100, 2, false});
    rejected([&] { transaction.reserve(p, "u1", "key", {{"p", 1}}, 100, 10); });
    assert(transaction.stock("p") == 2);
    market::MemoryRepository concurrent_repository; market::Checkout concurrent(concurrent_repository);
    concurrent.catalogue({"limited", "u1", 100, 8, false});
    std::atomic<unsigned> successes{0};
    std::vector<std::thread> workers;
    for (unsigned i = 0; i < 32; ++i) workers.emplace_back([&, i] {
        try { concurrent.reserve(p, "u1", std::to_string(i), {{"limited", 1}}, 100, 300); ++successes; }
        catch (const std::runtime_error&) {}
    });
    for (auto& worker : workers) worker.join();
    assert(successes == 8 && concurrent.stock("limited") == 0);
}
