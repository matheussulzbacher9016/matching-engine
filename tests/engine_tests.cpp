#include "check.hpp"
#include "matching/engine.hpp"
#include <iostream>
using namespace matching;
void numeric_tests() {
    CHECK(parse_price("10") == 1000);
    CHECK(parse_price("9.99") == 999);
    CHECK(parse_price("10.1") == 1010);
    CHECK(format_price(1010) == "10.1");
    CHECK(format_price(1001) == "10.01");
    CHECK(parse_qty("100") == 100);
    CHECK(parse_price("92233720368547758.07") == std::numeric_limits<Price>::max());
    for (const auto *s : {"", "0", "-1", "+1", "NaN", "1.001", "1.", ".1", "1,1", "1e2", "1.2.3",
                          "92233720368547758.08"}) {
        rejects([&] { (void)parse_price(s); });
    }
    rejects([] { (void)parse_qty("1.5"); });
    rejects([] { (void)parse_qty("9223372036854775808"); });
}
void matching_tests() {
    Engine e;
    const auto b = e.limit(Side::Buy, 1000, 100).id;
    const auto a1 = e.limit(Side::Sell, 2000, 100).id;
    const auto a2 = e.limit(Side::Sell, 2000, 200).id;
    auto r = e.market(Side::Buy, 150);
    CHECK(r.trades.size() == 2);
    CHECK(r.trades[0].sell_id == a1 && r.trades[0].qty == 100);
    CHECK(r.trades[1].sell_id == a2 && r.trades[1].qty == 50);
    CHECK(!e.find(a1) && e.find(a2)->remaining == 150);
    r = e.market(Side::Buy, 200);
    CHECK(r.trades.size() == 1 && r.trades[0].qty == 150 && r.unfilled == 50);
    r = e.market(Side::Sell, 200);
    CHECK(r.trades[0].buy_id == b && r.unfilled == 100);
    CHECK(e.snapshot(Side::Buy).empty() && e.snapshot(Side::Sell).empty());
    CHECK(e.market(Side::Buy, 10).unfilled == 10);
    Engine levels;
    const auto expensive = levels.limit(Side::Sell, 1100, 7).id;
    const auto cheap = levels.limit(Side::Sell, 1000, 3).id;
    r = levels.limit(Side::Buy, 1050, 5);
    CHECK(r.trades.size() == 1 && r.trades[0].sell_id == cheap && r.trades[0].price == 1000);
    CHECK(levels.find(r.id)->remaining == 2 && levels.find(expensive)->remaining == 7);
    Engine sell;
    const auto high = sell.limit(Side::Buy, 1200, 4).id;
    sell.limit(Side::Buy, 1100, 4);
    r = sell.limit(Side::Sell, 1000, 7);
    CHECK(r.trades.size() == 2 && r.trades[0].buy_id == high && r.trades[1].price == 1100);
    rejects([&] { (void)sell.limit(Side::Buy, 0, 1); });
    rejects([&] { (void)sell.market(Side::Buy, -1); });
}
void amendment_tests() {
    Engine e;
    const auto first = e.limit(Side::Buy, 1000, 200).id;
    const auto second = e.limit(Side::Buy, 999, 100).id;
    e.amend(first, 998, std::nullopt);
    CHECK(e.snapshot(Side::Buy)[0].id == second);
    CHECK(e.snapshot(Side::Buy)[1].id == first);
    e.cancel(second);
    CHECK(!e.find(second));
    rejects([&] { (void)e.cancel(second); });
    rejects([&] { (void)e.amend(first, 1000, 0); });
    CHECK(e.find(first)->price == 998 && e.find(first)->remaining == 200);
    rejects([&] { (void)e.amend(first, {}, {}); });
    Engine fifo;
    const auto a = fifo.limit(Side::Sell, 1000, 10).id;
    const auto b = fifo.limit(Side::Sell, 1000, 10).id;
    fifo.amend(a, {}, 5); // reduction retains priority
    CHECK(fifo.snapshot(Side::Sell)[0].id == a);
    fifo.amend(a, {}, 6); // increase loses priority
    CHECK(fifo.snapshot(Side::Sell)[0].id == b);
    auto r = fifo.amend(a, 900, 3);
    CHECK(r.trades.empty() && fifo.snapshot(Side::Sell)[0].id == a);
    const auto buy = fifo.limit(Side::Buy, 800, 5).id;
    r = fifo.amend(buy, 900, {});
    CHECK(r.trades.size() == 1 && r.trades[0].price == 900 && r.trades[0].qty == 3);
    CHECK(fifo.find(buy)->remaining == 2);
    fifo.cancel(buy);
    CHECK(!fifo.find(buy));
    Engine middle;
    const auto x = middle.limit(Side::Buy, 100, 1).id;
    const auto y = middle.limit(Side::Buy, 100, 1).id;
    const auto z = middle.limit(Side::Buy, 100, 1).id;
    middle.cancel(y);
    r = middle.market(Side::Sell, 2);
    CHECK(r.trades[0].buy_id == x && r.trades[1].buy_id == z);
}
void peg_tests() {
    Engine e;
    const auto low = e.limit(Side::Buy, 999, 100).id;
    const auto bid = e.limit(Side::Buy, 1000, 200).id;
    e.limit(Side::Sell, 1050, 100);
    const auto peg = e.peg(Kind::PegBid, Side::Buy, 150).id;
    CHECK(e.snapshot(Side::Buy)[0].id == bid && e.snapshot(Side::Buy)[1].id == peg);
    const auto better = e.limit(Side::Buy, 1010, 300).id;
    auto rows = e.snapshot(Side::Buy);
    CHECK(rows[0].id == peg && rows[1].id == better && rows[2].id == bid);
    CHECK(rows[0].price == 1010);
    e.cancel(better);
    CHECK(e.find(peg)->price == 1000);
    e.cancel(bid);
    CHECK(e.find(peg)->price == 999);
    e.cancel(low);
    CHECK(!e.find(peg)->price && e.suspended().size() == 1);
    CHECK(e.market(Side::Sell, 10).unfilled == 10);
    const auto revived = e.limit(Side::Buy, 980, 50).id;
    CHECK(e.snapshot(Side::Buy)[0].id == peg && e.find(peg)->price == 980);
    e.amend(peg, {}, 200);
    CHECK(e.snapshot(Side::Buy)[0].id == revived);
    rejects([&] { (void)e.amend(peg, 990, {}); });
    e.cancel(peg);
    CHECK(!e.find(peg));
    Engine offer;
    const auto anchor = offer.limit(Side::Sell, 1100, 1).id;
    const auto op = offer.peg(Kind::PegOffer, Side::Sell, 3).id;
    const auto newer = offer.limit(Side::Sell, 1090, 2).id;
    CHECK(offer.snapshot(Side::Sell)[0].id == op);
    offer.cancel(newer);
    auto r = offer.market(Side::Buy, 4);
    CHECK(r.trades.size() == 1 && r.trades[0].sell_id == anchor && r.unfilled == 3);
    CHECK(!offer.find(op)->price); // anchor disappears BEFORE next fill
    Engine opposite;
    opposite.limit(Side::Sell, 1050, 5);
    r = opposite.peg(Kind::PegOffer, Side::Buy, 7);
    CHECK(r.trades.size() == 1 && r.trades[0].qty == 5 && r.trades[0].price == 1050);
    CHECK(opposite.find(r.id)->remaining == 2 && !opposite.find(r.id)->price);
    Engine suspended;
    const auto p1 = suspended.peg(Kind::PegBid, Side::Sell, 2).id;
    const auto p2 = suspended.peg(Kind::PegOffer, Side::Buy, 2).id;
    CHECK(suspended.suspended().size() == 2);
    r = suspended.limit(Side::Buy, 1000, 3);
    CHECK(r.trades.size() == 1 && r.trades[0].sell_id == p1);
    CHECK(!suspended.find(p1) && suspended.find(p2));
    suspended.cancel(p2);
    Engine repriced;
    repriced.limit(Side::Buy, 1000, 100);
    const auto moving = repriced.peg(Kind::PegBid, Side::Buy, 10).id;
    repriced.limit(Side::Sell, 1500, 10);
    r = repriced.limit(Side::Buy, 2000, 100);
    CHECK(r.trades.size() == 1 && r.trades[0].buy_id == moving);
    CHECK(r.trades[0].price == 1500); // existing ask, NOT the peg's new bid of 2000
    Engine multi;
    const auto old = multi.peg(Kind::PegBid, Side::Buy, 1).id;
    const auto next = multi.peg(Kind::PegBid, Side::Buy, 1).id;
    multi.limit(Side::Buy, 1000, 3);
    r = multi.market(Side::Sell, 2);
    CHECK(r.trades[0].buy_id == old && r.trades[1].buy_id == next);
}

