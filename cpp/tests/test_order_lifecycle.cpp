#include "quant_engine/matching_engine.hpp"

#include "test_support.hpp"

namespace {
using namespace quant_engine;

SubmitResult limit(MatchingEngine& engine, const OrderId id, const Side side,
                   const Price price, const Quantity quantity) {
    return engine.submit(OrderRequest{id, side, OrderType::Limit, quantity, price});
}

void accepted_non_crossing_limit_becomes_active() {
    MatchingEngine engine;
    const auto result = limit(engine, 1, Side::Buy, 100, 10);
    CHECK(result.accepted);
    CHECK(result.report.status == OrderStatus::Active);
    CHECK(engine.book().find_active_order(1) != nullptr);
}

void invalid_submission_becomes_rejected_history() {
    MatchingEngine engine;
    const auto result =
        engine.submit(OrderRequest{1, Side::Buy, OrderType::Limit, 10, std::nullopt});
    CHECK(!result.accepted);
    CHECK(result.reject_reason == RejectReason::MissingLimitPrice);
    CHECK(result.report.status == OrderStatus::Rejected);
    CHECK(engine.get_order(1)->status == OrderStatus::Rejected);
    CHECK(engine.modify(1, ModifyRequest{100, 10}).reject_reason ==
          RejectReason::OrderNotActive);
}

void active_order_can_become_partially_filled_then_filled() {
    MatchingEngine engine;
    CHECK(limit(engine, 1, Side::Sell, 100, 100).accepted);
    static_cast<void>(limit(engine, 2, Side::Buy, 100, 40));
    CHECK(engine.get_order(1)->status == OrderStatus::PartiallyFilled);
    CHECK(engine.get_order(1)->remaining_quantity == 60);
    static_cast<void>(limit(engine, 3, Side::Buy, 100, 60));
    CHECK(engine.get_order(1)->status == OrderStatus::Filled);
    CHECK(engine.get_order(1)->remaining_quantity == 0);
}

void active_order_can_be_cancelled() {
    MatchingEngine engine;
    CHECK(limit(engine, 1, Side::Buy, 100, 10).accepted);
    const auto result = engine.cancel(1);
    CHECK(result.cancelled);
    CHECK(result.report->status == OrderStatus::Cancelled);
    CHECK(engine.book().find_active_order(1) == nullptr);
}

void partially_filled_order_can_be_cancelled() {
    MatchingEngine engine;
    CHECK(limit(engine, 1, Side::Sell, 100, 100).accepted);
    static_cast<void>(limit(engine, 2, Side::Buy, 100, 40));
    const auto result = engine.cancel(1);
    CHECK(result.cancelled);
    CHECK(result.report->status == OrderStatus::Cancelled);
    CHECK(result.report->executed_quantity == 40);
    CHECK(result.report->remaining_quantity == 60);
}

void filled_order_cannot_be_cancelled_again() {
    MatchingEngine engine;
    CHECK(limit(engine, 1, Side::Sell, 100, 10).accepted);
    static_cast<void>(limit(engine, 2, Side::Buy, 100, 10));
    const auto result = engine.cancel(1);
    CHECK(!result.cancelled);
    CHECK(result.reject_reason == RejectReason::OrderNotActive);
    CHECK(result.report->status == OrderStatus::Filled);
}

void second_cancel_is_safe_and_non_mutating() {
    MatchingEngine engine;
    CHECK(limit(engine, 1, Side::Buy, 100, 10).accepted);
    CHECK(engine.cancel(1).cancelled);
    const auto second = engine.cancel(1);
    CHECK(!second.cancelled);
    CHECK(second.reject_reason == RejectReason::OrderNotActive);
    CHECK(engine.get_order(1)->status == OrderStatus::Cancelled);
}

void cancel_unknown_has_machine_readable_reason() {
    MatchingEngine engine;
    const auto result = engine.cancel(999);
    CHECK(!result.cancelled);
    CHECK(result.reject_reason == RejectReason::OrderNotFound);
    CHECK(!result.report.has_value());
}

void final_cancellation_removes_empty_price_level() {
    MatchingEngine engine;
    CHECK(limit(engine, 1, Side::Buy, 100, 10).accepted);
    CHECK(engine.cancel(1).cancelled);
    CHECK(!engine.book().best_bid().has_value());
    CHECK(engine.book().snapshot().bids.empty());
}

void market_orders_are_always_terminal_after_submit() {
    MatchingEngine engine;
    const auto result =
        engine.submit(OrderRequest{1, Side::Sell, OrderType::Market, 100, std::nullopt});
    CHECK(result.accepted);
    CHECK(result.report.status == OrderStatus::Cancelled);
    CHECK(engine.book().find_active_order(1) == nullptr);
    CHECK(engine.modify(1, ModifyRequest{100, 100}).reject_reason ==
          RejectReason::OrderNotActive);
}

}  // namespace

int main() {
    accepted_non_crossing_limit_becomes_active();
    invalid_submission_becomes_rejected_history();
    active_order_can_become_partially_filled_then_filled();
    active_order_can_be_cancelled();
    partially_filled_order_can_be_cancelled();
    filled_order_cannot_be_cancelled_again();
    second_cancel_is_safe_and_non_mutating();
    cancel_unknown_has_machine_readable_reason();
    final_cancellation_removes_empty_price_level();
    market_orders_are_always_terminal_after_submit();
    return test_support::failures == 0 ? 0 : 1;
}
