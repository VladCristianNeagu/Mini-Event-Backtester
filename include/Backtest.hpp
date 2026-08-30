#pragma once
#include <vector>
#include <queue>
#include <optional>
#include <string>
#include <unordered_map>
using namespace std;
#include "Book.hpp"
#include "Portfolio.hpp"
#include "Strategy.hpp"

struct OrderCommandCompare {
    bool operator()(const OrderCommand& lhs, const OrderCommand& rhs) const {
        if (lhs.ts != rhs.ts) {
            return lhs.ts > rhs.ts;
        }
        if (lhs.quantity != rhs.quantity) {
            return lhs.quantity > rhs.quantity;
        }
        return lhs.order_id > rhs.order_id;
    }
};

class Backtest {
    public:
        Backtest(string& historicalDataPath,Book& book,Strategy& strategy,Time startTime,Time endTime);
        void run();
        void printResults();
    private:
        void load_historical_data(string historicalDataPath);
        void scheduleEvent(const OrderCommand& event);

        void applyOrderCommand(Time now, const OrderCommand& command);
        bool AddOrder(Time now,const OrderCommand& command, vector<Fill>* fills = nullptr);
        bool ModifyOrder(Time now,const OrderCommand& command, vector<Fill>* fills = nullptr);
        bool CancelOrder(Time now,const OrderCommand& command);
        optional<OrderEvent> applyFill(Time now,const Fill& fill);

        int backtest_to_strategy_latency_{150000LL}; // 150 microseconds
        int stategy_to_backtest_latency_{200000LL}; // 200 microseconds
        string historicalDataPath_;
        Time now_{0};
        Time startTime_{0};
        Time endTime_{0};
        Book& book_;
        Strategy& strategy_;
        Portfolio portfolio_;
        OrderId nextOrderId_{1};
        priority_queue<OrderCommand, vector<OrderCommand>, OrderCommandCompare> eventPool_;
        vector<OrderEvent> recent_order_events_; 
        unordered_map<OrderId,int> orderQuantities_; //used for the modify command
        unordered_map<OrderId,bool> usedOrderIds_; //used to track if an orderId has been used before
        vector<Time> strategyDuration_; //used to track the duration of each strategy call
};  