#include "Book.hpp"
using namespace std;

Book::Book() = default;

double Book::bestBid() const {
    if (bidLevels_.empty()) {
        return 0.0;
    }
    return bidLevels_.rbegin()->first;
}

double Book::bestAsk() const {
    if (askLevels_.empty()) {
        return 0.0;
    }
    return askLevels_.begin()->first;
}

vector<Order> Book::getOpenOrders() const{
    vector<Order> orders;
    orders.reserve(bidLevels_.size() + askLevels_.size());

    for (const auto& [price, level] : bidLevels_) {
        for (const auto& order : level.orders) {
            orders.push_back(order);
        }
    }

    for (const auto& [price, level] : askLevels_) {
        for (const auto& order : level.orders) {
            orders.push_back(order);
        }
    }

    return orders;
}

bool Book::addOrder(const Order& order, vector<Fill>* fills) {
    if (order.quantity <= 0) {
        return false;
    }

    if (order.side == Side::Buy) {
        bidLevels_[order.limit_price].orders.push_back(order);
        orderMap_[order.id] = {true, bidLevels_.find(order.limit_price), --bidLevels_[order.limit_price].orders.end()};
    } else {
        askLevels_[order.limit_price].orders.push_back(order);
        orderMap_[order.id] = {false, askLevels_.find(order.limit_price), --askLevels_[order.limit_price].orders.end()};
    }

    vector<Fill> generatedFills = checkCross(0, order);
    if (fills != nullptr) {
        *fills = std::move(generatedFills);
    }
    return true;
}

bool Book::modifyOrder(int orderId, Price new_limit_price, int new_quantity, vector<Fill>* fills) {
    if (new_quantity <= 0) {
        return cancelOrder(orderId);
    }

    auto it = orderMap_.find(orderId);
    if (it == orderMap_.end()) {
        return false;
    }

    OrderLocation& loc = it->second;
    const Order oldOrder = *(loc.orderIt);
    Order updatedOrder = oldOrder;

    auto eraseFromLevel = [&](auto& levels) {
        auto levelIt = loc.levelIt;
        auto& level = levelIt->second;
        level.orders.erase(loc.orderIt);
        if (level.orders.empty()) {
            levels.erase(levelIt);
        }
    };

    if (loc.isBid) {
        eraseFromLevel(bidLevels_);
    } else {
        eraseFromLevel(askLevels_);
    }

    updatedOrder.limit_price = new_limit_price;
    updatedOrder.quantity = new_quantity;

    if (loc.isBid) {
        bidLevels_[new_limit_price].orders.push_back(updatedOrder);
        loc.levelIt = bidLevels_.find(new_limit_price);
        loc.orderIt = --bidLevels_[new_limit_price].orders.end();
    } else {
        askLevels_[new_limit_price].orders.push_back(updatedOrder);
        loc.levelIt = askLevels_.find(new_limit_price);
        loc.orderIt = --askLevels_[new_limit_price].orders.end();
    }

    vector<Fill> generatedFills = checkCross(0, updatedOrder);
    if (fills != nullptr) {
        *fills = std::move(generatedFills);
    }
    return true;
}

bool Book::cancelOrder(int orderId) {
    auto it = orderMap_.find(orderId);
    if (it == orderMap_.end()) {
        return false;
    }
    OrderLocation& loc = it->second;
    if (loc.isBid) {
        auto levelIt = loc.levelIt;
        auto& level = levelIt->second;
        level.orders.erase(loc.orderIt);
        if (level.orders.empty()) {
            bidLevels_.erase(levelIt);
        }
    } else {
        auto levelIt = loc.levelIt;
        auto& level = levelIt->second;
        level.orders.erase(loc.orderIt);
        if (level.orders.empty()) {
            askLevels_.erase(levelIt);
        }
    }
    orderMap_.erase(it);
    return true;
}

