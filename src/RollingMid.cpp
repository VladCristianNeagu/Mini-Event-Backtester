#include "../include/RollingMid.hpp"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <deque>
#include <iostream>
using namespace std;

constexpr int kMaxPosition = 10;
constexpr int kBaseQuoteQty = 3;
constexpr size_t kHistoryWindow = 24;

Price clampPrice(Price price) {
    return max<Price>(0.0, price);
}

int clampInt(int value, int low, int high) {
    return max(low, min(value, high));
}

Price safeMid(Price bestBid, Price bestAsk, const RunningMean& midMean) {
    if (bestBid > 0.0 && bestAsk > 0.0) {
        return (bestBid + bestAsk) / 2.0;
    }

    if (midMean.size() > 0) {
        return midMean.mean();
    }

    if (bestBid > 0.0) {
        return bestBid * 1.001;
    }

    if (bestAsk > 0.0) {
        return bestAsk * 0.999;
    }

    return 0.0;
}

Price shortTermMean(const deque<Price>& midHistory) {
    if (midHistory.empty()) {
        return 0.0;
    }

    const size_t sampleCount = min<size_t>(5, midHistory.size());
    Price total = 0.0;
    for (size_t i = midHistory.size() - sampleCount; i < midHistory.size(); ++i) {
        total += midHistory[i];
    }

    return total / static_cast<Price>(sampleCount);
}

Price recentVolatility(const deque<Price>& midHistory) {
    if (midHistory.size() < 2) {
        return 0.0;
    }

    Price totalMove = 0.0;
    for (size_t i = 1; i < midHistory.size(); ++i) {
        totalMove += fabs(midHistory[i] - midHistory[i - 1]);
    }

    return totalMove / static_cast<Price>(midHistory.size() - 1);
}

optional<Order> bestOpenOrder(const vector<Order>& openOrders, Side side) {
    optional<Order> best;

    for (const auto& order : openOrders) {
        if (order.side != side) {
            continue;
        }

        if (!best.has_value()) {
            best = order;
            continue;
        }

        if (side == Side::Buy) {
            if (order.limit_price > best->limit_price) {
                best = order;
            }
        } else if (order.limit_price < best->limit_price) {
            best = order;
        }
    }

    return best;
}

OrderCommand makeAddOrder(Side side, int quantity, Price limitPrice) {
    OrderCommand command{};
    command.type = OrderCommandType::AddOrder;
    command.owner_id = 1;
    command.side = side;
    command.quantity = quantity;
    command.limit_price = limitPrice;
    return command;
}

OrderCommand makeCancelOrder(OrderId orderId, Side side) {
    OrderCommand command{};
    command.type = OrderCommandType::CancelOrder;
    command.owner_id = 1;
    command.order_id = orderId;
    command.side = side;
    return command;
}

OrderCommand makeModifyOrder(OrderId orderId, Side side, int quantity, Price limitPrice) {
    OrderCommand command{};
    command.type = OrderCommandType::ModifyOrder;
    command.owner_id = 1;
    command.side = side;
    command.order_id = orderId;
    command.new_quantity = quantity;
    command.new_limit_price = limitPrice;
    return command;
}

int buyCapacityForPosition(int effectivePosition) {
    return max(0, kMaxPosition - effectivePosition);
}

int sellCapacityForPosition(int effectivePosition) {
    return max(0, effectivePosition - 1);
}

int computeEffectivePosition(const Portfolio& portfolio) {
    return portfolio.getEffectivePosition();
}

