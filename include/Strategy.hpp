#pragma once
#include "Common.hpp"
#include "Book.hpp"
#include "Portfolio.hpp"

class Strategy {
public:
    virtual ~Strategy() = default;

    // Review: This virtual function is declared but never defined, which leaves Strategy with unresolved virtual symbols at link time. Make it pure virtual or provide a base implementation.
    virtual pair<optional<vector<OrderCommand>>, Time> onTimeMove(const Time& now, const Book& book, const Portfolio& portfolio, const vector<OrderEvent>& recent_events);
};