vector<Fill> Book::checkCross(Time now, Order order){
    if (order.quantity <= 0) {
        return {};
    }

    struct PendingModify {
        OrderId id;
        Price price;
        int quantity;
    };

    vector<Fill> ans;
    vector<PendingModify> pendingModifies;
    vector<OrderId> pendingCancels;

    auto removeOrderFromBook = [&](int orderId) {
        auto it = orderMap_.find(orderId);
        if (it == orderMap_.end()) {
            return;
        }

        OrderLocation& loc = it->second;
        auto levelIt = loc.levelIt;
        auto& level = levelIt->second;
        level.orders.erase(loc.orderIt);
        if (level.orders.empty()) {
            if (loc.isBid) {
                bidLevels_.erase(levelIt);
            } else {
                askLevels_.erase(levelIt);
            }
        }
        orderMap_.erase(it);
    };

    auto applyPendingChanges = [&]() {
        for (const auto& pending : pendingModifies) {
            auto it = orderMap_.find(pending.id);
            if (it == orderMap_.end()) {
                continue;
            }

            OrderLocation& loc = it->second;
            const Order oldOrder = *loc.orderIt;
            auto levelIt = loc.levelIt;
            auto& level = levelIt->second;
            level.orders.erase(loc.orderIt);
            if (level.orders.empty()) {
                if (loc.isBid) {
                    bidLevels_.erase(levelIt);
                } else {
                    askLevels_.erase(levelIt);
                }
            }

            Order updatedOrder = oldOrder;
            updatedOrder.limit_price = pending.price;
            updatedOrder.quantity = pending.quantity;

            if (loc.isBid) {
                bidLevels_[updatedOrder.limit_price].orders.push_back(updatedOrder);
                loc.levelIt = bidLevels_.find(updatedOrder.limit_price);
                loc.orderIt = --bidLevels_[updatedOrder.limit_price].orders.end();
            } else {
                askLevels_[updatedOrder.limit_price].orders.push_back(updatedOrder);
                loc.levelIt = askLevels_.find(updatedOrder.limit_price);
                loc.orderIt = --askLevels_[updatedOrder.limit_price].orders.end();
            }
        }
        for (const auto& id : pendingCancels) {
            removeOrderFromBook(id);
        }
    };

    auto updateIncomingOrderInBook = [&](int newQuantity) {
        if (newQuantity <= 0) {
            cancelOrder(order.id);
            return;
        }

        auto incomingIt = orderMap_.find(order.id);
        if (incomingIt == orderMap_.end()) {
            return;
        }

        OrderLocation& loc = incomingIt->second;
        const Order oldOrder = *loc.orderIt;

        if (loc.isBid) {
            auto levelIt = loc.levelIt;
            auto& level = levelIt->second;
            level.orders.erase(loc.orderIt);
            if (level.orders.empty()) {
                bidLevels_.erase(levelIt);
            }
        } else {
            auto levelIt = loc.levelIt;
            auto& level = levelIt->second;
            level.orders.erase(loc.orderIt);
            if (level.orders.empty()) {
                askLevels_.erase(levelIt);
            }
        }

        Order updatedOrder = oldOrder;
        updatedOrder.quantity = newQuantity;

        if (loc.isBid) {
            bidLevels_[updatedOrder.limit_price].orders.push_back(updatedOrder);
            loc.levelIt = bidLevels_.find(updatedOrder.limit_price);
            loc.orderIt = --bidLevels_[updatedOrder.limit_price].orders.end();
        } else {
            askLevels_[updatedOrder.limit_price].orders.push_back(updatedOrder);
            loc.levelIt = askLevels_.find(updatedOrder.limit_price);
            loc.orderIt = --askLevels_[updatedOrder.limit_price].orders.end();
        }
    };

    if (order.side==Side::Buy){
        for (auto it = askLevels_.begin(); it != askLevels_.end(); ++it) {
            double price = it->first;
            BookLevel& level = it->second;
            if(price > order.limit_price) {
                break;
            }
            for(const auto& elem: level.orders){
                if(elem.quantity <= order.quantity){
                    Fill fill{now, elem.id, elem.owner_id, elem.side, elem.quantity, price};
                    Fill fill1{now, order.id, order.owner_id, order.side, elem.quantity, price};
                    ans.push_back(fill);
                    ans.push_back(fill1);
                    order.quantity -= elem.quantity;
                    pendingCancels.push_back(elem.id);
                    if(order.quantity == 0) {
                        break;
                    }
                } else {
                    const int fillQty = order.quantity;
                    Fill fill{now, elem.id, elem.owner_id, elem.side, fillQty, price};
                    Fill fill1{now, order.id, order.owner_id, order.side, fillQty, price};
                    ans.push_back(fill);
                    ans.push_back(fill1);
                    pendingModifies.push_back({elem.id, price, elem.quantity - fillQty});
                    order.quantity = 0;
                    break;
                }
                if(order.quantity == 0) {
                    break;
                }
            }
            if(order.quantity == 0) {
                break;
            }
        }
        applyPendingChanges();
        if (order.quantity > 0) {
            updateIncomingOrderInBook(order.quantity);
        } else {
            cancelOrder(order.id);
        }
        return ans;
    }else{
        for (auto it = bidLevels_.rbegin(); it != bidLevels_.rend(); ++it) {
            double price = it->first;
            BookLevel& level = it->second;
            if(price < order.limit_price) {
                break;
            }
            for(const auto& elem: level.orders){
                if(elem.quantity <= order.quantity){
                    Fill fill{now, elem.id, elem.owner_id, elem.side, elem.quantity, price};
                    Fill fill1{now, order.id, order.owner_id, order.side, elem.quantity, price};
                    ans.push_back(fill);
                    ans.push_back(fill1);
                    order.quantity -= elem.quantity;
                    pendingCancels.push_back(elem.id);
                    if(order.quantity == 0) {
                        break;
                    }
                } else {
                    const int fillQty = order.quantity;
                    Fill fill{now, elem.id, elem.owner_id, elem.side, fillQty, price};
                    Fill fill1{now, order.id, order.owner_id, order.side, fillQty, price};
                    ans.push_back(fill);
                    ans.push_back(fill1);
                    pendingModifies.push_back({elem.id, price, elem.quantity - fillQty});
                    order.quantity = 0;
                    break;
                }
                if(order.quantity == 0) {
                    break;
                }
            }
            if(order.quantity == 0) {
                break;
            }
        }

        applyPendingChanges();
        if (order.quantity > 0) {
            updateIncomingOrderInBook(order.quantity);
        } else {
            cancelOrder(order.id);
        }
        return ans;
    }
}

