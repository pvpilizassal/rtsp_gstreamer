#ifndef GST_PTR_H
#define GST_PTR_H
#include <utility>

template <typename T, void (*Deleter)(T*)>
class GstPtr {
public:
    constexpr GstPtr() noexcept : m_ptr(nullptr) {}
    explicit GstPtr(T* ptr) noexcept : m_ptr(ptr) {}

    ~GstPtr() noexcept {
        reset();
    }

    void reset(T* ptr = nullptr) noexcept {
        if (m_ptr) {
            Deleter(m_ptr);
        }
        m_ptr = ptr;
    }

private:
    T* m_ptr = nullptr;
};

#endif // GST_PTR_H
