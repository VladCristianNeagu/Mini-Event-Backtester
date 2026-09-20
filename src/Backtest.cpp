#include "Backtest.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <numeric>
#include <sstream>
#include <chrono>
using namespace std;


Backtest::Backtest(string& historicalDataPath,Book& book,Strategy& strategy,Time startTime,Time endTime)
    : historicalDataPath_(historicalDataPath),book_(book),strategy_(strategy),startTime_(startTime),endTime_(endTime) {
        load_historical_data(historicalDataPath);
    }

void Backtest::load_historical_data(string historicalDataPath) {
    ifstream file(historicalDataPath);

    if (!file.is_open()) {
        throw runtime_error("Failed to open historical data file.");
    }

    string line;

    while (getline(file, line)) {
        stringstream ss(line);
        string cell;

        OrderCommand command;

        // Time
        getline(ss, cell, ',');
        // Review: command.ts is a long long, so assigning stod(cell) truncates fractional seconds before multiplying to nanoseconds.
        command.ts = stod(cell);
        command.ts=command.ts*1e9; // convert to nanoseconds

        // Event type
        getline(ss, cell, ',');
        int eventType = stoi(cell);

        if (eventType != 1 && eventType != 2 && eventType != 3)
            continue;

        // Order ID
        getline(ss, cell, ',');
        command.order_id = stoll(cell);

        // Size
        getline(ss, cell, ',');
        int size = stoi(cell);

        // Price
        getline(ss, cell, ',');
        command.limit_price = stod(cell)/10000.0; // convert to dollars

        // Direction
        getline(ss, cell, ',');
        command.side = (stoi(cell) == 1 ? Side::Buy : Side::Sell);

        switch (eventType) {
        case 1:
            command.type = OrderCommandType::AddOrder;
            command.quantity = size;
            break;

        case 2:
            command.type = OrderCommandType::ModifyOrder;
            command.new_quantity = size;   // quantity to cancel
            // Review: ModifyOrder later uses command.new_limit_price, but this branch never initializes it from the CSV price.
            break;

        case 3:
            command.type = OrderCommandType::CancelOrder;
            break;
        }
        eventPool_.push(command);
        usedOrderIds_[command.order_id] = true;
    }
}

bool Backtest::AddOrder(Time now, const OrderCommand& command, vector<Fill>* fills) {
    Order order;
    order.owner_id = command.owner_id;
    order.side = command.side;
    order.quantity = command.quantity;
    order.limit_price = command.limit_price;
    order.id = command.order_id;
    order.ts = now;
    bool worked=book_.addOrder(order, fills);
    orderQuantities_[order.id] = order.quantity;
    if(worked && command.owner_id==1)portfolio_.addOrder(order);
    return worked;
}

bool Backtest::ModifyOrder(Time now, const OrderCommand& command, vector<Fill>* fills) {
    int nextQuantity = command.new_quantity;

    if (command.owner_id != 1) {
        const int cancelledQuantity = max(0, command.new_quantity);
        const auto currentQuantityIt = orderQuantities_.find(command.order_id);
        const int currentQuantity = currentQuantityIt != orderQuantities_.end() ? currentQuantityIt->second : 0;
        nextQuantity = max(0, currentQuantity - cancelledQuantity);
    }

    bool worked = book_.modifyOrder(command.order_id, command.new_limit_price, nextQuantity, fills);

    if (worked) {
        orderQuantities_[command.order_id] = nextQuantity;
        if (command.owner_id == 1) {
            portfolio_.modifyOrder(command.order_id, command.new_limit_price, nextQuantity);
        }
    }
    return worked;
}

