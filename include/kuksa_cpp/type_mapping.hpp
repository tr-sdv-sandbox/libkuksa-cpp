/**
 * @file type_mapping.hpp
 * @brief Mapping between VSS logical types and KUKSA physical types
 *
 * VSS supports logical types (int8, int16, uint8, uint16) that don't exist
 * in KUKSA protobuf, which only has int32/uint32/int64/uint64 physical types.
 *
 * This file provides type traits and utilities for:
 * - Mapping logical types to physical types (e.g., int8_t -> int32_t)
 * - Converting ValueType enums (INT8 -> INT32)
 * - Validating narrowing conversions when reading from KUKSA
 */

#pragma once

#include <vss/types/types.hpp>
#include <cstdint>
#include <type_traits>

namespace kuksa {

/**
 * @brief Map VSS logical type to KUKSA physical type
 *
 * Narrowing types (int8, int16, uint8, uint16) are widened to their
 * corresponding physical representation (int32, uint32).
 *
 * Direct types (int32, int64, uint32, uint64, float, double, bool, string)
 * map to themselves.
 */
template<typename LogicalT>
struct PhysicalType {
    // Default: type maps to itself (no conversion needed)
    using type = LogicalT;
};

// Narrowing integer type mappings
template<> struct PhysicalType<int8_t>   { using type = int32_t; };
template<> struct PhysicalType<int16_t>  { using type = int32_t; };
template<> struct PhysicalType<uint8_t>  { using type = uint32_t; };
template<> struct PhysicalType<uint16_t> { using type = uint32_t; };

// Array type mappings
template<> struct PhysicalType<std::vector<int8_t>>   { using type = std::vector<int32_t>; };
template<> struct PhysicalType<std::vector<int16_t>>  { using type = std::vector<int32_t>; };
template<> struct PhysicalType<std::vector<uint8_t>>  { using type = std::vector<uint32_t>; };
template<> struct PhysicalType<std::vector<uint16_t>> { using type = std::vector<uint32_t>; };

/**
 * @brief Helper alias for cleaner syntax
 */
template<typename LogicalT>
using physical_type_t = typename PhysicalType<LogicalT>::type;

/**
 * @brief Map VSS ValueType enum to KUKSA physical ValueType
 *
 * Converts logical types to their physical equivalents:
 * - INT8, INT16 -> INT32
 * - UINT8, UINT16 -> UINT32
 * - INT8_ARRAY, INT16_ARRAY -> INT32_ARRAY
 * - UINT8_ARRAY, UINT16_ARRAY -> UINT32_ARRAY
 * - All other types -> unchanged
 *
 * @param logical_type VSS logical ValueType
 * @return KUKSA physical ValueType
 */
inline vss::types::ValueType to_physical_value_type(vss::types::ValueType logical_type) {
    using vss::types::ValueType;

    switch (logical_type) {
        // Narrowing scalar types -> widen to physical type
        case ValueType::INT8:
        case ValueType::INT16:
            return ValueType::INT32;

        case ValueType::UINT8:
        case ValueType::UINT16:
            return ValueType::UINT32;

        // Narrowing array types -> widen to physical type
        case ValueType::INT8_ARRAY:
        case ValueType::INT16_ARRAY:
            return ValueType::INT32_ARRAY;

        case ValueType::UINT8_ARRAY:
        case ValueType::UINT16_ARRAY:
            return ValueType::UINT32_ARRAY;

        // Direct types (no conversion)
        default:
            return logical_type;
    }
}

/**
 * @brief Check if a ValueType requires narrowing conversion
 *
 * Returns true for int8, int16, uint8, uint16 (and their array variants),
 * which require validation when converting from KUKSA physical types.
 *
 * @param type ValueType to check
 * @return true if type requires narrowing, false otherwise
 */
inline bool requires_narrowing(vss::types::ValueType type) {
    using vss::types::ValueType;

    return type == ValueType::INT8 ||
           type == ValueType::INT16 ||
           type == ValueType::UINT8 ||
           type == ValueType::UINT16 ||
           type == ValueType::INT8_ARRAY ||
           type == ValueType::INT16_ARRAY ||
           type == ValueType::UINT8_ARRAY ||
           type == ValueType::UINT16_ARRAY;
}

/**
 * @brief Check if two ValueTypes are compatible despite physical representation
 *
 * Returns true if:
 * - Types are identical, OR
 * - One is a narrowing type and the other is its physical equivalent
 *   (e.g., INT8 and INT32, UINT16_ARRAY and UINT32_ARRAY)
 *
 * This is used when comparing expected vs actual types from KUKSA.
 *
 * @param logical VSS logical type (from application)
 * @param physical KUKSA physical type (from databroker)
 * @return true if types are compatible
 */
inline bool are_physically_compatible(vss::types::ValueType logical, vss::types::ValueType physical) {
    if (logical == physical) return true;
    return to_physical_value_type(logical) == physical;
}

} // namespace kuksa
