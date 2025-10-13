/**
 * @file vss_types.cpp
 * @brief Implementation of VSS type helper functions
 */

#include <kuksa_cpp/types.hpp>
#include <algorithm>
#include <cctype>

namespace kuksa {

ValueType get_value_type(const Value& value) {
    return std::visit([](auto&& v) -> ValueType {
        using T = std::decay_t<decltype(v)>;
        return get_value_type<T>();
    }, value);
}

const char* value_type_to_string(ValueType type) {
    switch (type) {
        // Scalar types
        case ValueType::BOOL:         return "bool";
        case ValueType::INT32:        return "int32";
        case ValueType::UINT32:       return "uint32";
        case ValueType::INT64:        return "int64";
        case ValueType::UINT64:       return "uint64";
        case ValueType::FLOAT:        return "float";
        case ValueType::DOUBLE:       return "double";
        case ValueType::STRING:       return "string";

        // Array types
        case ValueType::BOOL_ARRAY:   return "bool[]";
        case ValueType::INT32_ARRAY:  return "int32[]";
        case ValueType::UINT32_ARRAY: return "uint32[]";
        case ValueType::INT64_ARRAY:  return "int64[]";
        case ValueType::UINT64_ARRAY: return "uint64[]";
        case ValueType::FLOAT_ARRAY:  return "float[]";
        case ValueType::DOUBLE_ARRAY: return "double[]";
        case ValueType::STRING_ARRAY: return "string[]";

        default: return "unknown";
    }
}

std::optional<ValueType> value_type_from_string(const std::string& str) {
    // Convert to lowercase for case-insensitive matching
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Scalar types
    if (lower == "bool" || lower == "boolean") return ValueType::BOOL;
    if (lower == "int32") return ValueType::INT32;
    if (lower == "uint32") return ValueType::UINT32;
    if (lower == "int64") return ValueType::INT64;
    if (lower == "uint64") return ValueType::UINT64;
    if (lower == "float") return ValueType::FLOAT;
    if (lower == "double") return ValueType::DOUBLE;
    if (lower == "string") return ValueType::STRING;

    // Array types - support both "type[]" and "type_array" formats
    if (lower == "bool[]" || lower == "bool_array") return ValueType::BOOL_ARRAY;
    if (lower == "int32[]" || lower == "int32_array") return ValueType::INT32_ARRAY;
    if (lower == "uint32[]" || lower == "uint32_array") return ValueType::UINT32_ARRAY;
    if (lower == "int64[]" || lower == "int64_array") return ValueType::INT64_ARRAY;
    if (lower == "uint64[]" || lower == "uint64_array") return ValueType::UINT64_ARRAY;
    if (lower == "float[]" || lower == "float_array") return ValueType::FLOAT_ARRAY;
    if (lower == "double[]" || lower == "double_array") return ValueType::DOUBLE_ARRAY;
    if (lower == "string[]" || lower == "string_array") return ValueType::STRING_ARRAY;

    // No match found
    return std::nullopt;
}

} // namespace kuksa
