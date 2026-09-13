#pragma once

#include "engine.hpp"

#include <iomanip>
#include <istream>
#include <ostream>
#include <sstream>

namespace matching {

inline Side parse_side(const std::string &text) {
    if (text == "buy") {
        return Side::Buy;
    }
    if (text == "sell") {
        return Side::Sell;
    }
    throw std::invalid_argument("side must be buy or sell");
}

inline Id parse_id(const std::string &text) {
    if (text.empty()) {
        throw std::invalid_argument("empty ID");
    }
    Id value = 0;
    for (char character : text) {
        if (character < '0' || character > '9') {
            throw std::invalid_argument("invalid ID");
        }
        const auto digit = static_cast<Id>(character - '0');
        if (value > (std::numeric_limits<Id>::max() - digit) / 10) {
            throw std::invalid_argument("ID too large");
        }
        value = value * 10 + digit;
    }
    if (value == 0) {
        throw std::invalid_argument("ID must be positive");
    }
    return value;
}

inline std::string order_row(const Order &order) {
    const auto price = order.price ? format_price(*order.price) : "suspended";
    auto row = std::to_string(order.remaining) + " @ " + price + " #" + std::to_string(order.id);
    if (order.kind != Kind::Limit) {
        row += order.kind == Kind::PegBid ? " [peg bid]" : " [peg offer]";
    }
    return row;
}

inline void print_book(const Engine &engine, std::ostream &out) {
    const auto buys = engine.snapshot(Side::Buy);
    const auto sells = engine.snapshot(Side::Sell);
    std::size_t width = std::string("Ordens de Compra").size();
    for (const auto &order : buys) {
        width = std::max(width, order_row(order).size());
    }
    width += 2;

    // Format in a local stream, leaving the caller's formatting flags unchanged.
    std::ostringstream table;
    table << std::left << std::setw(static_cast<int>(width)) << "Ordens de Compra"
          << "| Ordens de Venda\n";
    table << std::string(width, '-') << "|-----------------\n";
    for (std::size_t index = 0; index < std::max(buys.size(), sells.size()); ++index) {
        const auto buy = index < buys.size() ? order_row(buys[index]) : "";
        const auto sell = index < sells.size() ? order_row(sells[index]) : "";
        table << std::setw(static_cast<int>(width)) << buy << "| " << sell << '\n';
    }
    for (const auto &order : engine.suspended()) {
        table << "Suspended: " << name(order.side) << ' ' << order_row(order) << '\n';
    }
    out << table.str();
}

inline void print_trades(const Result &result, std::ostream &out) {
    // Individual fills remain intact; aggregation is presentation-only.
    std::size_t index = 0;
    while (index < result.trades.size()) {
        const Price price = result.trades[index].price;
        Qty qty = 0;
        do {
            qty += result.trades[index].qty;
            ++index;
        } while (index < result.trades.size() && result.trades[index].price == price &&
                 result.trades[index].qty <= std::numeric_limits<Qty>::max() - qty);
        out << "Trade, price: " << format_price(price) << ", qty: " << qty << '\n';
    }
}

// Returns false for exit. Parsing and domain validation precede book mutation.
inline bool execute(Engine &engine, const std::string &line, std::ostream &out) {
    std::istringstream input(line);
    std::vector<std::string> tokens;
    for (std::string token; input >> token;) {
        tokens.push_back(token);
    }

    if (tokens.empty()) {
        return true;
    }
    if (tokens == std::vector<std::string>{"exit"}) {
        return false;
    }
    if (tokens == std::vector<std::string>{"help"}) {
        out << "limit buy|sell PRICE QTY\n"
               "market buy|sell QTY\n"
               "peg bid|offer buy|sell QTY\n"
               "cancel order ID\n"
               "amend order ID PRICE|- QTY|-\n"
               "print book\nexit\n";
        return true;
    }
    if (tokens == std::vector<std::string>{"print", "book"}) {
        print_book(engine, out);
        return true;
    }

    Result result;
    if (tokens[0] == "limit" && tokens.size() == 4) {
        const auto side = parse_side(tokens[1]);
        const auto price = parse_price(tokens[2]);
        const auto qty = parse_qty(tokens[3]);
        result = engine.limit(side, price, qty);
        out << "Order created: " << name(side) << ' ' << qty << " @ " << format_price(price) << ' '
            << result.id << '\n';
    } else if (tokens[0] == "market" && tokens.size() == 3) {
        const auto side = parse_side(tokens[1]);
        const auto qty = parse_qty(tokens[2]);
        result = engine.market(side, qty);
    } else if (tokens[0] == "peg" && tokens.size() == 4) {
        if (tokens[1] != "bid" && tokens[1] != "offer") {
            throw std::invalid_argument("reference must be bid or offer");
        }
        const auto kind = tokens[1] == "bid" ? Kind::PegBid : Kind::PegOffer;
        const auto side = parse_side(tokens[2]);
        const auto qty = parse_qty(tokens[3]);
        result = engine.peg(kind, side, qty);
        out << "Order created: peg " << tokens[1] << ' ' << name(side) << ' ' << qty << ' '
            << result.id << '\n';
    } else if (tokens.size() == 3 && tokens[0] == "cancel" && tokens[1] == "order") {
        result = engine.cancel(parse_id(tokens[2]));
        out << "Order cancelled\n";
    } else if (tokens.size() == 5 && tokens[0] == "amend" && tokens[1] == "order") {
        const auto id = parse_id(tokens[2]);
        const auto price = tokens[3] == "-" ? std::optional<Price>{} : parse_price(tokens[3]);
        const auto qty = tokens[4] == "-" ? std::optional<Qty>{} : parse_qty(tokens[4]);
        result = engine.amend(id, price, qty);
        out << "Order amended\n";
    } else {
        throw std::invalid_argument("invalid command; type help");
    }
    print_trades(result, out);
    return true;
}

inline int run_session(std::istream &input, std::ostream &output, std::ostream &errors) {
    Engine engine;
    for (std::string line; std::getline(input, line);) {
        try {
            if (!execute(engine, line, output)) {
                break;
            }
        } catch (const std::invalid_argument &error) {
            errors << "Error: " << error.what() << '\n';
        }
    }
    if (input.bad() || (input.fail() && !input.eof())) {
        errors << "Error: input failure\n";
        return 1;
    }
    return 0;
}

} // namespace matching
