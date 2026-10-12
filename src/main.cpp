#include "market.hpp"
#include <iostream>
#include <sstream>
int main() {
    market::MemoryRepository repository;
    market::Checkout checkout(repository);
    std::string line;
    while (std::getline(std::cin, line)) {
        try {
            std::istringstream input(line); std::string command; input >> command;
            if (command == "product") {
                market::Product p;
                if (!(input >> p.id >> p.unit >> p.cents >> p.stock >> p.restricted)) throw std::invalid_argument("product ID UNIT CENTS STOCK RESTRICTED");
                checkout.catalogue(std::move(p));
            } else if (command == "reserve") {
                market::Principal p; p.role = market::Role::owner; p.blocked = false; p.pin_verified = false;
                std::string qr, key, id; std::uint32_t quantity; std::int64_t now;
                if (!(input >> p.member >> p.household >> p.unit >> p.email_verified >> p.adult_verified >> qr >> key >> now)) throw std::invalid_argument("reserve MEMBER HOUSEHOLD UNIT EMAIL_VERIFIED ADULT_VERIFIED QR_UNIT KEY NOW [PRODUCT QUANTITY]...");
                std::map<std::string, std::uint32_t> quantities;
                while (input >> id) {
                    if (!(input >> quantity) || quantities.contains(id)) throw std::invalid_argument("invalid cart");
                    quantities.emplace(id, quantity);
                }
                const auto order = checkout.reserve(p, qr, key, quantities, now, 300);
                std::cout << order.id << ' ' << order.total << ' ' << order.expires << '\n';
            } else if (command == "expire") {
                std::int64_t now; if (!(input >> now)) throw std::invalid_argument("expire NOW"); checkout.expire(now);
            } else if (command == "stock") {
                std::string id; if (!(input >> id)) throw std::invalid_argument("stock PRODUCT"); std::cout << checkout.stock(id) << '\n';
            } else if (command == "audit") {
                for (const auto& e : repository.audit) std::cout << e.sequence << ' ' << e.action << ' ' << e.member << ' ' << e.order << '\n';
            } else if (command == "quit") break;
            else throw std::invalid_argument("unknown command");
        } catch (const std::exception& e) { std::cerr << "error: " << e.what() << '\n'; }
    }
}
