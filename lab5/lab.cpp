#include <iostream>
#include <memory_resource>
#include <map>
#include <vector>
#include <cstddef>
#include <new>
#include <stdexcept>
#include <type_traits>

// 1. Реализация memory_resource (стратегия 6)
class DynamicMapMemoryResource : public std::pmr::memory_resource {
private:
    std::map<void*, size_t> allocated_blocks_;

    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        void* ptr = std::malloc(bytes); // malloc guarantees alignment >= alignof(max_align_t)
        if (!ptr) {
            throw std::bad_alloc{};
        }
        allocated_blocks_[ptr] = bytes;
        return ptr;
    }

    void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override {
        auto it = allocated_blocks_.find(p);
        if (it != allocated_blocks_.end()) {
            // Проверка соответствия размера
            std::free(p);
            allocated_blocks_.erase(it);
        }
        // Если указатель не найден — игнорируем 
    }

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }

public:
    ~DynamicMapMemoryResource() {
        // Освобождаем всё, что осталось
        for (auto& [ptr, size] : allocated_blocks_) {
            std::free(ptr);
        }
        allocated_blocks_.clear();
    }
};

// 2. Итератор для pmr_vector
template <typename T>
class pmr_vector_iterator {
public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = T*;
    using reference = T&;

private:
    T* ptr_;

public:
    explicit pmr_vector_iterator(T* p) : ptr_(p) {}

    reference operator*() const { return *ptr_; }
    pointer operator->() const { return ptr_; }

    pmr_vector_iterator& operator++() {
        ++ptr_;
        return *this;
    }

    pmr_vector_iterator operator++(int) {
        pmr_vector_iterator tmp = *this;
        ++(*this);
        return tmp;
    }

    bool operator==(const pmr_vector_iterator& other) const {
        return ptr_ == other.ptr_;
    }

    bool operator!=(const pmr_vector_iterator& other) const {
        return !(*this == other);
    }
};

// 3. Контейнер: pmr_vector
template <typename T>
class pmr_vector {
private:
    T* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t capacity_ = 0;
    std::pmr::polymorphic_allocator<T> alloc_;

    void grow() {
        std::size_t new_cap = (capacity_ == 0) ? 1 : capacity_ * 2;
        T* new_data = alloc_.allocate(new_cap);

        // Переместить существующие элементы (move)
        for (std::size_t i = 0; i < size_; ++i) {
            std::allocator_traits<std::pmr::polymorphic_allocator<T>>::construct(
                alloc_, &new_data[i], std::move(data_[i])
            );
            std::allocator_traits<std::pmr::polymorphic_allocator<T>>::destroy(
                alloc_, &data_[i]
            );
        }

        if (data_) {
            alloc_.deallocate(data_, capacity_);
        }

        data_ = new_data;
        capacity_ = new_cap;
    }

public:
    using iterator = pmr_vector_iterator<T>;
    using const_iterator = pmr_vector_iterator<const T>;

    explicit pmr_vector(std::pmr::memory_resource* mr = std::pmr::get_default_resource())
        : alloc_(mr) {}

    ~pmr_vector() {
        clear();
        if (data_) {
            alloc_.deallocate(data_, capacity_);
        }
    }

    // Запрещаем копирование (можно добавить, но для упрощения — move-only)
    pmr_vector(const pmr_vector&) = delete;
    pmr_vector& operator=(const pmr_vector&) = delete;

    pmr_vector(pmr_vector&& other) noexcept
        : data_(other.data_), size_(other.size_), capacity_(other.capacity_), alloc_(other.alloc_) {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }


    pmr_vector& operator=(pmr_vector&& other) noexcept {
        if (this != &other) {
            clear();
            if (data_) {
                alloc_.deallocate(data_, capacity_);
            }
            data_ = other.data_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            alloc_ = other.alloc_;

            other.data_ = nullptr;
            other.size_ = 0;
            other.capacity_ = 0;
        }
        return *this;
    }

    void push_back(const T& value) {
        if (size_ >= capacity_) {
            grow();
        }
        std::allocator_traits<std::pmr::polymorphic_allocator<T>>::construct(
            alloc_, &data_[size_], value
        );
        ++size_;
    }

    void push_back(T&& value) {
        if (size_ >= capacity_) {
            grow();
        }
        std::allocator_traits<std::pmr::polymorphic_allocator<T>>::construct(
            alloc_, &data_[size_], std::move(value)
        );
        ++size_;
    }

    void clear() {
        for (std::size_t i = 0; i < size_; ++i) {
            std::allocator_traits<std::pmr::polymorphic_allocator<T>>::destroy(
                alloc_, &data_[i]
            );
        }
        size_ = 0;
    }

    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    iterator begin() { return iterator(data_); }
    iterator end() { return iterator(data_ + size_); }
    const_iterator begin() const { return const_iterator(data_); }
    const_iterator end() const { return const_iterator(data_ + size_); }
    const_iterator cbegin() const { return begin(); }
    const_iterator cend() const { return end(); }

    T& operator[](std::size_t i) { return data_[i]; }
    const T& operator[](std::size_t i) const { return data_[i]; }
};

// 4. Демонстрация
struct Point {
    int x, y, z;
    Point(int x = 0, int y = 0, int z = 0) : x(x), y(y), z(z) {}
    friend std::ostream& operator<<(std::ostream& os, const Point& p) {
        return os << "(" << p.x << ", " << p.y << ", " << p.z << ")";
    }
};

int main() {
    DynamicMapMemoryResource mr;

    // Пример с int
    {
        pmr_vector<int> vec(&mr);
        vec.push_back(10);
        vec.push_back(20);
        vec.push_back(30);

        std::cout << "int vector: ";
        for (const auto& v : vec) {
            std::cout << v << " ";
        }
        std::cout << "\n";
    } // int-вектор уничтожен, 

    // Пример с Point
    {
        pmr_vector<Point> points(&mr);
        points.push_back(Point{1, 2, 3});
        points.push_back(Point{4, 5, 6});
        points.push_back(Point{7, 8, 9});

        std::cout << "Point vector:\n";
        for (const auto& p : points) {
            std::cout << p << "\n";
        }
    }

    // При уничтожении mr освободит всё, что осталось
    std::cout << "DynamicMapMemoryResource destroyed automatically.\n";
    return 0;
}
