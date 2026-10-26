// Zero-filled array backed by anonymous mmap: the OS commits a page only when it is first touched, so a buffer sized
// for the maximum recording length costs memory in proportion to what was actually recorded (and munmap gives it back
// at once; calloc would memset a recycled block and commit all of it).
#pragma once
#include <cstddef>
#include <sys/mman.h>

#include <memory>
#include <new>

namespace ttr {

template <class T>
class LazyArray {
public:
    LazyArray() = default;
    LazyArray(const LazyArray&) = delete;
    LazyArray& operator=(const LazyArray&) = delete;

    // Not real-time safe. Discards the old content; throws std::bad_alloc when the memory is not available.
    // The old storage stays valid for everyone who still holds share() of it (UI frames that are being drawn).
    void assign(size_t count) {
        ptr_.reset();
        size_ = 0;
        if (count == 0) return;
        const size_t bytes = count * sizeof(T);
        void* raw = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
        if (raw == MAP_FAILED) throw std::bad_alloc();
        ptr_ = std::shared_ptr<T>(static_cast<T*>(raw), [bytes](T* p) { munmap(p, bytes); });
        size_ = count;
    }
    // Keeps the current storage alive (type-erased); empty when nothing is allocated.
    std::shared_ptr<const void> share() const { return ptr_; }
    T* data() { return ptr_.get(); }
    const T* data() const { return ptr_.get(); }
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    T& operator[](size_t i) { return ptr_.get()[i]; }
    const T& operator[](size_t i) const { return ptr_.get()[i]; }

private:
    std::shared_ptr<T> ptr_;
    size_t size_ = 0;
};

}  // namespace ttr
