/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 ***********************************************************************************************//**
 * @file FilesystemTypes.h
 *   Declaration of types and objects commonly used throughout all filesystem-related
 *   functionality.
 **************************************************************************************************/

#pragma once

#include <bitset>
#include <cstdint>

namespace Pathwinder
{
    /// Identifies a create disposition setting based on what types of file accesses are allowed.
    /// Immutable once constructed.
    class CreateDisposition
    {
    public:

        /// Enumerates supported create disposition settings. These can exist in combination.
        enum class ECreateDisposition : uint8_t
        {
            /// Specifies that a new file can be created.
            CreateNewFile,

            /// Specifies that an existing file can be opened.
            OpenExistingFile,

            /// Not used as a value. Identifies the number of enumerators present in this
            /// enumeration.
            Count
        };

        constexpr CreateDisposition(bool canCreateNewFile, bool canOpenExistingFile)
            : createDispositionBits()
        {
            createDispositionBits[static_cast<size_t>(ECreateDisposition::CreateNewFile)] =
                canCreateNewFile;
            createDispositionBits[static_cast<size_t>(ECreateDisposition::OpenExistingFile)] =
                canOpenExistingFile;
        }

        /// Creates an object of this class type that encodes only allowing creation of a new file.
        /// @return Initialized object of this class.
        static constexpr CreateDisposition CreateNewFile(void)
        {
            return CreateDisposition(true, false);
        }

        /// Creates an object of this class type that encodes allowing both creation of a new file
        /// or opening of an existing file.
        /// @return Initialized object of this class.
        static constexpr CreateDisposition CreateNewOrOpenExistingFile(void)
        {
            return CreateDisposition(true, true);
        }

        /// Creates an object of this class type that encodes only allowing opening of an
        /// existingfile.
        /// @return Initialized object of this class.
        static constexpr CreateDisposition OpenExistingFile(void)
        {
            return CreateDisposition(false, true);
        }

        /// Determines whether or not the represented create disposition allows a new file to be
        /// created.
        /// @return `true` if so, `false` if not.
        constexpr bool AllowsCreateNewFile(void) const
        {
            return createDispositionBits[static_cast<size_t>(ECreateDisposition::CreateNewFile)];
        }

        /// Determines whether or not the represented create disposition allows an existing file to
        /// be opened.
        /// @return `true` if so, `false` if not.
        constexpr bool AllowsOpenExistingFile(void) const
        {
            return createDispositionBits[static_cast<size_t>(ECreateDisposition::OpenExistingFile)];
        }

    private:

        /// Holds the create disposition possibilities themselves.
        std::bitset<static_cast<size_t>(ECreateDisposition::Count)> createDispositionBits;
    };

    /// Identifies a file access mode based on what types of operations are allowed.
    /// Immutable once constructed.
    class FileAccessMode
    {
    public:

        /// Enumerates supported file access modes. These can exist in combination.
        enum class EFileAccessMode : uint8_t
        {
            /// Application is requesting to be able to read from the file.
            Read,

            /// Application is requesting to be able to write from the file.
            Write,

            /// Application is requesting to be able to delete the file.
            Delete,

            /// Not used as a value. Identifies the number of enumerators present in this
            /// enumeration.
            Count
        };

        constexpr FileAccessMode(bool canRead, bool canWrite, bool canDelete) : accessModeBits()
        {
            accessModeBits[static_cast<size_t>(EFileAccessMode::Read)] = canRead;
            accessModeBits[static_cast<size_t>(EFileAccessMode::Write)] = canWrite;
            accessModeBits[static_cast<size_t>(EFileAccessMode::Delete)] = canDelete;
        }

        /// Creates an object of this class that encodes only allowing read access to a file.
        /// @return Initialized object of this class.
        static constexpr FileAccessMode ReadOnly(void)
        {
            return FileAccessMode(true, false, false);
        }

        /// Creates an object of this class that encodes allowing read and write access to a file.
        /// @return Initialized object of this class.
        static constexpr FileAccessMode ReadWrite(void)
        {
            return FileAccessMode(true, true, false);
        }

        /// Creates an object of this class that encodes allowing delete access to a file.
        /// @return Initialized object of this class.
        static constexpr FileAccessMode Delete(void)
        {
            return FileAccessMode(false, false, true);
        }

        /// Determines whether or not the represented file access mode allows reading.
        /// @return `true` if so, `false` if not.
        constexpr bool AllowsRead(void) const
        {
            return accessModeBits[static_cast<size_t>(EFileAccessMode::Read)];
        }

        /// Determines whether or not the represented file access mode allows writing.
        /// @return `true` if so, `false` if not.
        constexpr bool AllowsWrite(void) const
        {
            return accessModeBits[static_cast<size_t>(EFileAccessMode::Write)];
        }

        /// Determines whether or not the represented file access mode allows deletion.
        /// @return `true` if so, `false` if not.
        constexpr bool AllowsDelete(void) const
        {
            return accessModeBits[static_cast<size_t>(EFileAccessMode::Delete)];
        }

    private:

        /// Holds the requested file access modes themselves.
        std::bitset<static_cast<size_t>(EFileAccessMode::Count)> accessModeBits;
    };
}  // namespace Pathwinder