bool Backtest::CancelOrder(Time now, const OrderCommand& command) {
    bool worked=book_.cancelOrder(command.order_id);
    orderQuantities_.erase(command.order_id);
    if(worked && command.owner_id==1)portfolio_.removeOrder(command.order_id);
    return worked;
}
optional<OrderEvent> Backtest::applyFill(Time now, const Fill& fill) {
    // Review: This block already decrements orderQuantities_; the non-strategy branch below decrements the same fill again, corrupting remaining size.
    auto quantityIt = orderQuantities_.find(fill.order_id);
    if (quantityIt != orderQuantities_.end()) {
        quantityIt->second -= fill.quantity;
        if (quantityIt->second <= 0) {
            orderQuantities_.erase(quantityIt);
        }
    }

    if (fill.owner_id == 1) {
        portfolio_.applyFill(fill);
        const int remainingQuantity = orderQuantities_.count(fill.order_id) ? orderQuantities_[fill.order_id] : 0;
        return optional<OrderEvent>{OrderEvent{now, OrderEventType::OrderFilled, fill.owner_id, fill.order_id, fill.side, fill.quantity, remainingQuantity, fill.price}};
    }

    if (fill.order_id != 0) {
        const auto orderIt = orderQuantities_.find(fill.order_id);
        if (orderIt != orderQuantities_.end()) {
            orderIt->second = max(0, orderIt->second - fill.quantity);
            if (orderIt->second <= 0) {
                orderQuantities_.erase(orderIt);
            }
        }
    }
    return nullopt;
}
void Backtest::scheduleEvent(const OrderCommand& event) {
    eventPool_.push(event);
}
void Backtest::applyOrderCommand(Time now, const OrderCommand& command) {
    switch (command.type) {
        case OrderCommandType::AddOrder: {
            vector<Fill> fills;
            bool worked=AddOrder(now_,command,&fills);
            if(command.owner_id==1){
                recent_order_events_.push_back(OrderEvent{now_, worked?OrderEventType::OrderAccepted:OrderEventType::OrderRejected, command.owner_id, command.order_id, command.side, command.quantity, orderQuantities_[command.order_id], command.limit_price});
            }
            for(const auto& fill:fills){
                Fill normalizedFill = fill;
                if (normalizedFill.order_id == command.order_id && normalizedFill.owner_id == command.owner_id) {
                    normalizedFill.side = command.side;
                }
                auto order_event=applyFill(now_,normalizedFill);
                if(order_event.has_value()){
                    recent_order_events_.push_back(order_event.value());
                }
            }
            break;
        }
        case OrderCommandType::ModifyOrder: {
            vector<Fill> fills;
            bool worked=ModifyOrder(now_,command,&fills);
            if(command.owner_id==1){
                recent_order_events_.push_back(OrderEvent{now_, worked?OrderEventType::OrderModified:OrderEventType::OrderModifyFailed, command.owner_id, command.order_id, command.side, command.new_quantity, orderQuantities_[command.order_id], command.new_limit_price});
            }
            for(const auto& fill:fills){
                Fill normalizedFill = fill;
                if (normalizedFill.order_id == command.order_id && normalizedFill.owner_id == command.owner_id) {
                    normalizedFill.side = command.side;
                }
                auto order_event=applyFill(now_,normalizedFill);
                if(order_event.has_value()){
                    recent_order_events_.push_back(order_event.value());
                }
            }
            break;
        }
        case OrderCommandType::CancelOrder: {
            bool worked=CancelOrder(now_,command);
            if(command.owner_id==1){
                recent_order_events_.push_back(OrderEvent{now_, worked?OrderEventType::OrderCancelled:OrderEventType::OrderCancelFailed, command.owner_id, command.order_id, command.side, command.quantity, orderQuantities_[command.order_id], command.limit_price});
            }
            break;
        }
    }
}
void Backtest::run() {
    cout<<"Started Backtest from "<<startTime_<<" to "<<endTime_<<endl;
    Time when_is_not_busy=0;
    for(Time i=startTime_;i<=endTime_;i=eventPool_.empty()?endTime_+1:eventPool_.top().ts){
        now_=i;
        cout<<"Processing event at time: "<<now_<<endl;
        vector<OrderCommand> currentCommands;
        while(!eventPool_.empty() && eventPool_.top().ts<=now_){
            currentCommands.push_back(eventPool_.top());
            eventPool_.pop();
        }
        for (const auto& command : currentCommands) {
            applyOrderCommand(now_, command);
        }
        if (now_<=when_is_not_busy) {
            continue;
        }
        /////The commands the strategy made and the time it took the strategy to process
        pair<optional<vector<OrderCommand>>, Time> result=strategy_.onTimeMove(now_,book_,portfolio_,recent_order_events_);
        optional<vector<OrderCommand>> new_commands=result.first;
        Time strategy_duration=result.second;
        /*this for research purpose*/ strategyDuration_.push_back(strategy_duration);
        when_is_not_busy=now_ + backtest_to_strategy_latency_ + stategy_to_backtest_latency_ + strategy_duration;
        recent_order_events_.clear();
        if(new_commands.has_value()){
            vector<OrderCommand> delayedCommands;
            delayedCommands.reserve(new_commands->size());
            for(auto& new_command:new_commands.value()){
                new_command.ts = now_ + backtest_to_strategy_latency_ + stategy_to_backtest_latency_ + strategy_duration;
                if (new_command.type == OrderCommandType::AddOrder && new_command.quantity <= 0) {
                    continue;
                }
                if(new_command.type==OrderCommandType::AddOrder){
                    while(usedOrderIds_.find(nextOrderId_)!=usedOrderIds_.end()){
                        nextOrderId_++;
                    }
                    new_command.order_id=nextOrderId_;
                    usedOrderIds_[nextOrderId_]=true;
                }
                delayedCommands.push_back(new_command);
            }
            for (auto& delayedCommand : delayedCommands) {
                eventPool_.push(delayedCommand);
            }
        }
    }
}