void extra_numeric_tests() {
    CHECK(parse_qty("9223372036854775807") == std::numeric_limits<Qty>::max());
    CHECK(parse_price("00010.50") == 1050);
    CHECK(parse_price("0.01") == 1);
    CHECK(format_price(std::numeric_limits<Price>::max()) == "92233720368547758.07");
    for (Price price = 1; price <= 100000; ++price) {
        CHECK(parse_price(format_price(price)) == price);
    }
    for (const auto *text : {"0", "-1", "+1", " 1", "1 ", "", "1e2", "1,5"}) {
        rejects([&] { (void)parse_qty(text); });
    }
    rejects([] { (void)format_price(0); });
    rejects([] { validate_side(static_cast<Side>(9)); });
}

void extra_matching_tests() {
    for (Side side : {Side::Buy, Side::Sell}) {
        Engine engine;
        const Side opposite = side == Side::Buy ? Side::Sell : Side::Buy;
        const auto first = engine.limit(side, 1000, 2).id;
        const auto second = engine.limit(side, 1000, 3).id;
        const auto result = engine.market(opposite, 4);
        CHECK(result.trades.size() == 2);
        CHECK(result.trades[0].qty == 2 && result.trades[1].qty == 2);
        CHECK((side == Side::Buy ? result.trades[0].buy_id : result.trades[0].sell_id) == first);
        CHECK((side == Side::Buy ? result.trades[1].buy_id : result.trades[1].sell_id) == second);
        CHECK(!engine.find(first));
        CHECK(engine.find(second).has_value());
        CHECK(engine.find(second)->remaining == 1);

        auto copy = engine.snapshot(side);
        copy[0].remaining = 99;
        CHECK(engine.find(second)->remaining == 1);
    }

    Engine invalid;
    rejects([&] { (void)invalid.limit(static_cast<Side>(2), 100, 1); });
    rejects([&] { (void)invalid.market(Side::Buy, 0); });
    rejects([&] { (void)invalid.limit(Side::Sell, 100, -1); });
    CHECK(invalid.limit(Side::Buy, 100, 1).id == 1);

    Engine levels;
    levels.limit(Side::Sell, 1100, 3);
    levels.limit(Side::Sell, 1000, 2);
    levels.limit(Side::Sell, 1200, 5);
    const auto swept = levels.market(Side::Buy, 7);
    CHECK(swept.trades.size() == 3);
    CHECK(swept.trades[0].price == 1000 && swept.trades[0].qty == 2);
    CHECK(swept.trades[1].price == 1100 && swept.trades[1].qty == 3);
    CHECK(swept.trades[2].price == 1200 && swept.trades[2].qty == 2);
    CHECK(swept.unfilled == 0);
}

