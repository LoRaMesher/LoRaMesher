#pragma once

//Inspired by: https://stackoverflow.com/questions/9986591/vectors-in-arduino#:~:text=You%20can%20write%20this%20LinkedList%20template%20class%20and%20simply%20call%20it%20wherever%20you%20want%20%3A

#include "BuildOptions.h"

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
    LM_ListNode<T>* head;
    LM_ListNode<T>* tail;
    LM_ListNode<T>* curr;
    SemaphoreHandle_t xSemaphore;
public:
    LM_LinkedList();
    LM_LinkedList(LM_LinkedList<T>& list);
    ~LM_LinkedList();
    T* getCurrent();
    T* First() const;
    T* Last() const;
    T* operator[](int);
    size_t getLength();
    void Append(T*);
    T* Pop();
    void addCurrent(T*);
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
    head = nullptr;
    tail = nullptr;
    curr = nullptr;

    /* Attempt to create a semaphore. */
    xSemaphore = xSemaphoreCreateMutex();

    if (xSemaphore == NULL) {
        ESP_LOGE(LM_TAG, "Semaphore in Linked List not created");
    }
}

template<class T>
inline LM_LinkedList<T>::LM_LinkedList(LM_LinkedList<T>& list) {
    length = 0;
    head = nullptr;
    tail = nullptr;
    curr = nullptr;

    /* Attempt to create a semaphore. */
    xSemaphore = xSemaphoreCreateMutex();

    if (xSemaphore == NULL) {
        ESP_LOGE(LM_TAG, "Semaphore in Linked List not created");
    }


    list.setInUse();

    if (list.moveToStart()) {
        do {
            Append(list.getCurrent());
        } while (list.next());
    }

    list.releaseInUse();
}


template <class T>
LM_LinkedList<T>::~LM_LinkedList() {
    Clear();
    vSemaphoreDelete(xSemaphore);
}

template<class T>
T* LM_LinkedList<T>::getCurrent() {
    return curr ? curr->element : nullptr;
}

template<class T>
T* LM_LinkedList<T>::First() const {
    return head ? head->element : nullptr;
}

template<class T>
T* LM_LinkedList<T>::Last() const {
    return tail ? tail->element : nullptr;
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
void LM_LinkedList<T>::addCurrent(T* element) {
    if (length == 0) {
        Append(element);
        return;
    }

    if (curr->prev == nullptr) {

    }

    LM_ListNode<T>* node = new LM_ListNode<T>(element, curr->prev, curr);

    if (curr->prev != nullptr) {
        curr->prev->next = node;
    }

    curr->prev = node;

    if (curr == head) {
        head = node;
    }

    length++;

}

template<class T>
T* LM_LinkedList<T>::Pop() {
    moveToStart();
    T* element = getCurrent();
    DeleteCurrent();
    return element;
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
    else if (curr == head) {
        curr = head = head->next;
        if (head == nullptr)  // If the new head is nullptr, set tail to nullptr as well
            tail = nullptr;
    }
    else if (curr == tail)
        curr = tail = tail->prev;
    else
        curr = curr->next;

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
    while (xSemaphoreTake(xSemaphore, (TickType_t) 10) != pdTRUE) {
        ESP_LOGW(LM_TAG, "List in Use Alert");
    }
}

template <class T>
void LM_LinkedList<T>::releaseInUse() {
    xSemaphoreGive(xSemaphore);
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