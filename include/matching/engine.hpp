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
    }

    void detach(const Order &order) {
        if (order.price) {
            book(order.side).erase(entry(order));
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
        }
        result.unfilled = qty; // Discarded: a market order never rests in the book.
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
};

} // namespace matching