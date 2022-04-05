#pragma once

#include <Arduino.h>

template <class T>
class QueueNode {
public:
    T* element;
    QueueNode* next;
    QueueNode* prev;

    QueueNode(T* element, QueueNode* prev, QueueNode* next) : element(element) {
        this->next = next;
        this->prev = prev;
    };
};

template <class T>
class LinkedQueue {
private:
    uint8_t id = 0;
    int length;
    QueueNode<T>* head;
    QueueNode<T>* tail;
public:
    LinkedQueue(uint8_t id = 0);
    ~LinkedQueue();
    T& First() const;
    T& Last() const;
    int getLength();
    void Append(T*);
    T* Pop();
    void Clear();
};

template <class T>
LinkedQueue<T>::LinkedQueue(uint8_t id) {
    length = 0;
    head = nullptr;
    tail = nullptr;
    id = id;
}

template <class T>
LinkedQueue<T>::~LinkedQueue() {
    Clear();
}

template<class T>
T& LinkedQueue<T>::First() const {
    return head->element;
}

template<class T>
T& LinkedQueue<T>::Last() const {
    return tail->element;
}

template<class T>
int LinkedQueue<T>::getLength() {
    return length;
}

template <class T>
void LinkedQueue<T>::Append(T* element) {
    QueueNode<T>* node = new QueueNode<T>(element, tail, nullptr);

    if (length == 0)
        tail = head = node;
    else {
        tail->next = node;
        tail = node;
    }

    length++;
}

template <class T>
T* LinkedQueue<T>::Pop() {
    if (length == 0)
        return nullptr;

    T* elem = head->element;
    head = head->next;
    delete head;

    length--;
    return elem;
}

template <class T>
void LinkedQueue<T>::Clear() {
    if (length == 0)
        return;
    QueueNode<T>* temp = head;

    while (temp != nullptr) {
        head = head->next;
        delete temp->element;
        delete temp;
        temp = head;
    }

    head = tail = nullptr;

    length = 0;
}