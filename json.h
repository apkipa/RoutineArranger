#pragma once

#include <variant>

// Implement json functions via WinRT APIs
namespace json {
    class JsonValue;
    class JsonObject;
    class JsonArray;

    namespace details {
        template<typename T>
        struct dependent_false_type : std::false_type {};
    }

    enum class JsonValueKind {
        Null = 0,
        Boolean,
        Array,
        Number,
        String,
        Object,
    };

    class JsonArray {
    public:
        using value_type = JsonValue;
        using Container = std::vector<value_type>;

        using allocator_type = typename Container::allocator_type;
        using size_type = typename Container::size_type;
        using difference_type = typename Container::difference_type;
        using reference = typename Container::reference;
        using const_reference = typename Container::const_reference;
        using pointer = typename Container::pointer;
        using const_pointer = typename Container::const_pointer;
        using iterator = typename Container::iterator;
        using const_iterator = typename Container::const_iterator;
        using reverse_iterator = typename Container::reverse_iterator;
        using const_reverse_iterator = typename Container::const_reverse_iterator;

        JsonArray() : m_vec() {}
        ~JsonArray() = default;
        JsonArray(JsonArray const& other) : m_vec(other.m_vec) {}
        JsonArray(JsonArray&& other) noexcept : JsonArray() { swap(*this, other); }
        JsonArray& operator=(JsonArray other) noexcept { swap(*this, other); return *this; }

        // TODO: Add more functions from std::vector
        reference at(size_type pos) { return m_vec.at(pos); }
        const_reference at(size_type pos) const { return m_vec.at(pos); }
        reference operator[](size_type pos) noexcept { return m_vec[pos]; }
        const_reference operator[](size_type pos) const noexcept { return m_vec[pos]; }
        reference front() noexcept { return m_vec.front(); }
        const_reference front() const noexcept { return m_vec.front(); }
        reference back() noexcept { return m_vec.back(); }
        const_reference back() const noexcept { return m_vec.back(); }
        value_type* data() noexcept { return m_vec.data(); }
        const value_type* data() const noexcept { return m_vec.data(); }
        iterator begin() noexcept { return m_vec.begin(); }
        const_iterator begin() const noexcept { return m_vec.begin(); };
        const_iterator cbegin() const noexcept { return m_vec.cbegin(); };
        iterator end() noexcept { return m_vec.end(); }
        const_iterator end() const noexcept { return m_vec.end(); };
        const_iterator cend() const noexcept { return m_vec.cend(); };
        reverse_iterator rbegin() noexcept { return m_vec.rbegin(); }
        const_reverse_iterator rbegin() const noexcept { return m_vec.rbegin(); };
        const_reverse_iterator crbegin() const noexcept { return m_vec.crbegin(); };
        reverse_iterator rend() noexcept { return m_vec.rend(); }
        const_reverse_iterator rend() const noexcept { return m_vec.rend(); };
        const_reverse_iterator crend() const noexcept { return m_vec.crend(); };
        bool empty() const noexcept { return m_vec.empty(); }
        size_type size() const noexcept { return m_vec.size(); }
        size_type max_size() const noexcept { return m_vec.max_size(); }
        void reserve(size_type new_cap) { m_vec.reserve(new_cap); }
        size_type capacity() const noexcept { return m_vec.capacity(); }
        void shrink_to_fit() { m_vec.shrink_to_fit(); }
        void clear() noexcept { m_vec.clear(); }
        // SAFETY: JsonValue is designed to be nothrow-move-assignable
        iterator erase(iterator pos) noexcept;
        void push_back(value_type const& value) { m_vec.push_back(value); }
        void push_back(value_type&& value) { m_vec.push_back(std::move(value)); }

        bool operator==(JsonArray const& rhs) const;
        bool operator!=(JsonArray const& rhs) const {
            return !operator==(rhs);
        }

        friend void swap(JsonArray& a, JsonArray& b) noexcept {
            using std::swap;
            swap(a.m_vec, b.m_vec);
        }

        friend class JsonHelper;
    private:
        Container m_vec;
    };

    class JsonObject {
    public:
        using key_type = std::wstring;
        using mapped_type = JsonValue;
        using key_compare = std::less<>;
        using Container = std::map<key_type, mapped_type, key_compare>;

