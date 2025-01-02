/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2025
 ***********************************************************************************************//**
 * @file Resolver.h
 *   Interface declaration for resolving references identified by name and relative path
 *   components.
 **************************************************************************************************/

#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include <Infra/Core/Configuration.h>
#include <Infra/Core/Strings.h>
#include <Infra/Core/TemporaryBuffer.h>
#include <Infra/Core/ValueOrError.h>

namespace Pathwinder
{
  /// Delimiter used to separate portions of a string that are to be taken as literals versus
  /// to be taken as named references.
  inline constexpr std::wstring_view kStrDelimiterReferenceVsLiteral = L"%";

  /// Delimiter used to separate a named reference into a domain part and a name part.
  inline constexpr std::wstring_view kStrDelimterReferenceDomainVsName = L"::";

  /// Domain part of a named reference that identifies the domain as being a built-in string.
  inline constexpr std::wstring_view kStrReferenceDomainBuiltin = L"BUILTIN";

  /// Domain part of a named reference that identifies the domain as being an environment
  /// variable.
  inline constexpr std::wstring_view kStrReferenceDomainEnvironmentVariable = L"ENV";

  /// Domain part of a named reference that identifies the domain as being a shell "known
  /// folder" identifier.
  inline constexpr std::wstring_view kStrReferenceDomainKnownFolderIdentifier = L"FOLDERID";

  /// Type alias for representing either the result of resolving references or an error
  /// message. This version fully contains and owns the resulting string.
  using ResolvedStringOrError = Infra::ValueOrError<std::wstring, Infra::TemporaryString>;

  /// Type alias for representing either the result of resolving references or an error
  /// message. This version provides the resulting string as a read-only view.
  using ResolvedStringViewOrError = Infra::ValueOrError<std::wstring_view, Infra::TemporaryString>;

  class Resolver
  {
  public:

    /// Type alias for representing all the definitions of values within a single domain.
    using TDefinitions = std::unordered_map<
        std::wstring,
        std::wstring,
        Infra::Strings::CaseInsensitiveHasher<wchar_t>,
        Infra::Strings::CaseInsensitiveEqualityComparator<wchar_t>>;

    Resolver(void);

    Resolver(const Resolver& other) = delete;

    Resolver(Resolver&& other) = delete;

    /// Registers a custom domain in this resolver object.
    /// @param [in] domain Name of the custom domain.
    /// @param [in] definitions Definitions of variables in the custom domain. These may include
    /// references to other variables in any domain.
    /// @return `true` if the domain was successfully registered (domain name is non-empty and not
    /// already registered), `false` otherwise.
    bool RegisterCustomDomain(std::wstring_view domain, TDefinitions&& definitions);

    /// Resolves a single reference represented by the input string. Input string is expected to
    /// be of the form [DOMAIN]::[REFERENCE_NAME]. Single reference resolution results are
    /// cached internally, so the result is a view into the internal cache data structure.
    /// @param [in] str Input string representing a single reference.
    /// @return Resolved reference or an error message if the resolution failed.
    ResolvedStringViewOrError ResolveSingleReference(std::wstring_view str);

    /// Resolves all references contained in the input string and optionally escapes special
    /// characters if they appear within the results of any references that are resolved. For
    /// example, if variable X is defined as "ABC!DEF" and this function is asked to escape
    /// characters including '!' then the result of "%X%" is "ABC\!DEF" with a backslash escape
    /// in the proper location. Each reference is expected to be of the form
    /// %[DOMAIN]::[REFERENCE_NAME]% with %% used to indicate a literal '%' sign. Full string
    /// reference resolution results are not cached and involve a fair bit of dynamic memory
    /// manipulation, so the result object fully contains and owns its string.
    /// @param [in] str Input string for which references should be resolved.
    /// @param [in] escapeCharacters Optional string containing characters to escape if they
    /// appear within the results of any references that are resolved, defaults to none.
    /// @param [in] escapeSequenceStart Optional string specifying what character sequence to
    /// use to begin an escape sequence, defaults to a single backslash.
    /// @param [in] escapeSequenceEnd Optional string specifying what character sequence to use
    /// to end an escape sequence, defaults to an empty string.
    /// @return Input string with all references resolved or an error message if the resolution
    /// failed.
    ResolvedStringOrError ResolveAllReferences(
        std::wstring_view str,
        std::wstring_view escapeCharacters = std::wstring_view(),
        std::wstring_view escapeSequenceStart = L"\\",
        std::wstring_view escapeSequenceEnd = std::wstring_view());

  private:

    /// Type alias for all functions that attempt to resolve a specific type of reference.
    using TResolveReferenceFunc = std::function<ResolvedStringOrError(std::wstring_view)>;

    /// Type alias for a registry for all resolver functions keyed by domain.
    using TResolversByDomainRegistry = std::unordered_map<
        std::wstring_view,
        TResolveReferenceFunc,
        Infra::Strings::CaseInsensitiveHasher<wchar_t>,
        Infra::Strings::CaseInsensitiveEqualityComparator<wchar_t>>;

    /// Resolves a definition for a custom domain.
    /// @param [in] name Name of the custom domain variable to resolve.
    /// @param [in] definitions Definitions that correspond to the variables in the custom domain.
    /// @return Resolved value on success, error message on failure.
    ResolvedStringOrError ResolveCustomDomainVariable(
        std::wstring_view name, const TDefinitions& definitions);

    /// Container for holding reference resolutions that are currently in progress at any given
    /// time. Used for cycle detection.
    std::unordered_set<
        std::wstring,
        Infra::Strings::CaseInsensitiveHasher<wchar_t>,
        Infra::Strings::CaseInsensitiveEqualityComparator<wchar_t>>
        resolutionsInProgress;

    /// Internal cache of the result of resolving a single reference.
    TDefinitions resolvedSingleReferenceCache;

    /// Map of domain to its corresponding resolver function.
    TResolversByDomainRegistry resolversByDomain;
  };
} // namespace Pathwinder
