/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2024
 ***********************************************************************************************//**
 * @file FilesystemDirector.cpp
 *   Implementation of filesystem manipulation and application functionality.
 **************************************************************************************************/

#include "FilesystemDirector.h"

#include <algorithm>
#include <cwctype>
#include <optional>
#include <string_view>

#include "ApiWindows.h"
#include "DebugAssert.h"
#include "FilesystemInstruction.h"
#include "FilesystemOperations.h"
#include "FilesystemRule.h"
#include "Message.h"
#include "PrefixTree.h"
#include "Strings.h"

namespace Pathwinder
{
  /// Identify the rule from within the container that was responsible for performing a redirection
  /// from a path on that rule's origin side to the specified redirected path, which would be on the
  /// rule's target sidde.
  /// @param [in] rules Container of rules to search.
  /// @param [in] redirectedPath Path that resulted from doing a redirection using that filesystem
  /// rule.
  /// @return Rule that did the redirection, or `nullptr` if no such rule exists.
  static const FilesystemRule* IdentifyRuleThatPerformedRedirection(
      const RelatedFilesystemRuleContainer& rules, std::wstring_view redirectedPath)
  {
    for (const auto& rule : rules.AllRules())
    {
      switch (rule.DirectoryCompareWithTarget(redirectedPath))
      {
        case EDirectoryCompareResult::Equal:
        case EDirectoryCompareResult::CandidateIsChild:
        case EDirectoryCompareResult::CandidateIsDescendant:
          return &rule;
      }
    }

    return nullptr;
  }

  /// Selects a rule to use for determining which real directory to query for enumeration data
  /// during a name insertion.
  /// @param [in] possibleRules Container of possible rules from which to select.
  /// @return Reference to the rule that was selected.
  static const FilesystemRule& SelectRuleForDirectoryNameInsertionSource(
      const RelatedFilesystemRuleContainer& possibleRules)
  {
    for (const auto& possibleRule : possibleRules.AllRules())
    {
      // All rules in the container have the same origin directory, by design. However, they all
      // have different target directories, and which rule to use to get directory information
      // depends on which target directories exist. The first rule in the container whose target
      // directory exists in the filesystem as a directory is chosen.
      if (FilesystemOperations::IsDirectory(possibleRule.GetTargetDirectoryFullPath()))
        return possibleRule;
    }

    // If no rules have target directories that really exist then it does not matter which rule is
    // selected. The name insertion will not take place because the underlying query to the rule's
    // target directory will fail.
    return possibleRules.AnyRule();
  }

  const RelatedFilesystemRuleContainer* FilesystemDirector::SelectRulesForPath(
      std::wstring_view absolutePath) const
  {
    // It is possible that multiple rules all have a prefix that matches the directory part of
    // the full file path. We want to pick the most specific one to apply, meaning it has the
    // longest matching prefix. For example, suppose two rules exist with "C:\Dir1\Dir2" and
    // "C:\Dir1" as their respective origin directories. A file having full path
    // "C:\Dir1\Dir2\textfile.txt" would need to use "C:\Dir1\Dir2" even though technically both
    // rules do match.

    auto ruleNode = filesystemRulesByOriginDirectory.LongestMatchingPrefix(absolutePath);
    if (nullptr == ruleNode) return nullptr;

    return &ruleNode->GetData();
  }

