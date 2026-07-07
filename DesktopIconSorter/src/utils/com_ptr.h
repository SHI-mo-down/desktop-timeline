// com_ptr.h : Simple COM smart pointer (no ATL dependency)
#pragma once
#include <Windows.h>

template<typename T>
class ComPtr {
public:
    ComPtr() : ptr_(nullptr) {}
    ComPtr(T* p) : ptr_(p) { if (ptr_) ptr_->AddRef(); }
    ~ComPtr() { Release(); }

    ComPtr(const ComPtr& other) : ptr_(other.ptr_) {
        if (ptr_) ptr_->AddRef();
    }

    ComPtr& operator=(const ComPtr& other) {
        if (this != &other) {
            Release();
            ptr_ = other.ptr_;
            if (ptr_) ptr_->AddRef();
        }
        return *this;
    }

    T** operator&() { Release(); return &ptr_; }
    T* operator->() const { return ptr_; }
    operator T*() const { return ptr_; }
    T* Get() const { return ptr_; }

    void Release() {
        if (ptr_) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }

private:
    T* ptr_;
};
