/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022
 *************************************************************************//**
 * @file MatchRule.cpp
 *   Implementation of string matching and substitution functionality.
 *****************************************************************************/

#include "MatchRule.h"
#include "Message.h"
#include "Resolver.h"
#include "Strings.h"
#include "TemporaryBuffer.h"
#include "ValueOrError.h"

#include <regex>
#include <string>
#include <string_view>


namespace Pathwinder
{
    static constexpr std::wstring_view kRegexSpecialCharacters = L".[{}()\\*+?|^$:";


    // -------- INTERNAL FUNCTIONS ----------------------------------------- //

    /// Converts a regular expression error code into a string.
    /// @param [in] errorCode Regular expression error code
    /// @return String representation of the error code.
    static TemporaryString RegexErrorString(std::regex_constants::error_type errorCode)
    {
        switch (errorCode)
        {
        case std::regex_constants::error_type::error_collate:
            return L"Expression contains an invalid collating element name";
        case std::regex_constants::error_type::error_ctype:
            return L"Expression contains an invalid character class name";
        case std::regex_constants::error_type::error_escape:
            return L"Expression contains an invalid escaped character or a trailing escape";
        case std::regex_constants::error_type::error_backref:
            return L"Expression contains an invalid back reference";
        case std::regex_constants::error_type::error_brack:
            return L"Expression contains mismatched square brackets ('[' and ']')";
        case std::regex_constants::error_type::error_paren:
            return L"Expression contains mismatched parentheses ('(' and ')')";
        case std::regex_constants::error_type::error_brace:
            return L"Expression contains mismatched curly braces ('{' and '}')";
        case std::regex_constants::error_type::error_badbrace:
            return L"Expression contains an invalid range in a {} expression";
        case std::regex_constants::error_type::error_range:
            return L"Expression contains an invalid character range (e.g. [b-a])";
        case std::regex_constants::error_type::error_space:
            return L"Expression failed to parse because there were not enough resources available";
        case std::regex_constants::error_type::error_badrepeat:
            return L"One of *?+{ was not preceded by a valid regular expression";
        case std::regex_constants::error_type::error_complexity:
            return L"Match failed because it was too complex";
        case std::regex_constants::error_type::error_stack:
            return L"Match failed because there was not enough memory available";
        case std::regex_constants::error_type::error_parse:
            return L"Expression failed to parse";
        case std::regex_constants::error_type::error_syntax:
            return L"Expression failed to parse due to a syntax error";
        default:
            return L"Unknown error";
        }
    }


    // -------- CONSTRUCTION AND DESTRUCTION ------------------------------- //
    // See "MatchRule.h" for documentation.

    MatchRule::MatchRule(std::wstring_view matchRegexStr, bool caseSensitive) : kMatchExpression(matchRegexStr.cbegin(), matchRegexStr.cend(), (std::regex_constants::syntax_option_type)(((true == caseSensitive) ? std::regex_constants::icase : 0) | std::regex_constants::optimize | std::regex_constants::ECMAScript))
    {
        // Nothing to do here.
    }

    // --------

    MatchAndReplaceRule::MatchAndReplaceRule(MatchRule&& matchRule, std::wstring_view replaceFormatPattern) : MatchRule(std::move(matchRule)), kReplaceFormatPattern(replaceFormatPattern)
    {
        // Nothing to do here.
    }


    // -------- CLASS METHODS ---------------------------------------------- //
    // See "MatchRule.h" for documentation.

    ValueOrError<MatchRule, std::wstring> MatchRule::Create(std::wstring_view matchRegexStr, bool caseSensitive)
    {
        try
        {
            Resolver::ResolvedStringOrError maybeMatchRegexResolvedStr = Resolver::ResolveAllReferences(matchRegexStr, kRegexSpecialCharacters);
            if (true == maybeMatchRegexResolvedStr.HasError())
                return ValueOrError<MatchRule, std::wstring>::MakeError(Strings::FormatString(L"Failed to construct regular expression: %s: Failed to resolve embedded reference: %s.", matchRegexStr.data(), maybeMatchRegexResolvedStr.Error().c_str()));

            Message::OutputFormatted(Message::ESeverity::Debug, L"Resolved references in a regular expression: \"%s\" => \"%s\"", matchRegexStr.data(), maybeMatchRegexResolvedStr.Value().c_str());
            return MatchRule(maybeMatchRegexResolvedStr.Value(), caseSensitive);
        }
        catch (std::regex_error regexError)
        {
            return ValueOrError<MatchRule, std::wstring>::MakeError(Strings::FormatString(L"Failed to construct regular expression: %s: %s.", matchRegexStr.data(), RegexErrorString(regexError.code()).AsCString()));
        }
    }

    // --------

    ValueOrError<MatchAndReplaceRule, std::wstring> MatchAndReplaceRule::Create(std::wstring_view matchRegexStr, std::wstring_view replaceFormatPattern, bool caseSensitive)
    {
        ValueOrError<MatchRule, std::wstring> maybeMatchRule = MatchRule::Create(matchRegexStr, caseSensitive);
        if (true == maybeMatchRule.HasError())
            return std::move(maybeMatchRule.Error());

        Resolver::ResolvedStringOrError maybeReplacementFormatPatternResolvedStr = Resolver::ResolveAllReferences(replaceFormatPattern, kRegexSpecialCharacters);
        if (true == maybeReplacementFormatPatternResolvedStr.HasError())
            return ValueOrError<MatchAndReplaceRule, std::wstring>::MakeError(Strings::FormatString(L"Failed to construct replacement pattern: %s: Failed to resolve embedded reference: %s.", matchRegexStr.data(), maybeReplacementFormatPatternResolvedStr.Error().c_str()));

        Message::OutputFormatted(Message::ESeverity::Debug, L"Resolved references in a replacement format pattern: \"%s\" => \"%s\"", replaceFormatPattern.data(), maybeReplacementFormatPatternResolvedStr.Value().c_str());
        return MatchAndReplaceRule(std::move(maybeMatchRule.Value()), maybeReplacementFormatPatternResolvedStr.Value());
    }


    // -------- INSTANCE METHODS ------------------------------------------- //
    // See "MatchRule.h" for documentation.

    bool MatchRule::DoesMatch(std::wstring_view candidateString) const
    {
        return std::regex_search(candidateString.cbegin(), candidateString.cend(), kMatchExpression);
    }

    // --------

    std::wstring MatchAndReplaceRule::Replace(std::wstring_view candidateString, bool globalSubstitution) const
    {
        return std::regex_replace(candidateString.data(), kMatchExpression, kReplaceFormatPattern.c_str(), (std::regex_constants::match_flag_type)(((true == globalSubstitution) ? 0 : std::regex_constants::match_flag_type::format_first_only) | std::regex_constants::match_flag_type::format_sed));
    }
}