  DirectoryEnumerationInstruction FilesystemDirector::GetInstructionForDirectoryEnumeration(
      std::wstring_view associatedPath, std::wstring_view realOpenedPath) const
  {
    associatedPath = Strings::RemoveTrailing(associatedPath, L'\\');
    realOpenedPath = Strings::RemoveTrailing(realOpenedPath, L'\\');

    // This method's implementation takes advantage of, and depends on, the design and
    // implementation of another method for redirecting file operations. If this method is
    // queried for a directory enumeration, then that means a previous file operation resulted
    // in a directory handle being opened using either an unredirected or a redirected path and
    // associated with an unredirected path.
    //
    // There are three parts to a complete directory enumeration operation:
    //
    // (1) Enumerate the contents of one or more redirected directory paths, potentially subject to
    // file pattern matching with associated filesystem rules. This captures all of the files that
    // exist on the target side of any applicable filesystem rules. There may be more than one
    // filesystem rule when the enumeration is for an origin directory.
    //
    // (2) Enumerate the contents of the unredirected directory path, for
    // anything that exists on the origin side but is beyond the scope of the filesystem rule.
    //
    // (3) Insert specific subdirectories into the enumeration results provided back to the
    // application. This captures filesystem rules whose origin directories do not actually exist in
    // the real filesystem on the origin side.
    //
    // Parts 1 and 2 are only interesting if a redirection took place. Otherwise there is no reason
    // to merge directory contents on the origin side with directory contents on the target side.
    // Part 3 is always potentially interesting. Whether or not a redirection took place, the path
    // being queried for directory enumeration may have filesystem rule origin directories as direct
    // children, and these would potentially need to be enumerated.

    DirectoryEnumerationInstruction::TDirectoriesToEnumerate directoriesToEnumerate;
    if (associatedPath != realOpenedPath)
    {
      // This block implements parts 1 and 2.
      // A redirection took place when opening the directory handle.
      // The open directory handle is already on the target side, so it is potentially
      // necessary to do a merge with the origin side. Whether or not a merge is needed
      // depends on the scope of the filesystem rule that did the redirection. Any files
      // outside its scope should show up on the origin side, all others should show up on the
      // target side.

      std::wstring_view& redirectedPath = realOpenedPath;
      std::wstring_view& unredirectedPath = associatedPath;

      const std::wstring_view redirectedPathWindowsNamespacePrefix =
          Strings::PathGetWindowsNamespacePrefix(redirectedPath);
      const std::wstring_view redirectedPathTrimmedForQuery =
          redirectedPath.substr(redirectedPathWindowsNamespacePrefix.length());

      const std::wstring_view unredirectedPathWindowsNamespacePrefix =
          Strings::PathGetWindowsNamespacePrefix(unredirectedPath);
      const std::wstring_view unredirectedPathTrimmedForQuery =
          unredirectedPath.substr(unredirectedPathWindowsNamespacePrefix.length());

      const RelatedFilesystemRuleContainer* directoryEnumerationRules =
          SelectRulesForPath(unredirectedPathTrimmedForQuery);
      if (nullptr == directoryEnumerationRules)
      {
        Message::OutputFormatted(
            Message::ESeverity::Error,
            L"Directory enumeration query for path \"%.*s\" did not match any rules due to an internal error.",
            static_cast<int>(associatedPath.length()),
            associatedPath.data());
        return DirectoryEnumerationInstruction::PassThroughUnmodifiedQuery();
      }

      const FilesystemRule* originalRedirectRule = IdentifyRuleThatPerformedRedirection(
          *directoryEnumerationRules, redirectedPathTrimmedForQuery);
      if (nullptr == originalRedirectRule)
      {
        Message::OutputFormatted(
            Message::ESeverity::Error,
            L"Directory enumeration query for path \"%.*s\" matched rules but failed because the original rule could not be identified due to an internal error.",
            static_cast<int>(associatedPath.length()),
            associatedPath.data());
        return DirectoryEnumerationInstruction::PassThroughUnmodifiedQuery();
      }

      switch (directoryEnumerationRules->AnyRule().DirectoryCompareWithOrigin(
          unredirectedPathTrimmedForQuery))
      {
        case EDirectoryCompareResult::Equal:

          // The directory being enumerated is exactly equal to the selected rules' origin
          // directory. All of the selected rules need to be consulted, and each can generate a
          // separate target-side enumeration that needs to be merged into the enumeration output
          // provided back to the requesting application.

          do
          {
            int numSimpleRedirectModeRules = 0;
            int numOverlayRedirectModeRules = 0;
            bool containsSimpleRedirectModeRuleWithNoFilePatterns = false;

            if (1 == directoryEnumerationRules->CountOfRules())
            {
              // A major simplification is available in the common case of one rule per origin
              // directory. There is only one rule to consult, and it is directly known how it will
              // behave.
              const FilesystemRule& directoryEnumerationRule = *originalRedirectRule;

              if (true == directoryEnumerationRule.HasFilePatterns())
              {
                directoriesToEnumerate.emplace_back(
                    DirectoryEnumerationInstruction::SingleDirectoryEnumeration::
                        IncludeOnlyMatchingFilenames(
                            EDirectoryPathSource::RealOpenedPath, directoryEnumerationRule));
              }
              else
              {
                directoriesToEnumerate.emplace_back(
                    DirectoryEnumerationInstruction::SingleDirectoryEnumeration::
                        IncludeAllFilenames(EDirectoryPathSource::RealOpenedPath));
              }

              Message::OutputFormatted(
                  Message::ESeverity::Debug,
                  L"Directory enumeration query for path \"%.*s\" matches rule \"%.*s\" and will include in-scope contents of \"%.*s\" in the output.",
                  static_cast<int>(unredirectedPath.length()),
                  unredirectedPath.data(),
                  static_cast<int>(directoryEnumerationRule.GetName().length()),
                  directoryEnumerationRule.GetName().data(),
                  static_cast<int>(directoryEnumerationRule.GetTargetDirectoryFullPath().length()),
                  directoryEnumerationRule.GetTargetDirectoryFullPath().data());

              switch (directoryEnumerationRule.GetRedirectMode())
              {
                case ERedirectMode::Simple:
                  numSimpleRedirectModeRules = 1;
                  if (false == directoryEnumerationRule.HasFilePatterns())
                    containsSimpleRedirectModeRuleWithNoFilePatterns = true;
                  break;

                case ERedirectMode::Overlay:
                  numOverlayRedirectModeRules = 1;
                  break;

                default:
                  break;
              }
            }
            else
            {
              RelatedFilesystemRuleContainer::TFilesystemRulesIndex ruleIndex = 0;
              for (const FilesystemRule& directoryEnumerationRule :
                   directoryEnumerationRules->AllRules())
              {
                // If multiple filesystem rules are associated with the same origin directory then
                // they are processed in order of precedence. Any scopes that are overlapping
                // between the file patterns are resolved in favor of the rules that come earlier in
                // the container. Inverting all matches prior to the current rule means that we are
                // explicitly excluding from the scope of the current rule any file names that match
                // earlier rules.

                directoriesToEnumerate.emplace_back(
                    DirectoryEnumerationInstruction::SingleDirectoryEnumeration::
                        IncludeOnlyMatchingFilenames(
                            EDirectoryPathSource::FilePatternSourceTargetDirectory,
                            *directoryEnumerationRules,
                            EFilePatternMatchCondition::MatchByPositionInvertAllPriorToSelected,
                            ruleIndex));

                Message::OutputFormatted(
                    Message::ESeverity::Debug,
                    L"Directory enumeration query for path \"%.*s\" matches rule \"%.*s\" and will include in-scope contents of \"%.*s\" in the output.",
                    static_cast<int>(unredirectedPath.length()),
                    unredirectedPath.data(),
                    static_cast<int>(directoryEnumerationRule.GetName().length()),
                    directoryEnumerationRule.GetName().data(),
                    static_cast<int>(
                        directoryEnumerationRule.GetTargetDirectoryFullPath().length()),
                    directoryEnumerationRule.GetTargetDirectoryFullPath().data());

                switch (directoryEnumerationRule.GetRedirectMode())
                {
                  case ERedirectMode::Simple:
                    numSimpleRedirectModeRules += 1;
                    if (false == directoryEnumerationRule.HasFilePatterns())
                      containsSimpleRedirectModeRuleWithNoFilePatterns = true;
                    break;

                  case ERedirectMode::Overlay:
                    numOverlayRedirectModeRules += 1;
                    break;

                  default:
                    break;
                }

                ruleIndex += 1;
              }
            }

            // None of the files on the origin side will show up in the enumeration output if all of
            // the filesystem rules use Simple as their redirection mode and at least one of them
            // has no file patterns and hence matches all files.
            const bool originSideContentsCompletelyReplaced =
                ((0 == numOverlayRedirectModeRules) &&
                 containsSimpleRedirectModeRuleWithNoFilePatterns);

            // If any rule uses Simple as its redirection mode then it could hide real files on
            // the origin side.
            const bool originSideContentsPartiallyReplaced = (0 < numSimpleRedirectModeRules);

            if (false == originSideContentsCompletelyReplaced)
            {
              // Inside this block is a series of potential simplifications for the directory
              // enumeration instruction that is generated to enumerate the real contents of the
              // origin side directory, if necessary. The purpose of all these instructions is the
              // same, but some forms are simpler to represent and execute than others.

              if (true == originSideContentsPartiallyReplaced)
              {
                // To implement hiding of files on the origin side, the enumeration process needs to
                // verify an inverted match with those rules' file patterns. It is simpler to
                // represent and execute a single-rule instruction, and that is also the most common
                // expected case.

                if (1 == directoryEnumerationRules->CountOfRules())
                {
                  // Some files on the origin side may need to be hidden, but there is only one
                  // filesystem rule to be consulted. It is already known that this filesystem rule
                  // uses Simple redirect mode. Anything in-scope for that rule needs to be
                  // excluded.

                  directoriesToEnumerate.emplace_back(
                      DirectoryEnumerationInstruction::SingleDirectoryEnumeration::
                          IncludeAllExceptMatchingFilenames(
                              EDirectoryPathSource::AssociatedPath, *originalRedirectRule));
                }
                else
                {
                  // Some files on the origin side may need to be hidden, and multiple rules need to
                  // be consulted. The logic for this is not trivial. For each file, the container
                  // needs to be queried for the first rule that matches based on file patterns. If
                  // that rule uses Overlay mode, then the file should be shown in the enumeration
                  // output, but if the rule uses Simple mode, then it should be hidden.

                  directoriesToEnumerate.emplace_back(
                      DirectoryEnumerationInstruction::SingleDirectoryEnumeration::
                          IncludeAllExceptMatchingFilenames(
                              EDirectoryPathSource::AssociatedPath,
                              *directoryEnumerationRules,
                              EFilePatternMatchCondition::MatchByRedirectModeInvertOverlay));
                }
              }
              else
              {
                // No hiding of files on the origin side means no need to check for any
                // file pattern matches. This is a simpler case to represent and execute.

                directoriesToEnumerate.emplace_back(
                    DirectoryEnumerationInstruction::SingleDirectoryEnumeration::
                        IncludeAllFilenames(EDirectoryPathSource::AssociatedPath));
              }

              Message::OutputFormatted(
                  Message::ESeverity::Debug,
                  L"Directory enumeration query for path \"%.*s\" will additionally include in the output any contents of the original query path not hidden or already enumerated by matching filesystem rules.",
                  static_cast<int>(unredirectedPath.length()),
                  unredirectedPath.data());
            }
          }
          while (false);
          break;

        case EDirectoryCompareResult::CandidateIsChild:
        case EDirectoryCompareResult::CandidateIsDescendant:

          // The directory being enumerated is a descendant of the selected rules' origin directory,
          // and one of the rules in the container previously resulted in a redirection. This is
          // actually a relatively simple case because we only need to interact with a single rule,
          // the one that did the original redirection, and ignore the rest of them.

          do
          {
            switch (originalRedirectRule->GetRedirectMode())
            {
              case ERedirectMode::Simple:

                // In simple mode, the target-side contents that are in the filesystem rule scope
                // replace the contents of any contents on the origin side that are also in the
                // filesystem rule scope based on file patterns. Here, it is already known that the
                // directory being enumerated is somehow in scope, so the target-side directory
                // contents completely replace any origin-side directory contents.

                Message::OutputFormatted(
                    Message::ESeverity::Debug,
                    L"Directory enumeration query for path \"%.*s\" matches rule \"%.*s\" and will instead enumerate \"%.*s\".",
                    static_cast<int>(unredirectedPath.length()),
                    unredirectedPath.data(),
                    static_cast<int>(originalRedirectRule->GetName().length()),
                    originalRedirectRule->GetName().data(),
                    static_cast<int>(redirectedPath.length()),
                    redirectedPath.data());
                directoriesToEnumerate = {
                    DirectoryEnumerationInstruction::SingleDirectoryEnumeration::
                        IncludeAllFilenames(EDirectoryPathSource::RealOpenedPath)};
                break;

              case ERedirectMode::Overlay:

                // In overlay mode, the target-side contents that are in the filesystem rule scope
                // are always enumerated and merged with the origin-side contents regardless of
                // scope in the latter case. Here, it is already known that the directory being
                // enumerated is somehow in scope, so the target-side directory contents are merged
                // with any origin-side directory contents.

                Message::OutputFormatted(
                    Message::ESeverity::Debug,
                    L"Directory enumeration query for path \"%.*s\" matches rule \"%.*s\" and will overlay the contents of \"%.*s\".",
                    static_cast<int>(unredirectedPath.length()),
                    unredirectedPath.data(),
                    static_cast<int>(originalRedirectRule->GetName().length()),
                    originalRedirectRule->GetName().data(),
                    static_cast<int>(redirectedPath.length()),
                    redirectedPath.data());
                directoriesToEnumerate = {
                    DirectoryEnumerationInstruction::SingleDirectoryEnumeration::
                        IncludeAllFilenames(EDirectoryPathSource::RealOpenedPath),
                    DirectoryEnumerationInstruction::SingleDirectoryEnumeration::
                        IncludeAllFilenames(EDirectoryPathSource::AssociatedPath)};
                break;

              default:
                Message::OutputFormatted(
                    Message::ESeverity::Error,
                    L"Directory enumeration query for path \"%.*s\" matched rule \"%.*s\" but failed because of an invalid redirection mode due to an internal error.",
                    static_cast<int>(associatedPath.length()),
                    associatedPath.data(),
                    static_cast<int>(originalRedirectRule->GetName().length()),
                    originalRedirectRule->GetName().data());
                return DirectoryEnumerationInstruction::PassThroughUnmodifiedQuery();
            }
          }
          while (false);
          break;

        default:
          Message::OutputFormatted(
              Message::ESeverity::Error,
              L"Directory enumeration query for path \"%.*s\" matched rules but failed a directory hierarchy consistency check due to an internal error.",
              static_cast<int>(associatedPath.length()),
              associatedPath.data());
          return DirectoryEnumerationInstruction::PassThroughUnmodifiedQuery();
      }
    }
    else
    {
      // If a redirection did not take place then the contents of the requested directory
      // still need to be enumerated as is.
      Message::OutputFormatted(
          Message::ESeverity::SuperDebug,
          L"Directory enumeration query for path \"%.*s\" does not match any rules.",
          static_cast<int>(realOpenedPath.length()),
          realOpenedPath.data());
      directoriesToEnumerate = {
          DirectoryEnumerationInstruction::SingleDirectoryEnumeration::IncludeAllFilenames(
              EDirectoryPathSource::RealOpenedPath)};
    }

    std::optional<TemporaryVector<DirectoryEnumerationInstruction::SingleDirectoryNameInsertion>>
        directoryNamesToInsert;
    do
    {
      // This block implements part 3.
      // It is potentially necessary to insert potential origin directories into the
      // enumeration result. To do that, traverse the rule tree to the node that represents
      // the path associated internally with the file handle. Children of that node may
      // contain filesystem rules, in which case those origin directories should be inserted
      // into the enumeration results if their corresponding target directories actually exist
      // in the real filesystem.

      std::wstring_view& directoryPath = associatedPath;

      const std::wstring_view directoryPathWindowsNamespacePrefix =
          Strings::PathGetWindowsNamespacePrefix(directoryPath);
      const std::wstring_view directoryPathTrimmedForQuery =
          directoryPath.substr(directoryPathWindowsNamespacePrefix.length());

      auto parentOfDirectoriesToInsert =
          filesystemRulesByOriginDirectory.TraverseTo(directoryPathTrimmedForQuery);
      if (nullptr != parentOfDirectoriesToInsert)
      {
        for (const auto& childItem : parentOfDirectoriesToInsert->GetChildren())
        {
          if (true == childItem.second.HasData())
          {
            const FilesystemRule& childRule =
                SelectRuleForDirectoryNameInsertionSource(childItem.second.GetData());

            // Insertion of a rule's origin directory into the enumeration results
            // requires that two things be true: (1) Origin directory base name matches
            // the application-supplied enumeration quiery file pattern (or the
            // enumeration query file pattern is missing). This check is needed because
            // insertion bypasses the normal mechanism of having the system check for a
            // match during the enumeration system call. (2) Target directory exists as
            // a real directory in the filesystem. However, both of these things need to
            // be checked at insertion time, not at instruction creation time.

            if (false == directoryNamesToInsert.has_value()) directoryNamesToInsert.emplace();

            Message::OutputFormatted(
                Message::ESeverity::Info,
                L"Directory enumeration query for path \"%.*s\" will potentially insert \"%.*s\" into the output because it is the origin directory of rule \"%.*s\".",
                static_cast<int>(directoryPath.length()),
                directoryPath.data(),
                static_cast<int>(childRule.GetOriginDirectoryName().length()),
                childRule.GetOriginDirectoryName().data(),
                static_cast<int>(childRule.GetName().length()),
                childRule.GetName().data());

            directoryNamesToInsert->EmplaceBack(childRule);
          }
        }
      }

      if (true == directoryNamesToInsert.has_value())
      {
        // Directory enumeration operations often present files in sorted order. To preserve
        // this behavior, the names of directories to be inserted are sorted.
        std::sort(
            directoryNamesToInsert->begin(),
            directoryNamesToInsert->end(),
            [](const DirectoryEnumerationInstruction::SingleDirectoryNameInsertion& a,
               const DirectoryEnumerationInstruction::SingleDirectoryNameInsertion& b) -> bool
            {
              return (
                  Strings::CompareCaseInsensitive(a.FileNameToInsert(), b.FileNameToInsert()) < 0);
            });
      }
    }
    while (false);

    return DirectoryEnumerationInstruction(
        std::move(directoriesToEnumerate), std::move(directoryNamesToInsert));
  }