void extra_amendment_tests() {
    Engine engine;
    const auto first = engine.limit(Side::Sell, 1000, 100).id;
    const auto second = engine.limit(Side::Sell, 1000, 50).id;
    engine.market(Side::Buy, 40);
    CHECK(engine.find(first)->remaining == 60);
    const auto priority = engine.find(first)->sequence;

    engine.amend(first, 1000, 60);
    CHECK(engine.find(first)->sequence == priority); // No-op.
    engine.amend(first, {}, 59);
    CHECK(engine.find(first)->sequence == priority); // Reduction only.
    engine.amend(first, {}, 70);
    CHECK(engine.find(first)->remaining == 70); // Not 100 - 40 or 59 + 70.
    CHECK(engine.find(first)->sequence > engine.find(second)->sequence);

    const auto before = *engine.find(first);
    rejects([&] { (void)engine.amend(first, -1, 20); });
    rejects([&] { (void)engine.amend(first, 500, 0); });
    CHECK(engine.find(first)->price == before.price);
    CHECK(engine.find(first)->remaining == before.remaining);
    CHECK(engine.find(first)->sequence == before.sequence);

    engine.cancel(first);
    const auto result = engine.market(Side::Buy, 100);
    CHECK(result.trades.size() == 1);
    CHECK(result.trades[0].sell_id == second);
    CHECK(result.unfilled == 50);
    rejects([&] { (void)engine.cancel(second); }); // Already fully executed.
    rejects([&] { (void)engine.amend(second, {}, 10); });
}

void extra_peg_tests() {
    for (Side side : {Side::Buy, Side::Sell}) {
        Engine engine;
        const Kind kind = side == Side::Buy ? Kind::PegBid : Kind::PegOffer;
        const auto anchor1 = engine.limit(side, 1000, 2).id;
        const auto anchor2 = engine.limit(side, 1000, 3).id;
        const auto peg = engine.peg(kind, side, 4).id;
        engine.cancel(anchor1);
        CHECK(engine.find(peg)->price == 1000); // Duplicate anchor must survive.
        engine.cancel(anchor2);
        CHECK(!engine.find(peg)->price);
        CHECK(engine.suspended().size() == 1);

        engine.amend(peg, {}, 5);
        CHECK(engine.find(peg)->remaining == 5 && !engine.find(peg)->price);
        const auto new_anchor = engine.limit(side, 900, 1).id;
        const auto active = engine.snapshot(side);
        CHECK(active.size() == 2 && active[0].id == peg && active[1].id == new_anchor);
    }

    Engine repriced_sell;
    repriced_sell.limit(Side::Sell, 2000, 100);
    const auto peg = repriced_sell.peg(Kind::PegOffer, Side::Sell, 10).id;
    repriced_sell.limit(Side::Buy, 1500, 10);
    const auto result = repriced_sell.limit(Side::Sell, 1000, 100);
    CHECK(result.trades.size() == 1 && result.trades[0].sell_id == peg);
    CHECK(result.trades[0].price == 1500); // Existing bid supplies execution price.

    Engine across;
    across.limit(Side::Buy, 1000, 5);
    const auto sell = across.peg(Kind::PegBid, Side::Sell, 7);
    CHECK(sell.trades.size() == 1 && sell.trades[0].qty == 5);
    CHECK(sell.trades[0].price == 1000);
    CHECK(across.find(sell.id)->remaining == 2 && !across.find(sell.id)->price);
    rejects([&] { (void)across.peg(Kind::Limit, Side::Buy, 1); });
}

int main() {
    try {
        numeric_tests();
        extra_numeric_tests();
        matching_tests();
        extra_matching_tests();
        amendment_tests();
        extra_amendment_tests();
        peg_tests();
        extra_peg_tests();
        std::cout << "All tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
