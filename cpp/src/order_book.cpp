#include "quant_engine/order_book.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace quant_engine {

void OrderBook::reserve_capacity(const std::size_t expected_active_orders,
                                 const std::size_t expected_completed_orders,
                                 const std::size_t expected_trades,
                                 const std::size_t expected_orders_per_level) {
    active_orders_.reserve(expected_active_orders);
    completed_orders_.reserve(expected_completed_orders);
    trades_.reserve(expected_trades);
    expected_orders_per_level_ = expected_orders_per_level;
}

std::vector<Trade> OrderBook::add_limit_order(
    const OrderId order_id, const Side side, const Price price,
    const Quantity quantity, const Timestamp timestamp) {
    if (active_orders_.contains(order_id) || completed_orders_.contains(order_id)) {
        throw std::invalid_argument("order id has already been used");
    }

    auto order = std::make_unique<Order>(order_id, side, OrderType::Limit, price,
                                         quantity, next_order_sequence_, timestamp);
    ++next_order_sequence_;
    order->activate();
    Order& incoming = *order;
    active_orders_.emplace(order_id, ActiveOrder{std::move(order), nullptr});

    std::vector<Trade> generated;
    if (side == Side::Buy) {
        match_buy(incoming, generated);
    } else {
        match_sell(incoming, generated);
    }

    if (incoming.remaining_quantity() == 0) {
        complete_order(order_id);
    } else {
        rest(incoming);
    }
    return generated;
}

std::vector<Trade> OrderBook::add_market_order(
    const OrderId order_id, const Side side, const Quantity quantity,
    const Timestamp timestamp) {
    if (active_orders_.contains(order_id) || completed_orders_.contains(order_id)) {
        throw std::invalid_argument("order id has already been used");
    }

    auto order = std::make_unique<Order>(order_id, side, OrderType::Market, 0,
                                         quantity, next_order_sequence_, timestamp);
    ++next_order_sequence_;
    order->activate();
    Order& incoming = *order;
    active_orders_.emplace(order_id, ActiveOrder{std::move(order), nullptr});

    std::vector<Trade> generated;
    if (side == Side::Buy) {
        match_buy(incoming, generated);
    } else {
        match_sell(incoming, generated);
    }
    if (incoming.remaining_quantity() > 0) {
        incoming.cancel();
    }
    complete_order(order_id);
    return generated;
}

void OrderBook::match_buy(Order& incoming, std::vector<Trade>& generated) {
    while (incoming.remaining_quantity() > 0 && !asks_.empty()) {
        auto level_iterator = asks_.begin();
        if (incoming.type() == OrderType::Limit &&
            level_iterator->first > incoming.price()) {
            break;
        }
        PriceLevel& level = level_iterator->second;
        while (incoming.remaining_quantity() > 0 && !level.empty()) {
            Order& resting = level.front();
            const Quantity quantity =
                std::min(incoming.remaining_quantity(), resting.remaining_quantity());
            const Price execution_price = resting.price();
            const OrderId resting_id = resting.order_id();

            level.fill_front(quantity, execution_price);
            incoming.fill(quantity, execution_price);
            record_trade(incoming, resting, execution_price, quantity, generated);

            if (resting.remaining_quantity() == 0) {
                static_cast<void>(level.remove_order(resting_id));
                complete_order(resting_id);
            }
        }
        if (level.empty()) {
            asks_.erase(level_iterator);
        }
    }
}

void OrderBook::match_sell(Order& incoming, std::vector<Trade>& generated) {
    while (incoming.remaining_quantity() > 0 && !bids_.empty()) {
        auto level_iterator = bids_.begin();
        if (incoming.type() == OrderType::Limit &&
            level_iterator->first < incoming.price()) {
            break;
        }
        PriceLevel& level = level_iterator->second;
        while (incoming.remaining_quantity() > 0 && !level.empty()) {
            Order& resting = level.front();
            const Quantity quantity =
                std::min(incoming.remaining_quantity(), resting.remaining_quantity());
            const Price execution_price = resting.price();
            const OrderId resting_id = resting.order_id();

            level.fill_front(quantity, execution_price);
            incoming.fill(quantity, execution_price);
            record_trade(incoming, resting, execution_price, quantity, generated);

            if (resting.remaining_quantity() == 0) {
                static_cast<void>(level.remove_order(resting_id));
                complete_order(resting_id);
            }
        }
        if (level.empty()) {
            bids_.erase(level_iterator);
        }
    }
}

