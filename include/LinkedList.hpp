#pragma once

template<typename T>
class LinkedList {
    public:
        ///node structure////
        struct LinkedListNode {
            T order;
            LinkedListNode* next;
            LinkedListNode* prev;
        };
        /// iterator ////
        class iterator {
            LinkedListNode* current;

            public:
                iterator() : current(nullptr) {}
                iterator(LinkedListNode* node) : current(node) {}

                iterator& operator++() {
                    current = current->next;
                    return *this;
                }

                iterator& operator--() {
                    current = current->prev;
                    return *this;
                }

                T& operator*() {
                    return current->order;
                }

                bool operator!=(const iterator& other) const {
                    return current != other.current;
                }

                friend class LinkedList<T>;
        };
        /////implementation////
        LinkedListNode head{};
        LinkedListNode tail{};

        LinkedList() {
            head.next = &tail;
            head.prev = nullptr;
            tail.prev = &head;
            tail.next = nullptr;
        }

        void push_back(const T& order) {
            LinkedListNode* newNode = new LinkedListNode{order, &tail, tail.prev};
            tail.prev->next = newNode;
            tail.prev = newNode;
        }

        void erase(iterator it) {
            LinkedListNode* node = it.current;
            if (node == &head || node == &tail) {
                return;
            }

            node->prev->next = node->next;
            node->next->prev = node->prev;
            delete node;
        }

        void clear() {
            LinkedListNode* current = head.next;
            while (current != &tail) {
                LinkedListNode* nextNode = current->next;
                delete current;
                current = nextNode;
            }
            head.next = &tail;
            tail.prev = &head;
        }

        bool empty() const {
            return head.next == &tail;
        }

        iterator end() const {
            return iterator(const_cast<LinkedListNode*>(&tail));
        }

        iterator begin() const {
            return iterator(const_cast<LinkedListNode*>(head.next));
        }
};