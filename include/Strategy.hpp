#pragma once
#include "Common.hpp"
#include "Book.hpp"
#include "Portfolio.hpp"

class Strategy {
public:
    virtual ~Strategy() = default;

    virtual pair<optional<vector<OrderCommand>>, Time> onTimeMove(const Time& now, const Book& book, const Portfolio& portfolio, const vector<OrderEvent>& recent_events);
};