pair<optional<vector<OrderCommand>>, Time> RollingMid::onTimeMove(const Time& now, const Book& book, const Portfolio& portfolio, const vector<OrderEvent>& recent_events) {
    chrono::high_resolution_clock::time_point start = chrono::high_resolution_clock::now();
    // Alex: Function-local static state is shared by every RollingMid instance and survives after an individual backtest finishes. 
    // Running a second backtest in the same process therefore starts with prices from the first run, contaminating fair value and volatility and making test results depend on execution order.
    static deque<Price> midHistory;
    const Price bestBid = book.bestBid();
    const Price bestAsk = book.bestAsk();
    const Price mid = safeMid(bestBid, bestAsk, midMean_);
    if (mid > 0.0) {
        midMean_.add(mid);
        midHistory.push_back(mid);
        if (midHistory.size() > kHistoryWindow) {
            midHistory.pop_front();
        }
    }

    const auto openOrders = portfolio.getOpenOrders();
    const int position = portfolio.getPosition();
    const int effectivePosition = computeEffectivePosition(portfolio);
    const Price cash = portfolio.getCash();

    const Price shortMean = shortTermMean(midHistory);
    const Price longMean = midMean_.size() >= 3 ? midMean_.mean() : shortMean;
    const Price fairValue = shortMean > 0.0 && longMean > 0.0 ? (0.7 * shortMean + 0.3 * longMean) : max(shortMean, longMean);

    if (fairValue <= 0.0) {
        chrono::high_resolution_clock::time_point end = chrono::high_resolution_clock::now();
        Time duration = chrono::duration_cast<chrono::nanoseconds>(end - start).count();
        return make_pair(vector<OrderCommand>(), duration);
    }

    const Price tick = max<Price>(0.01, fairValue * 0.0001);
    const Price observedSpread = (bestBid > 0.0 && bestAsk > 0.0) ? max<Price>(0.0, bestAsk - bestBid) : fairValue * 0.0015;
    const Price volatility = recentVolatility(midHistory);
    const Price halfSpread = max<Price>(tick * 2.0, max<Price>(observedSpread * 0.28, volatility * 1.75));

    int recentBuyFills = 0;
    int recentSellFills = 0;
    for (const auto& event : recent_events) {
        if (event.owner_id != 1 || event.type != OrderEventType::OrderFilled) {
            continue;
        }

        if (event.side == Side::Buy) {
            recentBuyFills += event.quantity;
        } else {
            recentSellFills += event.quantity;
        }
    }

    const Price inventoryRatio = clamp<Price>(static_cast<Price>(effectivePosition) / static_cast<Price>(kMaxPosition), -1.0, 1.0);
    const Price flowRatio = clamp<Price>(static_cast<Price>(recentBuyFills - recentSellFills) / static_cast<Price>(kMaxPosition), -1.0, 1.0);
    const Price centerShift = (inventoryRatio * 0.9 + flowRatio * 0.4) * halfSpread;

    Price targetBid = clampPrice(fairValue - halfSpread - centerShift);
    Price targetAsk = clampPrice(fairValue + halfSpread - centerShift);

    if (bestBid > 0.0) {
        targetBid = max(targetBid, bestBid + tick);
    }
    if (bestAsk > 0.0) {
        targetAsk = min(targetAsk, bestAsk - tick);
    }

    if (targetBid >= targetAsk) {
        targetBid = clampPrice(fairValue - tick);
        targetAsk = clampPrice(fairValue + tick);
    }

    const int buyCapacity = buyCapacityForPosition(effectivePosition);
    const int sellCapacity = sellCapacityForPosition(effectivePosition);

    const int buyPreference = clampInt(kBaseQuoteQty + max(0, -effectivePosition) / 2 + recentSellFills / 2, 1, kMaxPosition);
    const int sellPreference = clampInt(kBaseQuoteQty + max(0, effectivePosition) / 2 + recentBuyFills / 2, 1, kMaxPosition);

    const Price buyCostPerShare = max<Price>(1.0, targetBid * (1.0 + fee_rate));
    const int cashCapacity = cash > 0.0 && buyCostPerShare > 0.0
        ? static_cast<int>(floor(cash / buyCostPerShare))
        : 0;

    int targetBuyQty = max(0, min({buyCapacity, buyPreference, cashCapacity}));
    int targetSellQty = max(0, min({sellCapacity, sellPreference}));

    if (targetBuyQty == 0 && targetSellQty == 0 && effectivePosition == 0 && cash > 0.0) {
        targetBuyQty = min(kBaseQuoteQty, buyCapacity);
        targetSellQty = min(kBaseQuoteQty, sellCapacity);
    }

    if (effectivePosition > 0) {
        const int reduction = min(targetSellQty, effectivePosition);
        targetSellQty = reduction;
    } else if (effectivePosition < 0) {
        const int reduction = min(targetBuyQty, -effectivePosition);
        targetBuyQty = reduction;
    }

    vector<OrderCommand> commands;
    const auto currentBid = bestOpenOrder(openOrders, Side::Buy);
    const auto currentAsk = bestOpenOrder(openOrders, Side::Sell);

    if (targetBuyQty <= 0) {
        if (currentBid.has_value()) {
            commands.push_back(makeCancelOrder(currentBid->id, Side::Buy));
        }
    } else if (!currentBid.has_value()) {
        commands.push_back(makeAddOrder(Side::Buy, targetBuyQty, targetBid));
    } else if (currentBid->quantity != targetBuyQty || fabs(currentBid->limit_price - targetBid) > tick * 0.75) {
        commands.push_back(makeModifyOrder(currentBid->id, Side::Buy, targetBuyQty, targetBid));
    }

    const bool keepCurrentAsk = currentAsk.has_value()
        && targetSellQty > 0
        && currentAsk->quantity == targetSellQty
        && fabs(currentAsk->limit_price - targetAsk) <= tick * 0.75;

    for (const auto& order : openOrders) {
        if (order.side == Side::Sell && (!keepCurrentAsk || order.id != currentAsk->id)) {
            commands.push_back(makeCancelOrder(order.id, Side::Sell));
        }
    }

    if (targetSellQty > 0 && !keepCurrentAsk) {
        commands.push_back(makeAddOrder(Side::Sell, targetSellQty, targetAsk));
    }
    chrono::high_resolution_clock::time_point end = chrono::high_resolution_clock::now();
    Time duration = chrono::duration_cast<chrono::nanoseconds>(end - start).count();
    if (commands.empty()) {
        return make_pair(vector<OrderCommand>(), duration);
    }
    return make_pair(commands, duration);
}