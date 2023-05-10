/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file ValueOrError.h
 *   Variant type that holds either a value or an error of some kind.
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>
#include <variant>


namespace Pathwinder
{
    /// Template for variants that can hold either values or errors.
    /// Useful as a return value from functions that can either produce a result or indicate an error.
    /// @tparam ValueType Type used to represent values.
    /// @tparam ErrorType Type used to represent errors.
    template <typename ValueType, typename ErrorType> class ValueOrError
    {
    private:
        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// Value or error itself.
        std::variant<ValueType, ErrorType> valueOrError;


    public:
        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Default constructor.
        /// Should never be invoked because all objects must have either a value or an error.
        ValueOrError(void) = delete;

        /// Conversion constructor.
        /// Delegates to the underlying data structure.
        template <typename... Args> constexpr ValueOrError(Args&&... args) : valueOrError(std::forward<Args>(args)...)
        {
            // Nothing to do here.
        }

        /// Disambiguating constructor for null pointers.
        /// If the value type is constructible from a null pointer,  then a null pointer argument favors construction of a value rather than an error.
        /// Useful when both value and error types can be constructed using a null pointer argument.
        template <typename = std::enable_if_t<std::is_constructible_v<ValueType, std::nullptr_t>>> constexpr ValueOrError(std::nullptr_t) : valueOrError(ValueType(nullptr))
        {
            // Nothing to do here.
        }


        // -------- OPERATORS ---------------------------------------------- //

        /// Equality operator.
        constexpr inline bool operator==(const ValueOrError& other) const = default;


        // -------- CLASS METHODS ------------------------------------------ //

        /// Creates an object that holds an error.
        /// @return Newly-created object containing an error.
        template <typename... Args> static constexpr ValueOrError<ValueType, ErrorType> MakeError(Args&&... args)
        {
            return ValueOrError<ValueType, ErrorType>(std::in_place_index<1>, ErrorType(std::forward<Args>(args)...));
        }

        /// Creates an object that holds a value.
        /// @return Newly-created object containing a value.
        template <typename... Args> static constexpr ValueOrError<ValueType, ErrorType> MakeValue(Args&&... args)
        {
            return ValueOrError<ValueType, ErrorType>(std::in_place_index<0>, ValueType(std::forward<Args>(args)...));
        }


        // -------- INSTANCE METHODS --------------------------------------- //

        /// Retrieves a read-only reference to the error held by this object.
        /// @return Error by reference.
        constexpr inline const ErrorType& Error(void) const
        {
            return std::get<1>(valueOrError);
        }

        /// Retrieves a mutable reference to the error held by this object.
        /// @return Error by reference.
        constexpr inline ErrorType& Error(void)
        {
            return std::get<1>(valueOrError);
        }

        /// Specifies if this object holds an error, as opposed to a value.
        /// @return `true` if so, `false` otherwise.
        constexpr inline bool HasError(void) const
        {
            return (1 == valueOrError.index());
        }

        /// Specifies if this object holds a value, as opposed to an error.
        /// @return `true` if so, `false` otherwise.
        constexpr inline bool HasValue(void) const
        {
            return (0 == valueOrError.index());
        }

        /// Retrieves a read-only reference to the value held by this object.
        /// @return Value by reference.
        constexpr inline const ValueType& Value(void) const
        {
            return std::get<0>(valueOrError);
        }

        /// Retrieves a mutable reference to the value held by this object.
        /// @return Value by reference.
        constexpr inline ValueType& Value(void)
        {
            return std::get<0>(valueOrError);
        }

        /// Retrieves a copy of the value held by this object, if this object holds a value, or a copy of the specified default value otherwise.
        /// @tparam DefaultValueType Type to use for the default value, which must be convertible to the value type held by this object.
        /// @param [in] defaultValue Default value to use in the absence of a value.
        /// @return Value held by this object or default value, depending on the state of this object.
        template <typename DefaultValueType> constexpr inline ValueType ValueOr(DefaultValueType&& defaultValue) const&
        {
            if (true == HasValue())
                return Value();

            return std::forward<DefaultValueType>(defaultValue);
        }

        /// Moves and returns the value held by this object, if this object holds a value, or a copy of the specified default value otherwise.
        /// @tparam DefaultValueType Type to use for the default value, which must be convertible to the value type held by this object.
        /// @param [in] defaultValue Default value to use in the absence of a value.
        /// @return Value held by this object or default value, depending on the state of this object.
        template <typename DefaultValueType> constexpr inline ValueType ValueOr(DefaultValueType&& defaultValue) &&
        {
            if (true == HasValue())
                return std::move(Value());

            return std::forward<DefaultValueType>(defaultValue);
        }
    };
}