void OrderBook::rest(Order& order) {
    if (order.side() == Side::Buy) {
        auto [iterator, inserted] = bids_.try_emplace(order.price(), order.price());
        if (inserted && expected_orders_per_level_ > 0) {
            iterator->second.reserve(expected_orders_per_level_);
        }
        iterator->second.add_order(order);
        active_orders_.at(order.order_id()).level = &iterator->second;
        return;
    }
    auto [iterator, inserted] = asks_.try_emplace(order.price(), order.price());
    if (inserted && expected_orders_per_level_ > 0) {
        iterator->second.reserve(expected_orders_per_level_);
    }
    iterator->second.add_order(order);
    active_orders_.at(order.order_id()).level = &iterator->second;
}

void OrderBook::remove_from_level(Order& order) {
    ActiveOrder& active = active_orders_.at(order.order_id());
    PriceLevel* level = active.level;
    if (level == nullptr || !level->remove_order(order.order_id())) {
        throw std::logic_error("active order is missing from its price level");
    }
    active.level = nullptr;
    if (!level->empty()) {
        return;
    }
    if (order.side() == Side::Buy) {
        bids_.erase(order.price());
    } else {
        asks_.erase(order.price());
    }
}

void OrderBook::complete_order(const OrderId order_id) {
    auto node = active_orders_.extract(order_id);
    if (node.empty()) {
        throw std::logic_error("cannot complete an order that is not active");
    }
    completed_orders_.emplace(order_id, std::move(*node.mapped().order));
}

void OrderBook::record_trade(Order& incoming, Order& resting,
                             const Price execution_price,
                             const Quantity quantity,
                             std::vector<Trade>& generated) {
    const OrderId buy_order_id = incoming.side() == Side::Buy
                                     ? incoming.order_id()
                                     : resting.order_id();
    const OrderId sell_order_id = incoming.side() == Side::Sell
                                      ? incoming.order_id()
                                      : resting.order_id();
    const Trade trade{
        .trade_id = next_trade_sequence_,
        .buy_order_id = buy_order_id,
        .sell_order_id = sell_order_id,
        .price = execution_price,
        .quantity = quantity,
        .sequence = next_trade_sequence_,
    };
    ++next_trade_sequence_;
    trades_.push_back(trade);
    generated.push_back(trade);
}

bool OrderBook::cancel_order(const OrderId order_id) {
    const auto found = active_orders_.find(order_id);
    if (found == active_orders_.end()) {
        return false;
    }
    Order& order = *found->second.order;
    remove_from_level(order);
    order.cancel();
    complete_order(order_id);
    return true;
}

ReplaceOutcome OrderBook::replace_order(const OrderId order_id,
                                        const Price new_price,
                                        const Quantity new_total_quantity) {
    const auto found = active_orders_.find(order_id);
    if (found == active_orders_.end()) {
        throw std::invalid_argument("replacement order is not active");
    }
    Order& order = *found->second.order;
    if (order.type() != OrderType::Limit || new_price <= 0 ||
        new_total_quantity < order.filled_quantity()) {
        throw std::invalid_argument("invalid replacement");
    }
    if (new_price == order.price() &&
        new_total_quantity == order.original_quantity()) {
        throw std::invalid_argument("replacement does not change the order");
    }

    const bool priority_preserved =
        new_price == order.price() &&
        new_total_quantity < order.original_quantity();
    if (priority_preserved && new_total_quantity > order.filled_quantity()) {
        found->second.level->modify_quantity(order, new_total_quantity);
        return ReplaceOutcome{{}, true};
    }

    if (new_price == order.price() &&
        new_total_quantity > order.original_quantity()) {
        PriceLevel* level = found->second.level;
        if (level == nullptr || !level->remove_order(order_id)) {
            throw std::logic_error("active order is missing from its price level");
        }
        found->second.level = nullptr;
        order.replace(new_price, new_total_quantity, next_order_sequence_++);
        level->add_order(order);
        found->second.level = level;
        return ReplaceOutcome{{}, false};
    }

    remove_from_level(order);
    const SequenceNumber replacement_sequence =
        priority_preserved ? order.sequence() : next_order_sequence_++;
    order.replace(new_price, new_total_quantity, replacement_sequence);
    if (!order.is_active()) {
        complete_order(order_id);
        return ReplaceOutcome{{}, priority_preserved};
    }

    std::vector<Trade> generated;
    if (order.side() == Side::Buy) {
        match_buy(order, generated);
    } else {
        match_sell(order, generated);
    }
    if (order.remaining_quantity() == 0) {
        complete_order(order_id);
    } else {
        rest(order);
    }
    return ReplaceOutcome{std::move(generated), priority_preserved};
}

