#pragma once

#include "types.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>

namespace matching {

class Engine {
    struct Entry {
        Price price;
        Sequence sequence;
        Id id;
    };

    struct Priority {
        Side side;

        bool operator()(const Entry &left, const Entry &right) const {
            if (left.price != right.price) {
                return side == Side::Buy ? left.price > right.price : left.price < right.price;
            }
            return std::tie(left.sequence, left.id) < std::tie(right.sequence, right.id);
        }
    };

    using Book = std::set<Entry, Priority>;

    Book bids_{Priority{Side::Buy}};
    Book asks_{Priority{Side::Sell}};
    std::map<Id, Order> orders_;
    Id next_id_ = 1;
    Sequence next_sequence_ = 1;

    std::multiset<Price> fixed_bids_;
    std::multiset<Price> fixed_asks_;
    std::set<Id> pegs_;

    std::multiset<Price> &anchors(Side side) {
        return side == Side::Buy ? fixed_bids_ : fixed_asks_;
    }

    std::optional<Price> reference(Kind kind) const {
        if (kind == Kind::PegBid) {
            if (fixed_bids_.empty()) {
                return std::nullopt;
            }
            return *fixed_bids_.rbegin();
        }
        if (fixed_asks_.empty()) {
            return std::nullopt;
        }
        return *fixed_asks_.begin();
    }

    void refresh_pegs() {
        // Pegs never anchor other pegs; these references cannot change in this loop.
        const auto bid = reference(Kind::PegBid);
        const auto offer = reference(Kind::PegOffer);

        for (Id id : pegs_) {
            auto &order = orders_.at(id);
            const auto new_price = order.kind == Kind::PegBid ? bid : offer;
            if (order.price == new_price) {
                continue;
            }

            const auto activation = allocate_sequence();
            if (order.price) {
                book(order.side).erase(entry(order));
            }
            order.price = new_price;
            order.working_sequence = activation;
            // Keep sequence: automatic repricing preserves FIFO priority.
            if (order.price) {
                book(order.side).insert(entry(order));
            }
        }
    }

    Book &book(Side side) {
        return side == Side::Buy ? bids_ : asks_;
    }

    const Book &book(Side side) const {
        return side == Side::Buy ? bids_ : asks_;
    }

    static Entry entry(const Order &order) {
        return {*order.price, order.sequence, order.id};
    }

    Id allocate_id() {
        if (next_id_ == std::numeric_limits<Id>::max()) {
            throw std::overflow_error("ID space exhausted");
        }
        return next_id_++;
    }

    Sequence allocate_sequence() {
        if (next_sequence_ == std::numeric_limits<Sequence>::max()) {
            throw std::overflow_error("sequence space exhausted");
        }
        return next_sequence_++;
    }

    void attach(const Order &order) {
        if (order.price) {
            book(order.side).insert(entry(order));
        }
        if (order.kind == Kind::Limit) {
            anchors(order.side).insert(*order.price);
        } else {
            pegs_.insert(order.id);
        }
    }

    void detach(const Order &order) {
        if (order.price) {
            book(order.side).erase(entry(order));
        }
        if (order.kind == Kind::Limit) {
            auto &prices = anchors(order.side);
            prices.erase(prices.find(*order.price)); // Remove exactly one anchor.
        } else {
            pegs_.erase(order.id);
        }
    }

    void erase_order(Id id) {
        const auto &order = orders_.at(id);
        detach(order);
        orders_.erase(id);
    }

    void consume(Id id, Qty qty) {
        auto &order = orders_.at(id);
        order.remaining -= qty;
        if (order.remaining == 0) {
            erase_order(id);
        }
    }

