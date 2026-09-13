#include "check.hpp"
#include "matching/cli.hpp"
#include <iostream>
using namespace matching;
void session_tests() {
    std::istringstream input("limit buy 10 5\n"
                             "amend order 1 11 0\n"
                             "market sell 2\n"
                             "print book\n"
                             "exit\n"
                             "limit sell 1 100\n");
    std::ostringstream output;
    std::ostringstream errors;
    CHECK(run_session(input, output, errors) == 0);
    CHECK(errors.str() == "Error: value must be positive\n");
    CHECK(output.str().find("Trade, price: 10, qty: 2\n") != std::string::npos);
    CHECK(output.str().find("3 @ 10 #1") != std::string::npos);
    CHECK(output.str().find("Order created: sell") == std::string::npos);

    std::istringstream eof_input("limit buy 1 1\nmarket sell 1");
    output.str("");
    errors.str("");
    CHECK(run_session(eof_input, output, errors) == 0);
    CHECK(output.str() == "Order created: buy 1 @ 1 1\nTrade, price: 1, qty: 1\n");
    CHECK(errors.str().empty());

    std::istringstream broken;
    broken.setstate(std::ios::badbit);
    CHECK(run_session(broken, output, errors) == 1);
    CHECK(errors.str() == "Error: input failure\n");

    Engine engine;
    engine.limit(Side::Buy, 100, 1);
    const auto flags = output.flags();
    print_book(engine, output);
    CHECK(output.flags() == flags);
    for (const auto *id : {"", "-1", "+1", "1.5", " 1", "0"}) {
        rejects([&] { (void)parse_id(id); });
    }

    Result grouped;
    grouped.trades = {{100, 2, 1, 2}, {200, 3, 3, 4}, {100, 4, 5, 6}};
    output.str("");
    print_trades(grouped, output);
    CHECK(output.str() == "Trade, price: 1, qty: 2\nTrade, price: 2, qty: 3\n"
                          "Trade, price: 1, qty: 4\n");
}

int main() {
    try {
        session_tests();
        Engine e;
        std::ostringstream out;
        execute(e, "limit buy 10 100", out);
        execute(e, "limit sell 20 100", out);
        execute(e, "limit sell 20 200", out);
        out.str("");
        out.clear();
        execute(e, "market buy 150", out);
        execute(e, "market buy 200", out);
        execute(e, "market sell 200", out);
        CHECK(
            out.str() ==
            "Trade, price: 20, qty: 150\nTrade, price: 20, qty: 150\nTrade, price: 10, qty: 100\n");
        execute(e, "limit buy 10 100", out); // ID 7; market IDs consume the counter too
        for (const auto *bad :
             {"limit buy 11 1 junk", "limit BUY 1 1", "market buy 0", "amend order 7 12 0",
              "amend order 7 - -", "cancel order 0", "cancel order 999", "amend order 7 10.123 1",
              "peg mid buy 10", "print book extra", "limit", "exit junk"}) {
            rejects([&] { execute(e, bad, out); });
        }
        CHECK(e.find(7)->price == 1000 && e.find(7)->remaining == 100);
        execute(e, "amend order 7 9.98 -", out);
        CHECK(e.find(7)->price == 998);
        execute(e, "amend order 7 - 50", out);
        CHECK(e.find(7)->remaining == 50);
        execute(e, "cancel order 7", out);
        CHECK(!e.find(7));
        execute(e, "peg bid buy 10", out);
        out.str("");
        execute(e, "print book", out);
        CHECK(out.str().find("Suspended: buy 10 @ suspended") != std::string::npos);
        CHECK(execute(e, "  ", out));
        CHECK(!execute(e, "exit", out));
        CHECK(parse_id("18446744073709551615") == std::numeric_limits<Id>::max());
        rejects([] { (void)parse_id("18446744073709551616"); });
        Result huge;
        huge.trades = {{100, std::numeric_limits<Qty>::max(), 1, 2}, {100, 1, 3, 4}};
        out.str("");
        print_trades(huge, out);
        CHECK(out.str() == "Trade, price: 1, qty: 9223372036854775807\nTrade, price: 1, qty: 1\n");
        std::cout << "All CLI tests passed\n";
    } catch (const std::exception &ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
