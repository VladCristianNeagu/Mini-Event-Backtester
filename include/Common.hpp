#pragma once
#include <map>
#include <set>
#include <vector>

using namespace std;

using Time = long long;
using OrderId = int;
using OwnerId = int;
using Price = double;

inline constexpr Price fee_rate = 0.0002;

enum class Side {
    Buy,
    Sell
};

struct Order {
    OrderId id;
    int owner_id;
    Time ts;
    Side side;
    int quantity;
    Price limit_price;

    bool operator<(const Order& other) const {
        if (ts != other.ts) {
            return ts < other.ts;
        }
        if (quantity != other.quantity) {
            return quantity < other.quantity;
        }
        return id < other.id;
    }
};

struct CompareById {
    bool operator()(const Order& a, const Order& b) const {
        return a.id < b.id;
    }
};

struct Fill {
    Time ts;
    OrderId order_id;
    OwnerId owner_id;
    Side side;
    int quantity;
    Price price;
};

struct AddOrder {
    Time sent_ts;
    Order order;
};

struct ModifyOrder {
    Time sent_ts;
    OrderId order_id;
    int new_quantity;
    Price new_limit_price;
};

struct CancelOrder {
    Time sent_ts;
    OrderId order_id;
};

enum class OrderCommandType {
    AddOrder,
    CancelOrder,
    ModifyOrder
};

struct OrderCommand {
    OrderCommandType type;
    Time ts = -1;
    OwnerId owner_id = 0;
    Side side = Side::Buy;
    int quantity = 0;
    Price limit_price = 0.0;

    OrderId order_id = 0;
    int new_quantity = 0;
    Price new_limit_price = 0.0;

    bool operator<(const OrderCommand& other) const {
        if (ts != other.ts) {
            return ts < other.ts;
        }
        if (quantity != other.quantity) {
            return quantity < other.quantity;
        }
        return order_id < other.order_id;
    }
};

enum class OrderEventType {
    OrderAccepted,
    OrderRejected,
    OrderFilled,
    OrderCancelled,
    OrderModified,
    OrderCancelFailed,
    OrderModifyFailed
};

struct OrderEvent {
    Time ts;
    OrderEventType type;

    OwnerId owner_id = 0;
    OrderId order_id = 0;
    Side side = Side::Buy;

    int quantity = 0;
    int remaining_quantity = 0;

    Price price = 0.0;
};