  FileOperationInstruction FilesystemDirector::GetInstructionForFileOperation(
      std::wstring_view absoluteFilePath,
      FileAccessMode fileAccessMode,
      CreateDisposition createDisposition) const
  {
    const std::wstring_view windowsNamespacePrefix =
        Strings::PathGetWindowsNamespacePrefix(absoluteFilePath);
    const std::wstring_view extraSuffix =
        ((true == absoluteFilePath.ends_with(L'\\')) ? L"\\" : L"");
    const std::wstring_view absoluteFilePathTrimmedForQuery =
        Strings::RemoveTrailing(absoluteFilePath.substr(windowsNamespacePrefix.length()), L'\\');

    if (false == Strings::PathBeginsWithDriveLetter(absoluteFilePathTrimmedForQuery))
    {
      Message::OutputFormatted(
          Message::ESeverity::SuperDebug,
          L"File operation redirection query for path \"%.*s\" does not begin with a drive letter and was therefore skipped for redirection.",
          static_cast<int>(absoluteFilePath.length()),
          absoluteFilePath.data());
      return FileOperationInstruction::NoRedirectionOrInterception();
    }

    const size_t lastSeparatorPos = absoluteFilePathTrimmedForQuery.find_last_of(L'\\');
    if (std::wstring_view::npos == lastSeparatorPos)
    {
      Message::OutputFormatted(
          Message::ESeverity::SuperDebug,
          L"File operation redirection query for path \"%.*s\" does not contain a final path separator and was therefore skipped for redirection.",
          static_cast<int>(absoluteFilePath.length()),
          absoluteFilePath.data());
      return FileOperationInstruction::NoRedirectionOrInterception();
    }

    const auto selectedRuleContainer = SelectRulesForPath(absoluteFilePathTrimmedForQuery);

    if (nullptr == selectedRuleContainer)
    {
      Message::OutputFormatted(
          Message::ESeverity::SuperDebug,
          L"File operation redirection query for path \"%.*s\" did not match any rules.",
          static_cast<int>(absoluteFilePath.length()),
          absoluteFilePath.data());

      if (true == IsPrefixForAnyRule(absoluteFilePathTrimmedForQuery))
      {
        // If the file path could possibly be a directory path that but exists in the
        // hierarchy as an ancestor of filesystem rules, then it is possible this same path
        // could be a relative root path later on for a something that needs to be
        // redirected. Therefore, if a file handle is being created, it needs to be
        // associated with the unredirected path.
        return FileOperationInstruction::InterceptWithoutRedirection(
            EAssociateNameWithHandle::Unredirected);
      }
      else
      {
        // Otherwise, an unredirected file path is not interesting and can be safely passed
        // to the system without any further processing. The path specified is totally
        // unrelated to all filesystem rules.
        return FileOperationInstruction::NoRedirectionOrInterception();
      }
    }

    std::wstring_view unredirectedPathDirectoryPart;
    std::wstring_view unredirectedPathDirectoryPartWithWindowsNamespacePrefix;
    std::wstring_view unredirectedPathFilePart;
    std::optional<TemporaryString> maybeRedirectedFilePath;
    const FilesystemRule* selectedRule = nullptr;

    if (EDirectoryCompareResult::Equal ==
        selectedRuleContainer->AnyRule().DirectoryCompareWithOrigin(
            absoluteFilePathTrimmedForQuery))
    {
      // If the input path is exactly equal to the origin directory for one or more filesystem
      // rules, then the entire input path is one big directory path, and the file part does not
      // exist. Redirection can occur to any target directory.
      selectedRule = &selectedRuleContainer->AnyRule();

      unredirectedPathDirectoryPart = absoluteFilePathTrimmedForQuery;
      unredirectedPathDirectoryPartWithWindowsNamespacePrefix = absoluteFilePath.substr(
          0, windowsNamespacePrefix.length() + absoluteFilePathTrimmedForQuery.length());

      maybeRedirectedFilePath = selectedRule->RedirectPathOriginToTarget(
          unredirectedPathDirectoryPart,
          unredirectedPathFilePart,
          windowsNamespacePrefix,
          extraSuffix);
      if (false == maybeRedirectedFilePath.has_value())
      {
        Message::OutputFormatted(
            Message::ESeverity::Error,
            L"File operation redirection query for path \"%.*s\" did not match rule \"%.*s\" due to an internal error.",
            static_cast<int>(absoluteFilePath.length()),
            absoluteFilePath.data(),
            static_cast<int>(selectedRule->GetName().length()),
            selectedRule->GetName().data());
        return FileOperationInstruction::NoRedirectionOrInterception();
      }

      if (selectedRuleContainer->CountOfRules() > 1)
      {
        Message::OutputFormatted(
            Message::ESeverity::Info,
            L"File operation redirection query for path \"%.*s\" is for the origin directory of multiple rules and was redirected to \"%s\" using arbitrarily-chosen rule \"%.*s\".",
            static_cast<int>(absoluteFilePath.length()),
            absoluteFilePath.data(),
            maybeRedirectedFilePath->AsCString(),
            static_cast<int>(selectedRule->GetName().length()),
            selectedRule->GetName().data());
      }
      else
      {
        Message::OutputFormatted(
            Message::ESeverity::Info,
            L"File operation redirection query for path \"%.*s\" is for the origin directory of rule \"%.*s\" and was redirected to \"%s\".",
            static_cast<int>(absoluteFilePath.length()),
            absoluteFilePath.data(),
            static_cast<int>(selectedRule->GetName().length()),
            selectedRule->GetName().data(),
            maybeRedirectedFilePath->AsCString());
      }
    }
    else
    {
      // If the input path is something else, then it is safe to split it at the last path
      // separator into a directory part and a file part.
      unredirectedPathDirectoryPart = absoluteFilePathTrimmedForQuery.substr(0, lastSeparatorPos);
      unredirectedPathDirectoryPartWithWindowsNamespacePrefix =
          absoluteFilePath.substr(0, windowsNamespacePrefix.length() + lastSeparatorPos);
      unredirectedPathFilePart = absoluteFilePathTrimmedForQuery.substr(1 + lastSeparatorPos);

      // Rules have their own internal logic for determining which part of the path to check against
      // file patterns, which may differ from the path split above. For example, the directory part
      // could be a child or descendant of the origin directory. In the common case there will only
      // be a single rule, so only one rule would ever be checked.
      for (const auto& candidateRule : selectedRuleContainer->AllRules())
      {
        maybeRedirectedFilePath = candidateRule.RedirectPathOriginToTarget(
            unredirectedPathDirectoryPart,
            unredirectedPathFilePart,
            windowsNamespacePrefix,
            extraSuffix);

        if (true == maybeRedirectedFilePath.has_value())
        {
          selectedRule = &candidateRule;
          break;
        }
      }

      if (false == maybeRedirectedFilePath.has_value())
      {
        std::wstring_view selectedOriginDirectory =
            selectedRuleContainer->AnyRule().GetOriginDirectoryFullPath();
        Message::OutputFormatted(
            Message::ESeverity::Info,
            L"File operation redirection query for path \"%.*s\" did not match any rules because no file patterns match for rules having origin directory \"%.*s\".",
            static_cast<int>(absoluteFilePath.length()),
            absoluteFilePath.data(),
            static_cast<int>(selectedOriginDirectory.length()),
            selectedOriginDirectory.data());
        return FileOperationInstruction::NoRedirectionOrInterception();
      }

      Message::OutputFormatted(
          Message::ESeverity::Info,
          L"File operation redirection query for path \"%.*s\" matched rule \"%s\" and was redirected to \"%s\".",
          static_cast<int>(absoluteFilePath.length()),
          absoluteFilePath.data(),
          selectedRule->GetName().data(),
          maybeRedirectedFilePath->AsCString());
    }

    DebugAssert(
        nullptr != selectedRule, "A rule was selected and used for redirection but it is null.");

    std::wstring_view redirectedFilePath = maybeRedirectedFilePath->AsStringView();

    BitSetEnum<EExtraPreOperation> extraPreOperations;
    std::wstring_view extraPreOperationOperand;

    if (true == createDisposition.AllowsCreateNewFile())
    {
      // If the filesystem operation can result in file creation, then it must be possible to
      // complete file creation in the target hierarchy if it would also be possible to do so
      // in the origin hierarchy. In this situation it is necessary to ensure that the
      // target-side hierarchy exists up to the directory containing the file that is to be
      // potentially created, if said hierarchy also exists on the origin side either as a real
      // directory or as the origin directory for a filesystem rule.

      if (filesystemRulesByOriginDirectory.Contains(unredirectedPathDirectoryPart) ||
          FilesystemOperations::IsDirectory(
              unredirectedPathDirectoryPartWithWindowsNamespacePrefix))
      {
        extraPreOperations.insert(static_cast<int>(EExtraPreOperation::EnsurePathHierarchyExists));
        extraPreOperationOperand = Strings::RemoveTrailing(
            redirectedFilePath.substr(0, redirectedFilePath.find_last_of(L'\\')), L'\\');
      }
    }
    else
    {
      // If the filesystem operation cannot result in file creation, then it is possible that
      // the operation is targeting a directory that exists in the origin hierarchy. In this
      // situation it is necessary to ensure that the same directory also exists in the target
      // hierarchy. This is required because the directory access is being redirected from the
      // origin side to the target side, and it would be incorrect for the access to fail due to
      // file-not-found if the requested directory exists on the origin side.

      if (FilesystemOperations::IsDirectory(absoluteFilePath))
      {
        extraPreOperations.insert(static_cast<int>(EExtraPreOperation::EnsurePathHierarchyExists));
        extraPreOperationOperand = Strings::RemoveTrailing(redirectedFilePath, L'\\');
      }
    }

    switch (selectedRule->GetRedirectMode())
    {
      case ERedirectMode::Overlay:
      {
        // In overlay redirection mode, if the application is willing to create a new
        // file, then a preference towards already-existing files needs to be set in the
        // instruction. There are two situations to consider, both of which involve the
        // file existing on the origin side but not existing on the target side. In both
        // cases, if no preference is set, the outcome is incorrect in that the file
        // ends up being created on the target side. (1) If the application is
        // additionally willing to accept opening an existing file, then the correct
        // outcome is that the existing file on the origin side is opened. (2) If the
        // application is only willing to accept creating a new file, then the correct
        // outcome is that the operation fails because the file already exists on the
        // origin side. It is up to the caller to interpret the combination of
        // preference, which is encoded in the returned instruction, and create
        // disposition, which is supplied by the application.

        const ECreateDispositionPreference createDispositionPreference =
            ((true == createDisposition.AllowsCreateNewFile())
                 ? ECreateDispositionPreference::PreferOpenExistingFile
                 : ECreateDispositionPreference::NoPreference);
        return FileOperationInstruction::OverlayRedirectTo(
            std::move(*maybeRedirectedFilePath),
            EAssociateNameWithHandle::Unredirected,
            createDispositionPreference,
            std::move(extraPreOperations),
            extraPreOperationOperand);
      }

      case ERedirectMode::Simple:
      {
        // In simple redirection mode there is nothing further to do. Only one file is
        // attempted, so no preference based on create disposition needs to be set.
        return FileOperationInstruction::SimpleRedirectTo(
            std::move(*maybeRedirectedFilePath),
            EAssociateNameWithHandle::Unredirected,
            std::move(extraPreOperations),
            extraPreOperationOperand);
      }
    }

    Message::OutputFormatted(
        Message::ESeverity::Error,
        L"Internal error: unrecognized file redirection mode (ERedirectMode = %u) encountered while processing file operation redirection query for path \"%.*s\".",
        static_cast<unsigned int>(selectedRule->GetRedirectMode()),
        static_cast<int>(absoluteFilePath.length()),
        absoluteFilePath.data());
    return FileOperationInstruction::NoRedirectionOrInterception();
  }
} // namespace Pathwinder
