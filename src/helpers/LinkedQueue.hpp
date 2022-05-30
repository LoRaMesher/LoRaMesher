#pragma once

#include <Arduino.h>

template <class T>
class LM_ListNode {
public:
    T* element;
    LM_ListNode* prev;
    LM_ListNode* next;

    LM_ListNode(T* element, LM_ListNode* prev, LM_ListNode* next)
        : element(element), prev(prev), next(next) {
    };
};

template <class T>
class LM_LinkedList {
private:
    size_t length;
    bool inUse;
    LM_ListNode<T>* head;
    LM_ListNode<T>* tail;
    LM_ListNode<T>* curr;
public:
    LM_LinkedList();
    ~LM_LinkedList();
    T* getCurrent();
    T* First() const;
    T* Last() const;
    T* operator[](int);
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
    void each(void (*func)(T*));
};

template <class T>
LM_LinkedList<T>::LM_LinkedList() {
    length = 0;
    inUse = false;
    head = nullptr;
    tail = nullptr;
    curr = nullptr;
}


template <class T>
LM_LinkedList<T>::~LM_LinkedList() {
    Clear();
}

template<class T>
T* LM_LinkedList<T>::getCurrent() {
    return curr->element;
}

template<class T>
T* LM_LinkedList<T>::First() const {
    return head->element;
}

template<class T>
T* LM_LinkedList<T>::Last() const {
    return tail->element;
}

template <class T>
T* LM_LinkedList<T>::operator[](int position) {
    if (position < length && moveToStart()) {
        int i = 0;
        do {
            if (i == position) {
                return curr->element;
            }

            i++;
        } while (next());
    }

    return NULL;
}

template<class T>
size_t LM_LinkedList<T>::getLength() {
    return length;
}

template <class T>
void LM_LinkedList<T>::Append(T* element) {
    LM_ListNode<T>* node = new LM_ListNode<T>(element, tail, nullptr);

    if (length == 0)
        curr = tail = head = node;
    else {
        tail->next = node;
        tail = node;
    }

    length++;

}

template <class T>
bool LM_LinkedList<T>::Search(T* elem) {
    if (moveToStart()) {
        do {
            if (curr->element == elem)
                return true;
        } while (next());
    }

    return false;
}

template <class T>
bool LM_LinkedList<T>::next() {
    if (length == 0)
        return false;

    if (curr->next == nullptr)
        return false;

    curr = curr->next;
    return true;
}

template <class T>
bool LM_LinkedList<T>::moveToStart() {
    curr = head;
    return length != 0;
}

template<class T>
bool LM_LinkedList<T>::prev() {
    if (length == 0)
        return false;

    if (curr->prev != nullptr)
        return false;

    curr = curr->prev;
    return true;
}

template <class T>
void LM_LinkedList<T>::DeleteCurrent() {
    if (length == 0)
        return;
    length--;
    LM_ListNode<T>* temp = curr;

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
void LM_LinkedList<T>::Clear() {
    if (length == 0)
        return;
    LM_ListNode<T>* temp = head;

    while (temp != nullptr) {
        head = head->next;
        delete temp;
        temp = head;
    }

    head = curr = tail = nullptr;

    length = 0;

}

template <class T>
void LM_LinkedList<T>::setInUse() {
    while (inUse) {
        vTaskDelay(100);
    }
    inUse = true;
}

template <class T>
void LM_LinkedList<T>::releaseInUse() {
    inUse = false;
}

template <class T>
void LM_LinkedList<T>::each(void (*func)(T*)) {
    setInUse();

    if (moveToStart()) {
        do {
            func(curr->element);
        } while (next());
    }

    releaseInUse();
}