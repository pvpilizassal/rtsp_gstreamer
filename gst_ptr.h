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

    GstPtr(const GstPtr&) = delete; //запрет копирования (двойное освобождение памяти)
    GstPtr& operator=(const GstPtr&) = delete; //запрет присваивания

    // разрешение перемещения (разрешенное копирование)
    GstPtr(GstPtr&& other) noexcept : m_ptr(std::exchange(other.m_ptr, nullptr)) {}

    // разрешение перемещения через =
    GstPtr& operator=(GstPtr&& other) noexcept {
        if (this != &other) {
            reset();
            m_ptr = std::exchange(other.m_ptr, nullptr);
        }
        return *this;
    }

    T* operator->() const noexcept { return m_ptr; }
    explicit operator bool() const noexcept { return m_ptr != nullptr; }

    void reset(T* ptr = nullptr) noexcept {
        if (m_ptr) {
            Deleter(m_ptr);
        }
        m_ptr = ptr;
    }

    // возврат сырого указателя на объект в m_ptr и зануление оного, чтоб не освободить
    [[nodiscard]] T* release() noexcept {
        return std::exchange(m_ptr, nullptr);
    }

    // возврат указателя на объект в куче
    [[nodiscard]] T* get() const noexcept { return m_ptr; }

    // возврат указателя на пустой и зануленный указатель
    T** put() noexcept {
        reset();
        return &m_ptr;
    }

private:
    T* m_ptr = nullptr;
};

#endif // GST_PTR_H