        using value_compare = typename Container::value_compare;
        using value_type = typename Container::value_type;
        using allocator_type = typename Container::allocator_type;
        using size_type = typename Container::size_type;
        using difference_type = typename Container::difference_type;
        using pointer = typename Container::pointer;
        using const_pointer = typename Container::const_pointer;
        using reference = value_type&;
        using const_reference = const value_type&;
        using iterator = typename Container::iterator;
        using const_iterator = typename Container::const_iterator;
        using reverse_iterator = typename Container::reverse_iterator;
        using const_reverse_iterator = typename Container::const_reverse_iterator;

        JsonObject() : m_map() {}
        ~JsonObject() = default;
        JsonObject(JsonObject const& other) : m_map(other.m_map) {}
        JsonObject(JsonObject&& other) noexcept : JsonObject() { swap(*this, other); }
        JsonObject& operator=(JsonObject other) noexcept { swap(*this, other); return *this; }

        JsonValue& operator[](std::wstring_view sv);

        // TODO: Add more functions from std::map
        // TODO: Improve performance by not constructing temporary objects
        mapped_type& at(std::wstring_view key) { return m_map.at(std::wstring{ key }); }
        const mapped_type& at(std::wstring_view key) const { return m_map.at(std::wstring{ key }); }
        iterator begin() noexcept { return m_map.begin(); }
        const_iterator begin() const noexcept { return m_map.begin(); };
        const_iterator cbegin() const noexcept { return m_map.cbegin(); };
        iterator end() noexcept { return m_map.end(); }
        const_iterator end() const noexcept { return m_map.end(); };
        const_iterator cend() const noexcept { return m_map.cend(); };
        reverse_iterator rbegin() noexcept { return m_map.rbegin(); }
        const_reverse_iterator rbegin() const noexcept { return m_map.rbegin(); };
        const_reverse_iterator crbegin() const noexcept { return m_map.crbegin(); };
        reverse_iterator rend() noexcept { return m_map.rend(); }
        const_reverse_iterator rend() const noexcept { return m_map.rend(); };
        const_reverse_iterator crend() const noexcept { return m_map.crend(); };
        bool empty() const noexcept { return m_map.empty(); }
        size_type size() const noexcept { return m_map.size(); }
        size_type max_size() const noexcept { return m_map.max_size(); }
        void clear() noexcept { m_map.clear(); }
        iterator erase(iterator pos) noexcept { return m_map.erase(pos); }
        size_type count(std::wstring_view key) const { return m_map.count(key); }
        const_iterator find(std::wstring_view key) const { return m_map.find(key); }
        // NOTE: Not re-exporting this method from the C++20 one
        bool contains(std::wstring_view key) const { return m_map.count(key) > 0; }

        bool operator==(JsonObject const& rhs) const;
        bool operator!=(JsonObject const& rhs) const {
            return !operator==(rhs);
        }

        friend void swap(JsonObject& a, JsonObject& b) noexcept;

        friend class JsonHelper;
    private:
        std::map<std::wstring, JsonValue, std::less<>> m_map;
    };

    class JsonValue {
    public:
        JsonValue() : m_kind(JsonValueKind::Null), m_var(nullptr) {}
        JsonValue(std::nullptr_t) : m_kind(JsonValueKind::Null), m_var(nullptr) {}
        JsonValue(bool v) : m_kind(JsonValueKind::Boolean), m_var(v) {}
        JsonValue(JsonArray const& v) : m_kind(JsonValueKind::Array), m_var(v) {}
        JsonValue(JsonArray&& v) : m_kind(JsonValueKind::Array), m_var(std::move(v)) {}
        //JsonValue(double v) : m_kind(JsonValueKind::Number), m_var(v) {}
        template<typename T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>
        JsonValue(T v) : m_kind(JsonValueKind::Number), m_var(static_cast<double>(v)) {}
        JsonValue(std::wstring_view v) : m_kind(JsonValueKind::String), m_var(std::wstring{ v }) {}
        JsonValue(std::wstring const& v) : JsonValue(std::wstring_view{ v }) {}
        JsonValue(std::wstring&& v) : m_kind(JsonValueKind::String), m_var(std::move(v)) {}
        JsonValue(JsonObject const& v) : m_kind(JsonValueKind::Object), m_var(v) {}
        JsonValue(JsonObject&& v) : m_kind(JsonValueKind::Object), m_var(std::move(v)) {}
        ~JsonValue() = default;
        JsonValue(JsonValue const& other) : m_kind(other.m_kind), m_var(other.m_var) {}
        JsonValue(JsonValue&& other) noexcept : JsonValue() { swap(*this, other); }
        JsonValue& operator=(JsonValue other) noexcept { swap(*this, other); return *this; }

