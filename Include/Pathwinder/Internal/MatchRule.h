/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022
 *************************************************************************//**
 * @file MatchRule.h
 *   Declaration of objects for matching and replacing string content.
 *****************************************************************************/

#pragma once

#include "ValueOrError.h"

#include <regex>
#include <string>
#include <string_view>


namespace Pathwinder
{
    /// Matches strings against a single fixed regular expression.
    class MatchRule
    {
    protected:
        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// Expression that will be used to determine if input strings match.
        const std::wregex kMatchExpression;


        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Initialization constructor.
        /// Requires a regular expression string.
        /// Not intended to be invoked externally. Objects of this type should be constructed using a factory method.
        MatchRule(std::wstring_view matchRegexStr, bool caseSensitive);


    public:
        // -------- CLASS METHODS ------------------------------------------ //

        /// Factory method for creating new match rule objects.
        /// Resolves any references contained within the input regular expression and escapes special characters that occur within the resolved references.
        /// @param [in] matchRegexStr String representation of the regular expression, including possible embeddded references.
        /// @param [in] caseSensitive Whether or not matches should be case sensitive, defaults to case-insensitive.
        /// @return Either a new match rule or an error message indicating why creation failed.
        static ValueOrError<MatchRule, std::wstring> Create(std::wstring_view matchRegexStr, bool caseSensitive = false);


        // -------- INSTANCE METHODS --------------------------------------- //

        /// Checks if the candidate string matches the regular expression contained within this object.
        /// @param [in] candidateString String to check against the contained regular expression.
        /// @return `true` if the candidate string matches, `false` otherwise.
        bool DoesMatch(std::wstring_view candidateString) const;
    };

    /// Matches strings against a single fixed regular expression and adds substitution functionality via a fixed pattern string.
    class MatchAndReplaceRule : public MatchRule
    {
    private:
        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// Format pattern that defines how string substitution will take place.
        const std::wstring kReplaceFormatPattern;


        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Initialization constructor.
        /// Promotes an existing match rule object to have replacement functionality.
        /// Not intended to be invoked externally. Objects of this type should be constructed using a factory method.
        MatchAndReplaceRule(MatchRule&& matchRule, std::wstring_view replaceFormatPattern);


    public:
        // -------- CLASS METHODS ------------------------------------------ //

        /// Factory method for creating new match-and-replace rule objects.
        /// Resolves any references contained within the input regular expression and escapes special characters that occur within the resolved references.
        /// @param [in] matchRegexStr String representation of the regular expression, including possible embeddded references.
        /// @param [in] replaceFormatPattern Format pattern to use when doing string substitution.
        /// @param [in] caseSensitive Whether or not matches should be case sensitive, defaults to case-insensitive.
        /// @return Either a new match-and-replace rule or an error message indicating why creation failed.
        static ValueOrError<MatchAndReplaceRule, std::wstring> Create(std::wstring_view matchRegexStr, std::wstring_view replaceFormatPattern, bool caseSensitive = false);


        // -------- INSTANCE METHODS --------------------------------------- //

        /// Replaces one or more instances of matching content within the candidate string based on the replacement format pattern.
        /// @param [in] candidateString String whose content should be examined and potentially replaced.
        /// @param [in] globalSubstitution Whether the replacement is global or stops after the very first match.
        /// @return Result of the replacement operation.
        std::wstring Replace(std::wstring_view candidateString, bool globalSubstitution = false) const;
    };
}
