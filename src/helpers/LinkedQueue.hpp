#pragma once

#include <Arduino.h>

template <class T>
class ListNode {
public:
    T* element;
    ListNode* next;
    ListNode* prev;

    ListNode(T* element, ListNode* prev, ListNode* next)
        : element(element), prev(prev), next(next) {
    };

    ~ListNode() {
        delete element;
    }
};

template <class T>
class LinkedList {
private:
    size_t length;
    bool inUse;
    ListNode<T>* head;
    ListNode<T>* tail;
    ListNode<T>* curr;
public:
    LinkedList();
    ~LinkedList();
    T* getCurrent();
    T* First() const;
    T* Last() const;
    size_t getLength();
    void Append(T*);
    bool Search(T*);
    void DeleteCurrent();
    bool next();
    bool moveToStart();
    bool prev();
    void Clear();
    void setInUse();
    void releaseInUse();
};

template <class T>
LinkedList<T>::LinkedList() {
    length = 0;
    inUse = false;
    head = nullptr;
    tail = nullptr;
    curr = nullptr;
}


template <class T>
LinkedList<T>::~LinkedList() {
    Clear();
}

template<class T>
T* LinkedList<T>::getCurrent() {
    return curr->element;
}

template<class T>
T* LinkedList<T>::First() const {
    return head->element;
}

template<class T>
T* LinkedList<T>::Last() const {
    return tail->element;
}

template<class T>
size_t LinkedList<T>::getLength() {
    return length;
}

template <class T>
void LinkedList<T>::Append(T* element) {
    ListNode<T>* node = new ListNode<T>(element, tail, nullptr);

    if (length == 0)
        curr = tail = head = node;
    else {
        tail->next = node;
        tail = node;
    }

    length++;

}

template <class T>
bool LinkedList<T>::Search(T* elem) {
    //TODO: Check if this works fine
    if (length == 0)
        return false;
    if (moveToStart())
        do {
            if (curr->element == elem)
                return true;
        } while (next());
        return false;
}

template <class T>
bool LinkedList<T>::next() {
    if (length == 0)
        return false;

    if (curr->next == nullptr)
        return false;

    curr = curr->next;
    return true;
}

template <class T>
bool LinkedList<T>::moveToStart() {
    curr = head;
    return length != 0;
}

template<class T>
bool LinkedList<T>::prev() {
    if (length == 0)
        return false;

    if (curr->prev != nullptr)
        return false;

    curr = curr->prev;
    return true;
}

template <class T>
void LinkedList<T>::DeleteCurrent() {
    if (length == 0)
        return;
    length--;
    ListNode<T>* temp = curr;

    if (temp->prev != nullptr)
        temp->prev->next = temp->next;
    if (temp->next != nullptr)
        temp->next->prev = temp->prev;

    if (length == 0)
        head = curr = tail = nullptr;
    else if (curr == head)
        curr = head = head->next;
    else if (curr == tail)
        curr = tail = tail->prev;
    else
        curr = curr->prev;

    delete temp;
}

template <class T>
void LinkedList<T>::Clear() {
    if (length == 0)
        return;
    ListNode<T>* temp = head;

    while (temp != nullptr) {
        head = head->next;
        delete temp;
        temp = head;
    }

    head = curr = tail = nullptr;

    length = 0;

}

template <class T>
void LinkedList<T>::setInUse() {
    while (!inUse) {
        vTaskDelay(100);
    }
    inUse = true;
}

template <class T>
void LinkedList<T>::releaseInUse() {
    inUse = false;
}