        bool try_deserialize_from_utf8(const char* data, size_t len);
        bool try_deserialize_from_utf8(std::vector<char> const& data) {
            return try_deserialize_from_utf8(data.data(), data.size());
        };
        std::vector<char> serialize_into_utf8(void) const;

        bool is_null(void) const { return m_kind == JsonValueKind::Null; }
        bool is_bool(void) const { return m_kind == JsonValueKind::Boolean; }
        bool is_array(void) const { return m_kind == JsonValueKind::Array; }
        bool is_number(void) const { return m_kind == JsonValueKind::Number; }
        bool is_string(void) const { return m_kind == JsonValueKind::String; }
        bool is_object(void) const { return m_kind == JsonValueKind::Object; }

        // TODO: Add proxy support for integral types ?
        template<typename T>
        T& get(void) {
            return std::get<T>(m_var);
        }
        template<typename T>
        const T& get(void) const {
            return std::get<T>(m_var);
        }
        JsonValue& operator[](size_t idx) {
            return this->get<JsonArray>()[idx];
        }
        const JsonValue& operator[](size_t idx) const {
            return this->get<JsonArray>()[idx];
        }
        JsonValue& operator[](std::wstring_view sv) {
            return this->get<JsonObject>()[sv];
        }
        JsonValue& at(size_t idx) {
            return this->get<JsonArray>().at(idx);
        }
        const JsonValue& at(size_t idx) const {
            return this->get<JsonArray>().at(idx);
        }
        JsonValue& at(std::wstring_view sv) {
            return this->get<JsonObject>().at(sv);
        }
        const JsonValue& at(std::wstring_view sv) const {
            return this->get<JsonObject>().at(sv);
        }
        template<typename T>
        T get_value(void) const {
            if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
                return static_cast<T>(std::get<double>(m_var));
            }
            else {
                return std::get<T>(m_var);
            }
        }
        template<typename T>
        void set_value(T const& v) {
            // NOTE: bool is specialized below, so no special checking is required here
            constexpr bool valid_number = std::is_arithmetic_v<T>;
            constexpr bool valid_string = std::is_convertible_v<T, std::wstring>;
            static_assert(valid_number || valid_string, "Invalid set_value type for JsonValue");
            if constexpr (valid_number) {
                m_var = static_cast<double>(v);
                m_kind = JsonValueKind::Number;
            }
            else {  // if constexpr (valid_string)
                m_var = std::wstring{ v };
                m_kind = JsonValueKind::String;
            }
        }
        template<>
        void set_value(bool const& v) {
            m_var = v;
            m_kind = JsonValueKind::Boolean;
        }
        template<>
        void set_value(JsonArray const& v) {
            m_var = v;
            m_kind = JsonValueKind::Array;
        }
        template<>
        void set_value(JsonObject const& v) {
            m_var = v;
            m_kind = JsonValueKind::Object;
        }
        template<>
        void set_value(std::nullptr_t const&) {
            m_var = nullptr;
            m_kind = JsonValueKind::Null;
        }
        void set_value(JsonArray&& v) {
            m_var = std::move(v);
            m_kind = JsonValueKind::Array;
        }
        void set_value(JsonObject&& v) {
            m_var = std::move(v);
            m_kind = JsonValueKind::Object;
        }
        void set_value(std::wstring&& v) {
            m_var = std::move(v);
            m_kind = JsonValueKind::String;
        }

        bool operator==(JsonValue const& rhs) const {
            return m_kind == rhs.m_kind && m_var == rhs.m_var;
        }
        bool operator!=(JsonValue const& rhs) const {
            return !operator==(rhs);
        }

        friend void swap(JsonValue& a, JsonValue& b) noexcept {
            using std::swap;
            swap(a.m_kind, b.m_kind);
            swap(a.m_var, b.m_var);
        }

        friend class JsonHelper;
    private:
        JsonValueKind m_kind;
        std::variant<std::nullptr_t, bool, JsonArray, double, std::wstring, JsonObject> m_var;
    };
}
