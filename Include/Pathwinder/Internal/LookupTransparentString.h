/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file LookupTransparentString.h
 *   Declaration and implementation of a string type that can either own a
 *   buffer or act as a string view. Intended to support transparent lookup
 *   in standard containers that might not ordinarily support it.
 *****************************************************************************/

#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>


namespace Pathwinder
{
    /// String type that can either own a buffer or act as a string view. Never guaranteed to be null-terminated, and immutable once created.
    /// @tparam CharType Type of character in each string, either narrow or wide.
    template <typename CharType> class LookupTransparentString
    {
    private:
        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// String data, either as a string object or as a string view.
        std::variant<std::basic_string<CharType>, std::basic_string_view<CharType>> stringVariant;


    public:
        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Initialization constructor.
        /// Copies the specified string object and takes ownership over a newly-allocated buffer.
        inline LookupTransparentString(const std::basic_string<CharType>& existingString) : stringVariant(std::basic_string<CharType>(existingString))
        {
            // Nothing to do here.
        }

        /// Initialization constructor.
        /// Moves the specified string object and takes ownership over its existing contents.
        inline LookupTransparentString(std::basic_string<CharType>&& existingString) : stringVariant(std::basic_string<CharType>(std::move(existingString)))
        {
            // Nothing to do here.
        }

        /// Initialization constructor.
        /// Allocates a new owned buffer and copies the specified null-terminated C string into it.
        inline LookupTransparentString(const CharType* existingString) : stringVariant(std::basic_string<CharType>(existingString))
        {
            // Nothing to do here.
        }

        /// Initialization constructor.
        /// Copies the specified string view into this object, without copying the string itself or taking any ownership.
        inline LookupTransparentString(std::basic_string_view<CharType> existingStringView) : stringVariant(std::basic_string_view<CharType>(existingStringView))
        {
            // Nothing to do here.
        }

        /// Copy constructor.
        LookupTransparentString(const LookupTransparentString& other) = default;

        /// Move constructor.
        LookupTransparentString(LookupTransparentString&& other) = default;


        // -------- OPERATORS ---------------------------------------------- //

        /// Implicit conversion to a string view.
        /// This is the main way to access string data.
        inline operator std::basic_string_view<CharType>(void) const
        {
            if (0 == stringVariant.index())
                return std::get<0>(stringVariant);
            else
                return std::get<1>(stringVariant);
        }

        /// Equality comparison with a null-terminated C string.
        inline bool operator==(const CharType* rhs) const
        {
            return (std::basic_string_view<CharType>(rhs) == static_cast<std::basic_string_view<CharType>>(*this));
        }

        /// Equality comparison with a string.
        inline bool operator==(const std::basic_string<CharType>& rhs) const
        {
            return (static_cast<std::basic_string_view<CharType>>(rhs) == static_cast<std::basic_string_view<CharType>>(*this));
        }

        /// Equality comparison with a string view.
        inline bool operator==(const std::basic_string_view<CharType>& rhs) const
        {
            return (static_cast<std::basic_string_view<CharType>>(rhs) == static_cast<std::basic_string_view<CharType>>(*this));
        }

        /// Equality comparison with another object of this type.
        inline bool operator==(const LookupTransparentString& rhs) const
        {
            return (static_cast<std::basic_string_view<CharType>>(rhs) == static_cast<std::basic_string_view<CharType>>(*this));
        }

        /// Less-than comparison with a null-terminated C string.
        inline bool operator<(const CharType* rhs) const
        {
            return (std::basic_string_view<CharType>(rhs) < static_cast<std::basic_string_view<CharType>>(*this));
        }

        /// Less-than comparison with a string.
        inline bool operator<(const std::basic_string<CharType>& rhs) const
        {
            return (static_cast<std::basic_string_view<CharType>>(rhs) < static_cast<std::basic_string_view<CharType>>(*this));
        }

        /// Less-than comparison with a string view.
        inline bool operator<(const std::basic_string_view<CharType>& rhs) const
        {
            return (static_cast<std::basic_string_view<CharType>>(rhs) < static_cast<std::basic_string_view<CharType>>(*this));
        }

        /// Less-than comparison with another object of this type.
        inline bool operator<(const LookupTransparentString& rhs) const
        {
            return (static_cast<std::basic_string_view<CharType>>(rhs) < static_cast<std::basic_string_view<CharType>>(*this));
        }
    };
}

/// Produces standard hashes for lookup-transparent string objects by delegating to the string view hasher.
/// @tparam CharType Type of character in each string, either narrow or wide.
template <typename CharType> struct std::hash<Pathwinder::LookupTransparentString<CharType>>
{
    inline size_t operator()(const Pathwinder::LookupTransparentString<CharType>& key) const
    {
        std::hash<std::basic_string_view<CharType>> hasher;
        return hasher(static_cast<std::basic_string_view<CharType>>(key));
    }
};