void Backtest::printResults() {
    cout << "Final Cash: " << portfolio_.getCash() << endl;
    cout << "Final Position: " << portfolio_.getEffectivePosition() << endl;
    cout << "Best bid: " << book_.bestBid() << endl;
    cout << "Best ask: " << book_.bestAsk() << endl;
    cout << "Portfolio open orders: " << endl;
    cout << "Final estimated money: " << portfolio_.getCash() + portfolio_.getPosition() * book_.bestBid() << endl;
    cout << "Total historical position: " << portfolio_.getTotalHistoricalPosition() << endl;
    cout << "Average strategy duration: " << (strategyDuration_.empty() ? 0 : accumulate(strategyDuration_.begin(), strategyDuration_.end(), 0LL) / strategyDuration_.size()) << " ns" << endl;
    cout << "Median strategy duration: " << (strategyDuration_.empty() ? 0 : *next(strategyDuration_.begin(), strategyDuration_.size() / 2)) << " ns" << endl;
    cout << "90th percentile strategy duration: " << (strategyDuration_.empty() ? 0 : *next(strategyDuration_.begin(), strategyDuration_.size() * 9 / 10)) << " ns" << endl;
    for (const auto& order : portfolio_.getOpenOrders()) {
        cout << "Order ID: " << order.id
             << ", Side: " << (order.side == Side::Buy ? "Buy" : "Sell")
             << ", Quantity: " << order.quantity
             << ", Limit Price: " << order.limit_price
             << endl;
    }

    /*cout << "Book resting orders: " << endl;
    for (const auto& order : book_.getOpenOrders()) {
        cout << "Order ID: " << order.id
             << ", Side: " << (order.side == Side::Buy ? "Buy" : "Sell")
             << ", Quantity: " << order.quantity
             << ", Limit Price: " << order.limit_price
             << endl;
    }*/
    /*cout << "Fill History: " << endl;
    for (const auto& fill : portfolio_.getOrderHistory()) {
        cout << "Time: " << fill.ts
             << ", Order ID: " << fill.order_id
             << ", Side: " << (fill.side == Side::Buy ? "Buy" : "Sell")
             << ", Quantity: " << fill.quantity
             << ", Price: " << fill.price
             << endl;
    }*/
}
