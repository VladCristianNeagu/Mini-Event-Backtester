#pragma once
#include <list>
#include <vector>
#include <map>
#include <unordered_map>
#include "Common.hpp"
#include "LinkedList.hpp"
using namespace std;
class Book{
    public:
        Book();
        bool addOrder(const Order& order, vector<Fill>* fills = nullptr);
        bool modifyOrder(int orderId, Price new_limit_price, int new_quantity, vector<Fill>* fills = nullptr);
        bool cancelOrder(int orderId);
        Price bestBid() const;
        Price bestAsk() const;
        vector<Order> getOpenOrders() const;
        vector<Fill> checkCross(Time now, Order order);
    private:
        struct BookLevel {
            LinkedList<Order> orders;
        };
        struct OrderLocation {
            bool isBid;
            map<Price, BookLevel>::iterator levelIt;
            LinkedList<Order>::iterator orderIt;
        };
        map<Price, BookLevel> bidLevels_;
        map<Price, BookLevel> askLevels_;
        unordered_map<OrderId, OrderLocation> orderMap_;
};