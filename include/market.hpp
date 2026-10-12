#pragma once
#include <cstdint>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace market {
enum class Role { owner, spouse, adult, minor };
enum class State { reserved, paid, collected, cancelled };
struct Principal { std::string member; std::string household; std::string unit; Role role; bool email_verified; bool adult_verified; bool pin_verified; bool blocked; };
struct Product { std::string id; std::string unit; std::int64_t cents; std::uint32_t stock; bool restricted; };
struct Line { std::string product; std::uint32_t quantity; std::int64_t unit_cents; };
struct Order { std::string id; std::string key; std::string member; std::string unit; std::vector<Line> lines; std::int64_t total; std::int64_t expires; State state; std::string receipt; };
struct Audit { std::uint64_t sequence; std::string action; std::string member; std::string order; };
struct Repository {
    virtual ~Repository() = default;
    virtual void commit(const std::map<std::string, Product>&, const std::map<std::string, Order>&, const Audit&) = 0;
};
struct PaymentVerifier {
    virtual ~PaymentVerifier() = default;
    virtual bool verify(const Order&, const std::string&) = 0;
};
class MemoryRepository final : public Repository {
public:
    std::vector<Audit> audit;
    void commit(const std::map<std::string, Product>&, const std::map<std::string, Order>&, const Audit& event) override { audit.push_back(event); }
};
class Checkout {
    mutable std::mutex mutex_;
    Repository& repository_;
    std::map<std::string, Product> products_;
    std::map<std::string, Order> orders_;
    std::map<std::string, std::string> keys_;
    std::uint64_t sequence_ = 0;
    std::uint64_t next_id_ = 1;
    static void authorized(const Principal& principal, const std::string& unit) {
        if (principal.member.empty() || principal.household.empty() || principal.blocked || principal.unit != unit)
            throw std::runtime_error("access denied");
        if (principal.role == Role::owner && !principal.email_verified) throw std::runtime_error("owner email unverified");
        if (principal.role == Role::minor && !principal.pin_verified) throw std::runtime_error("minor PIN unverified");
    }
    void commit(std::map<std::string, Product> products, std::map<std::string, Order> orders, const std::string& action, const Principal& p, const std::string& id) {
        repository_.commit(products, orders, {sequence_ + 1, action, p.member, id});
        products_.swap(products); orders_.swap(orders); ++sequence_;
    }
    static std::int64_t total(const std::vector<Line>& lines) {
        std::int64_t result = 0;
        for (const auto& line : lines) {
            if (line.unit_cents <= 0 || line.quantity == 0 || line.unit_cents > (std::numeric_limits<std::int64_t>::max() - result) / line.quantity)
                throw std::overflow_error("invalid order amount");
            result += line.unit_cents * line.quantity;
        }
        return result;
    }
public:
    explicit Checkout(Repository& repository) : repository_(repository) {}
    void catalogue(Product product) {
        std::lock_guard lock(mutex_);
        if (product.id.empty() || product.unit.empty() || product.cents <= 0) throw std::invalid_argument("invalid product");
        if (products_.contains(product.id)) throw std::logic_error("product already configured");
        products_.emplace(product.id, std::move(product));
    }
    Order reserve(const Principal& principal, const std::string& qr_unit, const std::string& key, const std::map<std::string, std::uint32_t>& quantities, std::int64_t now, std::int64_t ttl) {
        std::lock_guard lock(mutex_);
        authorized(principal, qr_unit);
        if (key.empty() || key.size() > 128 || quantities.empty() || quantities.size() > 100 || now < 0 || ttl < 1 || ttl > 900 || now > std::numeric_limits<std::int64_t>::max() - ttl)
            throw std::invalid_argument("invalid reservation");
        const auto scoped_key = principal.member + ":" + key;
        if (const auto existing = keys_.find(scoped_key); existing != keys_.end()) {
            const auto& order = orders_.at(existing->second);
            if (order.unit != qr_unit || order.lines.size() != quantities.size()) throw std::logic_error("idempotency conflict");
            for (const auto& line : order.lines) {
                const auto it = quantities.find(line.product);
                if (it == quantities.end() || it->second != line.quantity) throw std::logic_error("idempotency conflict");
            }
            return order;
        }
        auto products = products_;
        Order order{std::to_string(next_id_), key, principal.member, qr_unit, {}, 0, now + ttl, State::reserved, ""};
        for (const auto& [id, quantity] : quantities) {
            auto& product = products.at(id);
            if (product.unit != qr_unit) throw std::runtime_error("product outside QR unit");
            if (product.restricted && (principal.role == Role::minor || !principal.adult_verified)) throw std::runtime_error("age restriction");
            if (quantity == 0 || quantity > product.stock) throw std::runtime_error("insufficient stock");
            order.lines.push_back({id, quantity, product.cents});
            product.stock -= quantity;
        }
        order.total = total(order.lines);
        auto orders = orders_; orders.emplace(order.id, order);
        auto keys = keys_; keys.emplace(scoped_key, order.id);
        commit(std::move(products), std::move(orders), "reserved", principal, order.id);
        keys_.swap(keys); ++next_id_;
        return order;
    }
    Order pay(const Principal& principal, const std::string& id, const std::string& receipt, std::int64_t now, PaymentVerifier& verifier) {
        std::lock_guard lock(mutex_);
        auto orders = orders_;
        auto& order = orders.at(id);
        authorized(principal, order.unit);
        if (order.member != principal.member) throw std::runtime_error("order ownership denied");
        if (order.state == State::paid && order.receipt == receipt) return order;
        if (order.state != State::reserved || now < 0 || now >= order.expires || receipt.empty()) throw std::logic_error("order not payable");
        for (const auto& [other_id, other] : orders)
            if (other_id != id && other.receipt == receipt) throw std::runtime_error("receipt already used");
        if (!verifier.verify(order, receipt)) throw std::runtime_error("payment not verified");
        order.receipt = receipt; order.state = State::paid;
        const auto result = order;
        commit(products_, std::move(orders), "paid", principal, id);
        return result;
    }
    Order collect(const Principal& principal, const std::string& id) {
        std::lock_guard lock(mutex_);
        auto orders = orders_; auto& order = orders.at(id);
        authorized(principal, order.unit);
        if (order.member != principal.member || order.state != State::paid) throw std::logic_error("collection denied");
        order.state = State::collected; const auto result = order;
        commit(products_, std::move(orders), "collected", principal, id);
        return result;
    }
    void expire(std::int64_t now) {
        std::lock_guard lock(mutex_);
        auto products = products_; auto orders = orders_;
        bool changed = false;
        for (auto& [id, order] : orders) {
            (void)id;
            if (order.state != State::reserved || order.expires > now) continue;
            for (const auto& line : order.lines) {
                auto& stock = products.at(line.product).stock;
                if (stock > std::numeric_limits<std::uint32_t>::max() - line.quantity) throw std::overflow_error("stock overflow");
                stock += line.quantity;
            }
            order.state = State::cancelled; changed = true;
        }
        if (changed) commit(std::move(products), std::move(orders), "expired", {"system", "", "", Role::owner, true, true, true, false}, "");
    }
    std::uint32_t stock(const std::string& id) const { std::lock_guard lock(mutex_); return products_.at(id).stock; }
};
}