    void settle(std::vector<Trade> &trades) {
        refresh_pegs();
        while (!bids_.empty() && !asks_.empty() && bids_.begin()->price >= asks_.begin()->price) {
            // Copies survive removal of fully filled orders by consume().
            const auto buy = orders_.at(bids_.begin()->id);
            const auto sell = orders_.at(asks_.begin()->id);
            const Price price =
                buy.working_sequence < sell.working_sequence ? *buy.price : *sell.price;
            const Qty filled = std::min(buy.remaining, sell.remaining);

            trades.push_back({price, filled, buy.id, sell.id});
            consume(buy.id, filled);
            consume(sell.id, filled);
            refresh_pegs();
        }
    }

public:
    Result limit(Side side, Price price, Qty qty) {
        validate_side(side);
        positive(price);
        positive(qty);

        const auto sequence = allocate_sequence();
        const auto id = allocate_id();
        const Order order{id, side, Kind::Limit, price, qty, sequence, sequence};
        orders_.emplace(id, order);
        attach(order);

        Result result;
        result.id = id;
        settle(result.trades);
        return result;
    }

    Result market(Side side, Qty qty) {
        validate_side(side);
        positive(qty);

        Result result;
        result.id = allocate_id();
        auto &opposite = book(side == Side::Buy ? Side::Sell : Side::Buy);
        while (qty > 0 && !opposite.empty()) {
            const auto resting = orders_.at(opposite.begin()->id);
            const Qty filled = std::min(qty, resting.remaining);
            const Id buy_id = side == Side::Buy ? result.id : resting.id;
            const Id sell_id = side == Side::Sell ? result.id : resting.id;

            result.trades.push_back({*resting.price, filled, buy_id, sell_id});
            consume(resting.id, filled);
            qty -= filled;
            settle(result.trades);
        }
        result.unfilled = qty; // Discarded: a market order never rests in the book.
        return result;
    }

    Result peg(Kind kind, Side side, Qty qty) {
        validate_side(side);
        positive(qty);
        if (kind != Kind::PegBid && kind != Kind::PegOffer) {
            throw std::invalid_argument("invalid peg reference");
        }

        const auto sequence = allocate_sequence();
        const auto id = allocate_id();
        const Order order{id, side, kind, reference(kind), qty, sequence, sequence};
        orders_.emplace(id, order);
        attach(order);

        Result result;
        result.id = id;
        settle(result.trades);
        return result;
    }

    Result cancel(Id id) {
        if (!find(id)) {
            throw std::invalid_argument("unknown order ID");
        }
        erase_order(id);

        Result result;
        result.id = id;
        settle(result.trades);
        return result;
    }

    Result amend(Id id, std::optional<Price> price, std::optional<Qty> qty) {
        const auto old = find(id);
        if (!old) {
            throw std::invalid_argument("unknown order ID");
        }
        if (!price && !qty) {
            throw std::invalid_argument("no amendment fields");
        }
        if (price) {
            positive(*price);
        }
        if (qty) {
            positive(*qty);
        }
        if (price && old->kind != Kind::Limit) {
            throw std::invalid_argument("pegged price is automatic");
        }

        Order updated = *old;
        if (price) {
            updated.price = *price;
        }
        if (qty) {
            updated.remaining = *qty; // New remaining quantity, not an increment.
        }
        const bool loses_priority =
            updated.price != old->price || updated.remaining > old->remaining;
        if (loses_priority) {
            updated.sequence = allocate_sequence();
            updated.working_sequence = updated.sequence;
        }

        // Invalid user input has been rejected before modifying any index.
        detach(*old);
        orders_.at(id) = updated;
        attach(updated);

        Result result;
        result.id = id;
        settle(result.trades);
        return result;
    }

    std::optional<Order> find(Id id) const {
        const auto iterator = orders_.find(id);
        if (iterator == orders_.end()) {
            return std::nullopt;
        }
        return iterator->second;
    }

    std::vector<Order> snapshot(Side side) const {
        validate_side(side);
        std::vector<Order> result;
        result.reserve(book(side).size());
        for (const auto &item : book(side)) {
            result.push_back(orders_.at(item.id));
        }
        return result;
    }

    std::vector<Order> suspended() const {
        std::vector<Order> result;
        for (Id id : pegs_) {
            const auto &order = orders_.at(id);
            if (!order.price) {
                result.push_back(order);
            }
        }
        return result;
    }
};

} // namespace matching