std::optional<Price> OrderBook::best_bid() const noexcept {
    if (bids_.empty()) {
        return std::nullopt;
    }
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::best_ask() const noexcept {
    if (asks_.empty()) {
        return std::nullopt;
    }
    return asks_.begin()->first;
}

std::optional<Price> OrderBook::spread() const noexcept {
    if (bids_.empty() || asks_.empty()) {
        return std::nullopt;
    }
    return asks_.begin()->first - bids_.begin()->first;
}

std::optional<double> OrderBook::mid_price() const noexcept {
    if (bids_.empty() || asks_.empty()) {
        return std::nullopt;
    }
    return static_cast<double>(bids_.begin()->first) / 2.0 +
           static_cast<double>(asks_.begin()->first) / 2.0;
}

bool OrderBook::is_crossed() const noexcept {
    return !bids_.empty() && !asks_.empty() &&
           bids_.begin()->first >= asks_.begin()->first;
}

const Order* OrderBook::find_active_order(const OrderId order_id) const noexcept {
    const auto found = active_orders_.find(order_id);
    return found == active_orders_.end() ? nullptr : found->second.order.get();
}

std::optional<OrderView> OrderBook::get_order(const OrderId order_id) const {
    const Order* order = find_order(order_id);
    if (order == nullptr) {
        return std::nullopt;
    }
    return OrderView{
        .order_id = order->order_id(),
        .side = order->side(),
        .type = order->type(),
        .price = order->type() == OrderType::Limit
                     ? std::optional<Price>{order->price()}
                     : std::nullopt,
        .original_quantity = order->original_quantity(),
        .executed_quantity = order->filled_quantity(),
        .remaining_quantity = order->remaining_quantity(),
        .sequence = order->sequence(),
        .status = order->status(),
    };
}

bool OrderBook::has_used_order_id(const OrderId order_id) const noexcept {
    return active_orders_.contains(order_id) || completed_orders_.contains(order_id);
}

const Order* OrderBook::find_order(const OrderId order_id) const noexcept {
    if (const Order* active = find_active_order(order_id); active != nullptr) {
        return active;
    }
    const auto completed = completed_orders_.find(order_id);
    return completed == completed_orders_.end() ? nullptr : &completed->second;
}

BookSnapshot OrderBook::snapshot() const {
    BookSnapshot result;
    result.bids.reserve(bids_.size());
    result.asks.reserve(asks_.size());
    for (const auto& [price, level] : bids_) {
        result.bids.push_back(LevelSnapshot{price, level.total_quantity(), level.size()});
    }
    for (const auto& [price, level] : asks_) {
        result.asks.push_back(LevelSnapshot{price, level.total_quantity(), level.size()});
    }
    return result;
}

std::string OrderBook::to_string() const {
    std::ostringstream output;
    output << "ASK\n";
    for (auto iterator = asks_.rbegin(); iterator != asks_.rend(); ++iterator) {
        output << iterator->first << " | " << iterator->second.total_quantity() << '\n';
    }
    output << "----------------\nBID\n";
    for (const auto& [price, level] : bids_) {
        output << price << " | " << level.total_quantity() << '\n';
    }
    return output.str();
}

bool OrderBook::validate_invariants() const noexcept {
    if (is_crossed()) {
        return false;
    }
    std::size_t book_order_count = 0;
    const auto validate_side = [&](const auto& levels, const Side side) {
        for (const auto& [price, level] : levels) {
            if (price <= 0 || level.empty() || !level.validate_invariants()) {
                return false;
            }
            book_order_count += level.size();
            for (const OrderId id : level.order_ids()) {
                const auto active = active_orders_.find(id);
                if (active == active_orders_.end() || active->second.order == nullptr ||
                    active->second.level != &level ||
                    active->second.order->side() != side ||
                    active->second.order->type() != OrderType::Limit) {
                    return false;
                }
            }
        }
        return true;
    };
    if (!validate_side(bids_, Side::Buy) || !validate_side(asks_, Side::Sell) ||
        book_order_count != active_orders_.size()) {
        return false;
    }
    for (const auto& [id, active] : active_orders_) {
        if (active.order == nullptr || active.level == nullptr ||
            !active.order->is_active() || active.order->remaining_quantity() == 0 ||
            !active.level->contains(id)) {
            return false;
        }
    }
    return true;
}

}  // namespace quant_engine
