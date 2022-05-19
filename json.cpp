#include "pch.h"

#include "json.h"

using namespace winrt;

namespace json {
    class JsonHelper {
    public:
        static JsonArray value_from_winrt(Windows::Data::Json::JsonArray const& ja) {
            JsonArray result;
            for (auto&& i : ja) {
                result.m_vec.push_back(value_from_winrt(i));
            }
            return result;
        }
        static JsonObject value_from_winrt(Windows::Data::Json::JsonObject const& jo) {
            JsonObject result;
            for (auto&& i : jo) {
                result.m_map.emplace(i.Key(), value_from_winrt(i.Value()));
            }
            return result;
        }
        static JsonValue value_from_winrt(Windows::Data::Json::IJsonValue const& jv) {
            JsonValue result;
            switch (jv.ValueType()) {
            case Windows::Data::Json::JsonValueType::Null:
                result.m_kind = JsonValueKind::Null;
                result.m_var = nullptr;
                break;
            case Windows::Data::Json::JsonValueType::Boolean:
                result.m_kind = JsonValueKind::Boolean;
                result.m_var = jv.GetBoolean();
                break;
            case Windows::Data::Json::JsonValueType::Array:
                result.m_kind = JsonValueKind::Array;
                result.m_var = value_from_winrt(jv.GetArray());
                break;
            case Windows::Data::Json::JsonValueType::Number:
                result.m_kind = JsonValueKind::Number;
                result.m_var = jv.GetNumber();
                break;
            case Windows::Data::Json::JsonValueType::String:
                result.m_kind = JsonValueKind::String;
                result.m_var = std::wstring{ jv.GetString() };
                break;
            case Windows::Data::Json::JsonValueType::Object:
                result.m_kind = JsonValueKind::Object;
                result.m_var = value_from_winrt(jv.GetObjectW());
                break;
            default:
                // Swallow the unreachable error
                //throw winrt::hresult_error(E_FAIL, L"Invalid Windows::Data::Json::JsonValueType value");
                break;
            }
            return result;
        }

        static Windows::Data::Json::JsonArray value_into_winrt(JsonArray const& ja) {
            Windows::Data::Json::JsonArray result;
            for (auto& i : ja) {
                result.Append(value_into_winrt(i));
            }
            return result;
        }
        static Windows::Data::Json::JsonObject value_into_winrt(JsonObject const& jo) {
            Windows::Data::Json::JsonObject result;
            for (auto& i : jo) {
                result.Insert(i.first, value_into_winrt(i.second));
            }
            return result;
        }
        static Windows::Data::Json::IJsonValue value_into_winrt(JsonValue const& jv) {
            switch (jv.m_kind) {
            case JsonValueKind::Null:
                return Windows::Data::Json::JsonValue::CreateNullValue();
            case JsonValueKind::Boolean:
                return Windows::Data::Json::JsonValue::CreateBooleanValue(std::get<bool>(jv.m_var));
            case JsonValueKind::Array:
                return value_into_winrt(std::get<JsonArray>(jv.m_var));
            case JsonValueKind::Number:
                return Windows::Data::Json::JsonValue::CreateNumberValue(std::get<double>(jv.m_var));
            case JsonValueKind::String:
                return Windows::Data::Json::JsonValue::CreateStringValue(std::get<std::wstring>(jv.m_var));
            case JsonValueKind::Object:
                return value_into_winrt(std::get<JsonObject>(jv.m_var));
            default:
                // Should be UNREACHABLE
                return Windows::Data::Json::JsonValue::CreateNullValue();
            }
        }
    };

    auto JsonArray::erase(iterator pos) noexcept -> iterator { return m_vec.erase(pos); }
    bool JsonArray::operator==(JsonArray const& rhs) const {
        return m_vec == rhs.m_vec;
    }

    JsonValue& JsonObject::operator[](std::wstring_view sv) {
        auto iter = m_map.find(sv);
        if (iter != m_map.end()) {
            return iter->second;
        }
        else {
            return m_map.emplace(sv, JsonValue{}).first->second;
        }
    }
    bool JsonObject::operator==(JsonObject const& rhs) const {
        return m_map == rhs.m_map;
    }
    void swap(JsonObject& a, JsonObject& b) noexcept {
        using std::swap;
        swap(a.m_map, b.m_map);
    }

    bool JsonValue::try_deserialize_from_utf8(const char* data, size_t len) {
        auto data_buf = Windows::Security::Cryptography::CryptographicBuffer::CreateFromByteArray(
            { reinterpret_cast<const uint8_t*>(data), static_cast<uint32_t>(len) }
        );
        auto data_str = Windows::Security::Cryptography::CryptographicBuffer::ConvertBinaryToString(
            Windows::Security::Cryptography::BinaryStringEncoding::Utf8,
            data_buf
        );
        auto json_value = Windows::Data::Json::JsonValue::CreateNullValue();
        if (!Windows::Data::Json::JsonValue::TryParse(data_str, json_value)) {
            return false;
        }

        JsonValue result = JsonHelper::value_from_winrt(json_value);
        swap(*this, result);

        return true;
    }
    std::vector<char> JsonValue::serialize_into_utf8(void) const {
        auto data_buf = Windows::Security::Cryptography::CryptographicBuffer::ConvertStringToBinary(
            JsonHelper::value_into_winrt(*this).Stringify(),
            Windows::Security::Cryptography::BinaryStringEncoding::Utf8
        );
        const char* data_ptr = reinterpret_cast<const char*>(data_buf.data());
        return { data_ptr, data_ptr + data_buf.Length() };
    }
}
