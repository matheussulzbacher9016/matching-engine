#include "check.hpp"
#include "matching/engine.hpp"
#include <iostream>
#include <random>
using namespace matching;

// Independent, intentionally slow oracle: vector + linear scan, without pegged orders.
struct SlowOrder {
    Id id;
    Side side;
    Price price;
    Qty qty;
    Sequence seq;
};
struct Slow {
    std::vector<SlowOrder> orders;
    Id next_id = 1;
    Sequence next_seq = 1;
    std::size_t best(Side side) const {
        auto index = orders.size();
        for (std::size_t i = 0; i < orders.size(); ++i) {
            const auto &o = orders[i];
            if (o.side != side) {
                continue;
            }
            if (index == orders.size() || (o.price != orders[index].price
                                               ? (side == Side::Buy ? o.price > orders[index].price
                                                                    : o.price < orders[index].price)
                                               : o.seq < orders[index].seq)) {
                index = i;
            }
        }
        return index;
    }
    Result submit(Side side, Price price, Qty qty, bool market) {
        Result r;
        r.id = next_id++;
        const auto seq = market ? 0 : next_seq++;
        while (qty > 0) {
            const auto i = best(side == Side::Buy ? Side::Sell : Side::Buy);
            if (i == orders.size()) {
                break;
            }
            auto &other = orders[i];
            if (!market && (side == Side::Buy ? price < other.price : price > other.price)) {
                break;
            }
            const auto filled = std::min(qty, other.qty);
            r.trades.push_back({other.price, filled, side == Side::Buy ? r.id : other.id,
                                side == Side::Sell ? r.id : other.id});
            qty -= filled;
            other.qty -= filled;
            if (other.qty == 0) {
                orders.erase(orders.begin() + static_cast<std::ptrdiff_t>(i));
            }
        }
        if (market) {
            r.unfilled = qty;
        } else if (qty) {
            orders.push_back({r.id, side, price, qty, seq});
        }
        return r;
    }
};
void differential_tests(unsigned seed) {
    Engine fast;
    Slow slow;
    std::mt19937 random(seed);
    for (int step = 0; step < 2000; ++step) {
        const auto side = random() % 2 ? Side::Buy : Side::Sell;
        const Price price = 950 + random() % 101;
        const Qty qty = 1 + random() % 30;
        const bool market = random() % 4 == 0;
        const auto actual = market ? fast.market(side, qty) : fast.limit(side, price, qty);
        const auto expected = slow.submit(side, price, qty, market);
        CHECK(actual.id == expected.id && actual.unfilled == expected.unfilled);
        CHECK(actual.trades.size() == expected.trades.size());
        for (std::size_t i = 0; i < actual.trades.size(); ++i) {
            const auto &a = actual.trades[i];
            const auto &b = expected.trades[i];
            CHECK(a.price == b.price && a.qty == b.qty && a.buy_id == b.buy_id &&
                  a.sell_id == b.sell_id);
        }
        auto expected_rows = slow.orders;
        std::sort(expected_rows.begin(), expected_rows.end(), [](const auto &a, const auto &b) {
            if (a.side != b.side) {
                return a.side == Side::Buy;
            }
            if (a.price != b.price) {
                return a.side == Side::Buy ? a.price > b.price : a.price < b.price;
            }
            return a.seq < b.seq;
        });
        auto rows = fast.snapshot(Side::Buy);
        const auto sells = fast.snapshot(Side::Sell);
        rows.insert(rows.end(), sells.begin(), sells.end());
        CHECK(rows.size() == expected_rows.size());
        for (std::size_t i = 0; i < rows.size(); ++i) {
            CHECK(rows[i].id == expected_rows[i].id && rows[i].price == expected_rows[i].price);
            CHECK(rows[i].remaining == expected_rows[i].qty);
        }
    }
}
std::vector<Order> all(const Engine &e) {
    auto result = e.snapshot(Side::Buy);
    const auto sells = e.snapshot(Side::Sell), suspended = e.suspended();
    result.insert(result.end(), sells.begin(), sells.end());
    result.insert(result.end(), suspended.begin(), suspended.end());
    return result;
}
Qty total(const Engine &e) {
    Qty result = 0;
    for (const auto &o : all(e)) {
        result += o.remaining;
    }
    return result;
}
void invariants(const Engine &e) {
    std::optional<Price> bid, offer;
    std::set<Id> ids;
    for (const auto &o : all(e)) {
        CHECK(o.remaining > 0 && ids.insert(o.id).second);
        const auto found = e.find(o.id);
        CHECK(found.has_value());
        CHECK(found->sequence == o.sequence && found->working_sequence == o.working_sequence);
        CHECK(found->price == o.price && found->remaining == o.remaining);
        CHECK(found->kind == o.kind && found->side == o.side);
        if (o.kind != Kind::Limit) {
            continue;
        }
        CHECK(o.price && *o.price > 0);
        if (o.side == Side::Buy && (!bid || *o.price > *bid)) {
            bid = o.price;
        }
        if (o.side == Side::Sell && (!offer || *o.price < *offer)) {
            offer = o.price;
        }
    }
    for (const auto &o : all(e)) {
        if (o.kind == Kind::PegBid) {
            CHECK(o.price == bid);
        }
        if (o.kind == Kind::PegOffer) {
            CHECK(o.price == offer);
        }
    }
    const auto buys = e.snapshot(Side::Buy), sells = e.snapshot(Side::Sell);
    if (!buys.empty() && !sells.empty()) {
        CHECK(*buys.front().price < *sells.front().price);
    }
    for (const auto side : {Side::Buy, Side::Sell}) {
        const auto rows = e.snapshot(side);
        for (std::size_t i = 1; i < rows.size(); ++i) {
            CHECK(rows[i].price);
            if (rows[i - 1].price == rows[i].price) {
                CHECK(rows[i - 1].sequence < rows[i].sequence);
            } else {
                CHECK(side == Side::Buy ? *rows[i - 1].price > *rows[i].price
                                        : *rows[i - 1].price < *rows[i].price);
            }
        }
    }
}
void random_lifecycle_tests(unsigned seed) {
    Engine e;
    std::mt19937 random(seed);
    for (int step = 0; step < 3000; ++step) {
        const auto rows = all(e);
        const auto action = rows.empty() ? 0U : random() % 5;
        const auto side = random() % 2 ? Side::Buy : Side::Sell;
        const Qty qty = 1 + random() % 50;
        const Price price = 950 + random() % 101;
        Qty expected = total(e);
        Result r;
        if (action == 0) {
            r = e.limit(side, price, qty);
            expected += qty;
        } else if (action == 1) {
            r = e.peg(random() % 2 ? Kind::PegBid : Kind::PegOffer, side, qty);
            expected += qty;
        } else if (action == 2) {
            r = e.market(side, qty);
        } else {
            const auto old = rows[random() % rows.size()];
            if (action == 3) {
                r = e.cancel(old.id);
                expected -= old.remaining;
            } else {
                r = e.amend(old.id,
                            old.kind == Kind::Limit ? std::optional<Price>{price} : std::nullopt,
                            qty);
                expected += qty - old.remaining;
            }
        }
        for (const auto &trade : r.trades) {
            CHECK(trade.price > 0 && trade.qty > 0 && trade.buy_id != trade.sell_id);
            const bool market_fill = action == 2 && (trade.buy_id == r.id || trade.sell_id == r.id);
            expected -= (market_fill ? 1 : 2) * trade.qty;
        }
        CHECK(total(e) == expected);
        invariants(e);
    }
}
int main() {
    try {
        for (unsigned seed : {0U, 1U, 42U, 12345U, 987654U}) {
            std::cout << "Checking seed " << seed << '\n';
            differential_tests(seed);
            random_lifecycle_tests(seed);
        }
        std::cout << "10000 differential operations and 15000 lifecycle operations passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
