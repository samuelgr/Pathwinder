/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file Hooks.cpp
 *   Implementation of all Windows API hook functions used to implement path
 *   redirection.
 *****************************************************************************/

#include "ApiBitSet.h"
#include "ApiWindowsInternal.h"
#include "FileInformationStruct.h"
#include "FilesystemDirector.h"
#include "FilesystemInstruction.h"
#include "FilesystemOperations.h"
#include "Globals.h"
#include "Hooks.h"
#include "Message.h"
#include "OpenHandleStore.h"
#include "Strings.h"
#include "TemporaryBuffer.h"

#include <Hookshot/DynamicHook.h>

#include <atomic>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>


namespace Pathwinder
{
    // -------- INTERNAL TYPES --------------------------------------------- //

    /// Enumerates the possible modes for I/O using a file handle.
    enum class EInputOutputMode
    {
        Unknown,                                                            ///< I/O mode is not known. This represents an error case.
        Asynchronous,                                                       ///< I/O is asynchronous. System calls will return immediately, and completion information is provided out-of-band.
        Synchronous,                                                        ///< I/O is synchronous. System calls will return only after the requested operation completes.
        Count                                                               ///< Not used as a value. Identifies the number of enumerators present in this enumeration.
    };

    /// Holds file rename information, including the variably-sized filename, in a bytewise buffer and exposes it in a type-safe way.
    /// This object is used for representation only, not for any functionality surrounding actually creating a byte-wise buffer representation of a file rename information structure.
    class FileRenameInformationAndFilename
    {
    private:
        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// Byte-wise buffer, which will hold the file rename information data structure.
        TemporaryVector<uint8_t> bytewiseBuffer;


    public:
        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Initialization constructor. Requires an already-constructed bytewise buffer, which is moved into place.
        inline FileRenameInformationAndFilename(TemporaryVector<uint8_t>&& bytewiseBuffer) : bytewiseBuffer(std::move(bytewiseBuffer))
        {
            // Nothing to do here.
        }


        // -------- INSTANCE METHODS --------------------------------------- //

        /// Convenience method for retrieving a properly-typed file rename information structure.
        inline SFileRenameInformation& GetFileRenameInformation(void)
        {
            return *(reinterpret_cast<SFileRenameInformation*>(bytewiseBuffer.Data()));
        }

        /// Convenience method for retrieving the size, in bytes, of the stored file rename information structure including filename.
        inline unsigned int GetFileRenameInformationSizeBytes(void)
        {
            return bytewiseBuffer.Size();
        }
    };

    /// Contains all of the information associated with a file operation.
    struct SFileOperationContext
    {
        FileOperationInstruction instruction;                               ///< How the redirection should be performed.
        std::optional<TemporaryString> composedInputPath;                   ///< If an input path was composed, for example due to combination with a root directory, then that input path is stored here.
    };

    /// Contains all of the information needed to represent a file name and attributes in the format needed to interact with underlying system calls.
    struct SObjectNameAndAttributes
    {
        UNICODE_STRING objectName;                                          ///< Name of the object, as a Unicode string in the format supported by system calls.
        OBJECT_ATTRIBUTES objectAttributes;                                 ///< Attributes of the object, which includes a field that points to the name.
    };


    // -------- INTERNAL FUNCTIONS ----------------------------------------- //

    /// Generates a string representation of the specified access mask. Useful for logging.
    /// @param [in] accessMask Access mask, typically received from an application when creating or opening a file.
    /// @return String representation of the access mask.
    static TemporaryString AccessMaskToString(ACCESS_MASK accessMask)
    {
        constexpr std::wstring_view kSeparator = L" | ";
        TemporaryString outputString = Strings::FormatString(L"0x%08x (", accessMask);

        if (0 == accessMask)
        {
            outputString << L"none" << kSeparator;
        }
        else
        {
            if (FILE_ALL_ACCESS == (accessMask & FILE_ALL_ACCESS))
            {
                outputString << L"FILE_ALL_ACCESS" << kSeparator;
                accessMask &= (~(FILE_ALL_ACCESS));
            }

            if (FILE_GENERIC_READ == (accessMask & FILE_GENERIC_READ))
            {
                outputString << L"FILE_GENERIC_READ" << kSeparator;
                accessMask &= (~(FILE_GENERIC_READ));
            }

            if (FILE_GENERIC_WRITE == (accessMask & FILE_GENERIC_WRITE))
            {
                outputString << L"FILE_GENERIC_WRITE" << kSeparator;
                accessMask &= (~(FILE_GENERIC_WRITE));
            }

            if (FILE_GENERIC_EXECUTE == (accessMask & FILE_GENERIC_EXECUTE))
            {
                outputString << L"FILE_GENERIC_EXECUTE" << kSeparator;
                accessMask &= (~(FILE_GENERIC_EXECUTE));
            }

            if (0 != (accessMask & GENERIC_ALL))
                outputString << L"GENERIC_ALL" << kSeparator;
            if (0 != (accessMask & GENERIC_READ))
                outputString << L"GENERIC_READ" << kSeparator;
            if (0 != (accessMask & GENERIC_WRITE))
                outputString << L"GENERIC_WRITE" << kSeparator;
            if (0 != (accessMask & GENERIC_EXECUTE))
                outputString << L"GENERIC_EXECUTE" << kSeparator;
            if (0 != (accessMask & DELETE))
                outputString << L"DELETE" << kSeparator;
            if (0 != (accessMask & FILE_READ_DATA))
                outputString << L"FILE_READ_DATA" << kSeparator;
            if (0 != (accessMask & FILE_READ_ATTRIBUTES))
                outputString << L"FILE_READ_ATTRIBUTES" << kSeparator;
            if (0 != (accessMask & FILE_READ_EA))
                outputString << L"FILE_READ_EA" << kSeparator;
            if (0 != (accessMask & READ_CONTROL))
                outputString << L"READ_CONTROL" << kSeparator;
            if (0 != (accessMask & FILE_WRITE_DATA))
                outputString << L"FILE_WRITE_DATA" << kSeparator;
            if (0 != (accessMask & FILE_WRITE_ATTRIBUTES))
                outputString << L"FILE_WRITE_ATTRIBUTES" << kSeparator;
            if (0 != (accessMask & FILE_WRITE_EA))
                outputString << L"FILE_WRITE_EA" << kSeparator;
            if (0 != (accessMask & FILE_APPEND_DATA))
                outputString << L"FILE_APPEND_DATA" << kSeparator;
            if (0 != (accessMask & WRITE_DAC))
                outputString << L"WRITE_DAC" << kSeparator;
            if (0 != (accessMask & WRITE_OWNER))
                outputString << L"WRITE_OWNER" << kSeparator;
            if (0 != (accessMask & SYNCHRONIZE))
                outputString << L"SYNCHRONIZE" << kSeparator;
            if (0 != (accessMask & FILE_EXECUTE))
                outputString << L"FILE_EXECUTE" << kSeparator;
            if (0 != (accessMask & FILE_LIST_DIRECTORY))
                outputString << L"FILE_LIST_DIRECTORY" << kSeparator;
            if (0 != (accessMask & FILE_TRAVERSE))
                outputString << L"FILE_TRAVERSE" << kSeparator;

            accessMask &= (~(GENERIC_ALL | GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | DELETE | FILE_READ_DATA | FILE_READ_ATTRIBUTES | FILE_READ_EA | READ_CONTROL | FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA | FILE_APPEND_DATA | WRITE_DAC | WRITE_OWNER | SYNCHRONIZE | FILE_EXECUTE | FILE_LIST_DIRECTORY | FILE_TRAVERSE));
            if (0 != accessMask)
                outputString << Strings::FormatString(L"0x%08x", accessMask) << kSeparator;
        }

        outputString.RemoveSuffix(static_cast<unsigned int>(kSeparator.length()));
        outputString << L")";

        return outputString;
    }

    /// Generates a string representation of the specified creation disposition value. Useful for logging.
    /// @param [in] createDisposition Creation disposition options, typically received from an application when creating or opening a file.
    /// @return String representation of the creation disposition.
    static TemporaryString CreateDispositionToString(ULONG createDisposition)
    {
        constexpr wchar_t kFormatString[] = L"0x%08x (%s)";

        switch (createDisposition)
        {
        case FILE_SUPERSEDE:
            return Strings::FormatString(kFormatString, createDisposition, L"FILE_SUPERSEDE");
        case FILE_CREATE:
            return Strings::FormatString(kFormatString, createDisposition, L"FILE_CREATE");
        case FILE_OPEN:
            return Strings::FormatString(kFormatString, createDisposition, L"FILE_OPEN");
        case FILE_OPEN_IF:
            return Strings::FormatString(kFormatString, createDisposition, L"FILE_OPEN_IF");
        case FILE_OVERWRITE:
            return Strings::FormatString(kFormatString, createDisposition, L"FILE_OVERWRITE");
        case FILE_OVERWRITE_IF:
            return Strings::FormatString(kFormatString, createDisposition, L"FILE_OVERWRITE_IF");
        default:
            return Strings::FormatString(kFormatString, createDisposition, L"unknown");
        }
    }

    /// Generates a string representation of the specified create/open options flags. Useful for logging.
    /// @param [in] createOrOpenOptions Create or open options flags.
    /// @return String representation of the create or open options flags.
    static TemporaryString CreateOrOpenOptionsToString(ULONG createOrOpenOptions)
    {
        // These flags are missing from available headers.
#ifndef FILE_DISALLOW_EXCLUSIVE 
#define FILE_DISALLOW_EXCLUSIVE 0x00020000
#endif
#ifndef FILE_SESSION_AWARE
#define FILE_SESSION_AWARE 0x00040000
#endif
#ifndef FILE_CONTAINS_EXTENDED_CREATE_INFORMATION
#define FILE_CONTAINS_EXTENDED_CREATE_INFORMATION 0x10000000
#endif

        constexpr std::wstring_view kSeparator = L" | ";
        TemporaryString outputString = Strings::FormatString(L"0x%08x (", createOrOpenOptions);

        if (0 == createOrOpenOptions)
        {
            outputString << L"none" << kSeparator;
        }
        else
        {
            if (0 != (createOrOpenOptions & FILE_DIRECTORY_FILE))
                outputString << L"FILE_DIRECTORY_FILE" << kSeparator;
            if (0 != (createOrOpenOptions & FILE_WRITE_THROUGH))
                outputString << L"FILE_WRITE_THROUGH" << kSeparator;
            if (0 != (createOrOpenOptions & FILE_SEQUENTIAL_ONLY))
                outputString << L"FILE_SEQUENTIAL_ONLY" << kSeparator;
            if (0 != (createOrOpenOptions & FILE_NO_INTERMEDIATE_BUFFERING))
                outputString << L"FILE_NO_INTERMEDIATE_BUFFERING" << kSeparator;
            if (0 != (createOrOpenOptions & FILE_SYNCHRONOUS_IO_ALERT))
                outputString << L"FILE_SYNCHRONOUS_IO_ALERT" << kSeparator;
            if (0 != (createOrOpenOptions & FILE_SYNCHRONOUS_IO_NONALERT))
                outputString << L"FILE_SYNCHRONOUS_IO_NONALERT" << kSeparator;
            if (0 != (createOrOpenOptions & FILE_NON_DIRECTORY_FILE))
                outputString << L"FILE_NON_DIRECTORY_FILE" << kSeparator;
            if (0 != (createOrOpenOptions & FILE_CREATE_TREE_CONNECTION))
                outputString << L"FILE_CREATE_TREE_CONNECTION" << kSeparator;
            if (0 != (createOrOpenOptions & FILE_COMPLETE_IF_OPLOCKED))
                outputString << L"FILE_COMPLETE_IF_OPLOCKED" << kSeparator;
            if (0 != (createOrOpenOptions & FILE_NO_EA_KNOWLEDGE))
                outputString << L"FILE_NO_EA_KNOWLEDGE" << kSeparator;
            if (0 != (createOrOpenOptions & FILE_OPEN_REMOTE_INSTANCE))
                outputString << L"FILE_OPEN_REMOTE_INSTANCE" << kSeparator;
            if (0 != (createOrOpenOptions & FILE_RANDOM_ACCESS))
                outputString << L"FILE_RANDOM_ACCESS" << kSeparator;
            if (0 != (createOrOpenOptions & FILE_DELETE_ON_CLOSE))
                outputString << L"FILE_DELETE_ON_CLOSE" << kSeparator;
            if (0 != (createOrOpenOptions & FILE_OPEN_BY_FILE_ID))
                outputString << L"FILE_OPEN_BY_FILE_ID" << kSeparator;
            if (0 != (createOrOpenOptions & FILE_OPEN_FOR_BACKUP_INTENT))
                outputString << L"FILE_OPEN_FOR_BACKUP_INTENT" << kSeparator;
            if (0 != (createOrOpenOptions & FILE_NO_COMPRESSION))
                outputString << L"FILE_NO_COMPRESSION" << kSeparator;
            if (0 != (createOrOpenOptions & FILE_OPEN_REQUIRING_OPLOCK))
                outputString << L"FILE_OPEN_REQUIRING_OPLOCK" << kSeparator;
            if (0 != (createOrOpenOptions & FILE_DISALLOW_EXCLUSIVE))
                outputString << L"FILE_DISALLOW_EXCLUSIVE" << kSeparator;
            if (0 != (createOrOpenOptions & FILE_SESSION_AWARE))
                outputString << L"FILE_SESSION_AWARE" << kSeparator;
            if (0 != (createOrOpenOptions & FILE_RESERVE_OPFILTER))
                outputString << L"FILE_RESERVE_OPFILTER" << kSeparator;
            if (0 != (createOrOpenOptions & FILE_OPEN_REPARSE_POINT))
                outputString << L"FILE_OPEN_REPARSE_POINT" << kSeparator;
            if (0 != (createOrOpenOptions & FILE_OPEN_NO_RECALL))
                outputString << L"FILE_OPEN_NO_RECALL" << kSeparator;
            if (0 != (createOrOpenOptions & FILE_OPEN_FOR_FREE_SPACE_QUERY))
                outputString << L"FILE_OPEN_FOR_FREE_SPACE_QUERY" << kSeparator;
            if (0 != (createOrOpenOptions & FILE_CONTAINS_EXTENDED_CREATE_INFORMATION))
                outputString << L"FILE_CONTAINS_EXTENDED_CREATE_INFORMATION" << kSeparator;

            createOrOpenOptions &= (~(FILE_DIRECTORY_FILE | FILE_WRITE_THROUGH | FILE_SEQUENTIAL_ONLY | FILE_NO_INTERMEDIATE_BUFFERING | FILE_SYNCHRONOUS_IO_ALERT | FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE | FILE_CREATE_TREE_CONNECTION | FILE_COMPLETE_IF_OPLOCKED | FILE_NO_EA_KNOWLEDGE | FILE_OPEN_REMOTE_INSTANCE | FILE_RANDOM_ACCESS | FILE_DELETE_ON_CLOSE | FILE_OPEN_BY_FILE_ID | FILE_OPEN_FOR_BACKUP_INTENT | FILE_NO_COMPRESSION | FILE_OPEN_REQUIRING_OPLOCK | FILE_DISALLOW_EXCLUSIVE | FILE_SESSION_AWARE | FILE_RESERVE_OPFILTER | FILE_OPEN_REPARSE_POINT | FILE_OPEN_NO_RECALL | FILE_OPEN_FOR_FREE_SPACE_QUERY | FILE_CONTAINS_EXTENDED_CREATE_INFORMATION));
            if (0 != createOrOpenOptions)
                outputString << Strings::FormatString(L"0x%08x", createOrOpenOptions) << kSeparator;
        }

        outputString.RemoveSuffix(static_cast<unsigned int>(kSeparator.length()));
        outputString << L")";

        return outputString;
    }

    /// Generates a string representation of the specified share access flags. Useful for logging.
    /// @param [in] shareAccess Share access flags, typically received from an application when creating or opening a file.
    /// @return String representation of the share access flags.
    static TemporaryString ShareAccessToString(ULONG shareAccess)
    {
        constexpr std::wstring_view kSeparator = L" | ";
        TemporaryString outputString = Strings::FormatString(L"0x%08x (", shareAccess);

        if (0 == shareAccess)
        {
            outputString << L"none" << kSeparator;
        }
        else
        {
            if (0 != (shareAccess & FILE_SHARE_READ))
                outputString << L"FILE_SHARE_READ" << kSeparator;
            if (0 != (shareAccess & FILE_SHARE_WRITE))
                outputString << L"FILE_SHARE_WRITE" << kSeparator;
            if (0 != (shareAccess & FILE_SHARE_DELETE))
                outputString << L"FILE_SHARE_DELETE" << kSeparator;

            shareAccess &= (~(FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE));
            if (0 != shareAccess)
                outputString << Strings::FormatString(L"0x%08x", shareAccess) << kSeparator;
        }

        outputString.RemoveSuffix(static_cast<unsigned int>(kSeparator.length()));
        outputString << L")";

        return outputString;
    }

    /// Advances an in-progress directory enumeration operation by copying file information structures to an application-supplied buffer.
    /// Most parameters come directly from `NtQueryDirectoryFileEx` but those that do not are documented.
    /// @param [in] functionName Name of the API function whose hook function is invoking this function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of the named function. Used only for logging.
    /// @param [in] enumerationState Enumeration state data structure, which must be mutable so it can be updated as the enumeration proceeds.
    /// @param [in] isFirstInvocation Whether or not to enable special behavior for the first invocation of a directory enumeration function, as specified by `NtQueryDirectoryFileEx` documentation.
    /// @return Windows error code corresponding to the result of advancing the directory enumeration operation.
    static NTSTATUS AdvanceDirectoryEnumerationOperation(const wchar_t* functionName, unsigned int functionRequestIdentifier, OpenHandleStore::SInProgressDirectoryEnumeration& enumerationState, bool isFirstInvocation, PIO_STATUS_BLOCK ioStatusBlock, PVOID outputBuffer, ULONG outputBufferSizeBytes, ULONG queryFlags, std::wstring_view queryFilePattern)
    {
        DebugAssert(nullptr != enumerationState.queue, "Advancing directory enumeration state without an operation queue.");

        if (queryFlags & Pathwinder::QueryFlag::kRestartScan)
        {
            enumerationState.queue->Restart(queryFilePattern);
            enumerationState.enumeratedFilenames.clear();
            isFirstInvocation = true;
        }

        // The `Information` field of the output I/O status block records the total number of bytes written.
        ioStatusBlock->Information = 0;

        // This block will cause `STATUS_NO_MORE_FILES` to be returned if the queue is empty and enumeration is complete.
        // Getting past here means the queue is not empty and more files can be enumerated.
        NTSTATUS enumerationStatus = enumerationState.queue->EnumerationStatus();
        if (!(NT_SUCCESS(enumerationStatus)))
        {
            // If the first invocation has resulted in no files available for enumeration then that should be the result.
            // This would only happen if a query file pattern is specified and it matches no files. Otherwise, a directory enumeration would at very least include "." and ".." entries.
            if ((true == isFirstInvocation) && NtStatus::kNoMoreFiles == enumerationStatus)
                return NtStatus::kNoSuchFile;

            return enumerationStatus;
        }

        // Some extra checks are required if this is the first invocation. This is to handle partial writes when the buffer is too small to hold a single complete structure.
        // On subsequent calls these checks are omitted, per `NtQueryDirectoryFileEx` documentation.
        if (true == isFirstInvocation)
        {
            if (outputBufferSizeBytes < enumerationState.fileInformationStructLayout.BaseStructureSize())
                return NtStatus::kBufferTooSmall;

            if (outputBufferSizeBytes < enumerationState.queue->SizeOfFront())
            {
                ioStatusBlock->Information = static_cast<ULONG_PTR>(enumerationState.queue->CopyFront(outputBuffer, outputBufferSizeBytes));
                enumerationState.fileInformationStructLayout.ClearNextEntryOffset(outputBuffer);
                return NtStatus::kBufferOverflow;
            }
        }

        const unsigned int maxElementsToWrite = ((queryFlags & Pathwinder::QueryFlag::kReturnSingleEntry) ? 1 : std::numeric_limits<unsigned int>::max());
        unsigned int numElementsWritten = 0;
        unsigned int numBytesWritten = 0;
        void* lastBufferPosition = nullptr;

        // At this point only full structures will be written, and it is safe to assume there is at least one file information structure left in the queue.
        while ((NT_SUCCESS(enumerationStatus)) && (numElementsWritten < maxElementsToWrite))
        {
            void* const bufferPosition = reinterpret_cast<void*>(reinterpret_cast<size_t>(outputBuffer) + static_cast<size_t>(numBytesWritten));
            const unsigned int bufferCapacityLeftBytes = outputBufferSizeBytes - numBytesWritten;

            if (bufferCapacityLeftBytes < enumerationState.queue->SizeOfFront())
                break;

            // If this is the first invocation, or just freshly-restarted, then no enumerated filenames have already been seen.
            // Otherwise the queue will have been pre-advanced to the first unique filename.
            // For these reasons it is correct to copy first and advance the queue after.
            numBytesWritten += enumerationState.queue->CopyFront(bufferPosition, bufferCapacityLeftBytes);
            numElementsWritten += 1;

            // There are a few reasons why the next entry offset field might not be correct.
            // If the file information structure is received from the system, then the value might be 0 to indicate no more files from the system, but that might not be correct to communicate to the application. Sometimes the system also adds padding which can be removed.
            // For these reasons it is necessary to update the next entry offset here and track the last written file information structure so that its next entry offset can be cleared after the loop.
            enumerationState.fileInformationStructLayout.UpdateNextEntryOffset(bufferPosition);
            lastBufferPosition = bufferPosition;

            enumerationState.enumeratedFilenames.emplace(std::wstring(enumerationState.queue->FileNameOfFront()));
            enumerationState.queue->PopFront();

            // Enumeration status must be checked first because, if there are no file information structures left in the queue, checking the front element's filename will cause a crash.
            while((NT_SUCCESS(enumerationState.queue->EnumerationStatus())) && (enumerationState.enumeratedFilenames.contains(enumerationState.queue->FileNameOfFront())))
                enumerationState.queue->PopFront();

            enumerationStatus = enumerationState.queue->EnumerationStatus();
        }

        if (nullptr != lastBufferPosition)
            enumerationState.fileInformationStructLayout.ClearNextEntryOffset(lastBufferPosition);

        ioStatusBlock->Information = numBytesWritten;

        // Whether or not the queue still has any file information structures is not relevant.
        // Coming into this function call there was at least one such structure available.
        // Even if it was not actually copied to the application buffer, and hence 0 bytes were copied, this is still considered success per `NtQueryDirectoryFileEx` documentation.
        switch (enumerationStatus)
        {
        case NtStatus::kMoreEntries:
        case NtStatus::kNoMoreFiles:
            enumerationStatus = NtStatus::kSuccess;
            break;

        default:
            break;
        }

        return enumerationStatus;
    }

    /// Copies the contents of the supplied file rename information structure but replaces the filename.
    /// This has the effect of changing the new name of the specified file after the rename operation completes.
    /// @param [in] inputFileRenameInformation Original file rename information structurewhose contents are to be copied.
    /// @param [in] replacementFilename Replacement filename.
    /// @return Buffer containing a new file rename information structure with the filename replaced.
    static FileRenameInformationAndFilename CopyFileRenameInformationAndReplaceFilename(const SFileRenameInformation& inputFileRenameInformation, std::wstring_view replacementFilename)
    {
        TemporaryVector<uint8_t> newFileRenameInformation;

        SFileRenameInformation outputFileRenameInformation = inputFileRenameInformation;
        outputFileRenameInformation.fileNameLength = (static_cast<ULONG>(replacementFilename.length()) * sizeof(wchar_t));

        for (size_t i = 0; i < offsetof(SFileRenameInformation, fileName); ++i)
            newFileRenameInformation.PushBack((reinterpret_cast<const uint8_t*>(&outputFileRenameInformation))[i]);

        for (size_t i = 0; i < (replacementFilename.length() * sizeof(wchar_t)); ++i)
            newFileRenameInformation.PushBack((reinterpret_cast<const uint8_t*>(replacementFilename.data()))[i]);

        return FileRenameInformationAndFilename(std::move(newFileRenameInformation));
    }

    /// Executes any pre-operations needed ahead of invoking underlying system calls.
    /// @param [in] functionName Name of the API function whose hook function is invoking this function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of the named function. Used only for logging.
    /// @param [in] instruction Instruction that specifies how to redirect a filesystem operation, including identifying any pre-operations needed.
    /// @return Result of executing the pre-operations. The code will indicate success if they all succeed or a failure that corresponds to the first applicable pre-operation failure.
    static NTSTATUS ExecuteExtraPreOperations(const wchar_t* functionName, unsigned int functionRequestIdentifier, const FileOperationInstruction& instruction)
    {
        NTSTATUS extraPreOperationResult = NtStatus::kSuccess;

        if (instruction.GetExtraPreOperations().contains(static_cast<int>(FileOperationInstruction::EExtraPreOperation::EnsurePathHierarchyExists)) && (NT_SUCCESS(extraPreOperationResult)))
        {
            Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Ensuring directory hierarchy exists for \"%.*s\".", functionName, functionRequestIdentifier, static_cast<int>(instruction.GetExtraPreOperationOperand().length()), instruction.GetExtraPreOperationOperand().data());
            extraPreOperationResult = static_cast<NTSTATUS>(FilesystemOperations::CreateDirectoryHierarchy(instruction.GetExtraPreOperationOperand()));
        }

        if (!(NT_SUCCESS(extraPreOperationResult)))
            Message::OutputFormatted(Message::ESeverity::Error, L"%s(%u): A required pre-operation failed (NTSTATUS = 0x%08x).", functionName, functionRequestIdentifier, static_cast<unsigned int>(extraPreOperationResult));

        return extraPreOperationResult;
    }

    /// Converts a `CreateDisposition` parameter, which system calls use to identify filesystem behavior regarding creating new files or opening existing files, into an appropriate internal create disposition object.
    /// @param [in] ntCreateDisposition `CreateDisposition` parameter received from the application.
    /// @return Corresponding create disposition object.
    static CreateDisposition CreateDispositionFromNtParameter(ULONG ntCreateDisposition)
    {
        switch (ntCreateDisposition)
        {
        case FILE_CREATE:
            return CreateDisposition::CreateNewFile();

        case FILE_SUPERSEDE:
        case FILE_OPEN_IF:
        case FILE_OVERWRITE_IF:
            return CreateDisposition::CreateNewOrOpenExistingFile();

        case FILE_OPEN:
        case FILE_OVERWRITE:
            return CreateDisposition::OpenExistingFile();

        default:
            return CreateDisposition::OpenExistingFile();
        }
    }

    /// Converts a `DesiredAccess` parameter, which system calls use to identify the type of access requested to a file, into an appropriate internal file access mode object.
    /// @param [in] ntDesiredAccess `DesiredAccess` parameter received from the application.
    /// @return Corresponding file access mode object.
    static FileAccessMode FileAccessModeFromNtParameter(ACCESS_MASK ntDesiredAccess)
    {
        constexpr ACCESS_MASK kReadAccessMask = (GENERIC_READ | FILE_READ_DATA | FILE_READ_ATTRIBUTES | FILE_READ_EA | READ_CONTROL | FILE_EXECUTE | FILE_LIST_DIRECTORY | FILE_TRAVERSE);
        constexpr ACCESS_MASK kWriteAccessMask = (GENERIC_WRITE | FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA | FILE_APPEND_DATA | WRITE_DAC | WRITE_OWNER | FILE_DELETE_CHILD);
        constexpr ACCESS_MASK kDeleteAccessMask = (DELETE);

        const bool canRead = (0 != (ntDesiredAccess & kReadAccessMask));
        const bool canWrite = (0 != (ntDesiredAccess & kWriteAccessMask));
        const bool canDelete = (0 != (ntDesiredAccess & kDeleteAccessMask));

        return FileAccessMode(canRead, canWrite, canDelete);
    }

    /// Fills the supplied object name and attributes structure with the name and attributes needed to represent the redirected filename from a file operation redirection instruction.
    /// Does nothing if the file operation redirection instruction does not specify any redirection.
    /// This must be done in place because the `OBJECT_ATTRIBUTES` structure refers to its `ObjectName` field by pointer. Returning by value would invalidate the address of the `ObjectName` field and therefore not work.
    /// @param [out] redirectedObjectNameAndAttributes Mutable reference to the structure to be filled.
    /// @param [in] instruction Instruction that specifies how to redirect a filesystem operation.
    /// @param [in] unredirectedObjectAttributes Object attributes structure received from the application.
    static void FillRedirectedObjectNameAndAttributesForInstruction(SObjectNameAndAttributes& redirectedObjectNameAndAttributes, const FileOperationInstruction& instruction, const OBJECT_ATTRIBUTES& unredirectedObjectAttributes)
    {
        if (true == instruction.HasRedirectedFilename())
        {
            redirectedObjectNameAndAttributes.objectName = Strings::NtConvertStringViewToUnicodeString(instruction.GetRedirectedFilename());

            redirectedObjectNameAndAttributes.objectAttributes = unredirectedObjectAttributes;
            redirectedObjectNameAndAttributes.objectAttributes.RootDirectory = nullptr;
            redirectedObjectNameAndAttributes.objectAttributes.ObjectName = &redirectedObjectNameAndAttributes.objectName;
        }
    }

    /// Retrieves an identifier for a particular invocation of a hook function.
    /// Used exclusively for logging.
    /// @return Numeric identifier for an invocation.
    static inline unsigned int GetRequestIdentifier(void)
    {
        static std::atomic<unsigned int> nextRequestIdentifier;
        return nextRequestIdentifier++;
    }

    /// Determines how to redirect an individual file operation in which the affected file is identified by an object attributes structure.
    /// @param [in] functionName Name of the API function whose hook function is invoking this function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of the named function. Used only for logging.
    /// @param [in] rootDirectory Open handle for the root directory that contains the input filename. May be `nullptr`, in which case the input filename must be a full and absolute path. Supplied by an application that invokes a system call.
    /// @param [in] inputFilename Filename received from the application that invoked the system call. Must be a full and absolute path if the root directory handle is not provided.
    /// @param [in] fileAccessMode Type of access or accesses to be performed on the file.
    /// @param [in] createDisposition Create disposition for the requsted file operation, which specifies whether a new file should be created, an existing file opened, or either.
    /// @return Context that contains all of the information needed to submit the file operation to the underlying system call.
    static SFileOperationContext GetFileOperationRedirectionInformation(const wchar_t* functionName, unsigned int functionRequestIdentifier, HANDLE rootDirectory, std::wstring_view inputFilename, FileAccessMode fileAccessMode, CreateDisposition createDisposition)
    {
        std::optional<TemporaryString> maybeRedirectedFilename = std::nullopt;
        std::optional<OpenHandleStore::SHandleDataView> maybeRootDirectoryHandleData = ((nullptr == rootDirectory) ? std::nullopt : OpenHandleStore::Singleton().GetDataForHandle(rootDirectory));

        if (true == maybeRootDirectoryHandleData.has_value())
        {
            // Input object attributes structure specifies an open directory handle as the root directory and the handle was found in the cache.
            // Before querying for redirection it is necessary to assemble the full filename, including the root directory path.

            std::wstring_view rootDirectoryHandlePath = maybeRootDirectoryHandleData->associatedPath;

            TemporaryString inputFullFilename;
            inputFullFilename << rootDirectoryHandlePath << L'\\' << inputFilename;

            FileOperationInstruction redirectionInstruction = Pathwinder::FilesystemDirector::Singleton().GetInstructionForFileOperation(inputFullFilename, fileAccessMode, createDisposition);
            if (true == redirectionInstruction.HasRedirectedFilename())
                Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Invoked with root directory path \"%.*s\" (via handle %zu) and relative path \"%.*s\" which were combined and redirected to \"%.*s\".", functionName, functionRequestIdentifier, static_cast<int>(rootDirectoryHandlePath.length()), rootDirectoryHandlePath.data(), reinterpret_cast<size_t>(rootDirectory), static_cast<int>(inputFilename.length()), inputFilename.data(), static_cast<int>(redirectionInstruction.GetRedirectedFilename().length()), redirectionInstruction.GetRedirectedFilename().data());
            else
                Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): Invoked with root directory path \"%.*s\" (via handle %zu) and relative path \"%.*s\" which were combined but not redirected.", functionName, functionRequestIdentifier, static_cast<int>(rootDirectoryHandlePath.length()), rootDirectoryHandlePath.data(), reinterpret_cast<size_t>(rootDirectory), static_cast<int>(inputFilename.length()), inputFilename.data());

            return {.instruction = std::move(redirectionInstruction), .composedInputPath = std::move(inputFullFilename)};
        }
        else if (nullptr == rootDirectory)
        {
            // Input object attributes structure does not specify an open directory handle as the root directory.
            // It is sufficient to send the object name directly for redirection.

            FileOperationInstruction redirectionInstruction = Pathwinder::FilesystemDirector::Singleton().GetInstructionForFileOperation(inputFilename, fileAccessMode, createDisposition);

            if (true == redirectionInstruction.HasRedirectedFilename())
                Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Invoked with path \"%.*s\" which was redirected to \"%.*s\".", functionName, functionRequestIdentifier, static_cast<int>(inputFilename.length()), inputFilename.data(), static_cast<int>(redirectionInstruction.GetRedirectedFilename().length()), redirectionInstruction.GetRedirectedFilename().data());
            else
                Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): Invoked with path \"%.*s\" which was not redirected.", functionName, functionRequestIdentifier, static_cast<int>(inputFilename.length()), inputFilename.data());

            return {.instruction = std::move(redirectionInstruction), .composedInputPath = std::nullopt};
        }
        else
        {
            // Input object attributes structure specifies an open directory handle as the root directory but the handle is not in cache.
            // When the root directory handle was originally opened it was determined that there is no possible match with a filesystem rule.
            // Therefore, it is not necessary to attempt redirection.

            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): Invoked with root directory handle %zu and relative path \"%.*s\" for which no redirection was attempted.", functionName, functionRequestIdentifier, reinterpret_cast<size_t>(rootDirectory), static_cast<int>(inputFilename.length()), inputFilename.data());
            return {.instruction = FileOperationInstruction::NoRedirectionOrInterception(), .composedInputPath = std::nullopt};
        }
    }

    /// Determines the input/output mode for the specified file handle.
    /// @param [in] handle Filesystem object handle to check.
    /// @return Input/output mode for the handle, or #EInputOutputMode::Unknown in the event of an error.
    static EInputOutputMode GetInputOutputModeForHandle(HANDLE handle)
    {
        SFileModeInformation modeInformation;
        IO_STATUS_BLOCK unusedStatusBlock{};

        if (!(NT_SUCCESS(Hooks::ProtectedDependency::NtQueryInformationFile::SafeInvoke(handle, &unusedStatusBlock, &modeInformation, sizeof(modeInformation), SFileModeInformation::kFileInformationClass))))
            return EInputOutputMode::Unknown;

        switch (modeInformation.mode & (FILE_SYNCHRONOUS_IO_ALERT | FILE_SYNCHRONOUS_IO_NONALERT))
        {
        case 0:
            return EInputOutputMode::Asynchronous;
        default:
            return EInputOutputMode::Synchronous;
        }
    }

    /// Places pointers to the data structures that identify files to try, in order, to submit to the underlying system call for a file operation.
    /// The number of pointers placed, and the order in which they are placed, is controlled by the file operation redirection instruction.
    /// Any entries placed as `nullptr` are invalid and should be skipped.
    /// @tparam FileObjectType Data structure type that identifies files to try.
    /// @param [in] functionName Name of the API function whose hook function is invoking this function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of the named function. Used only for logging.
    /// @param [in] instruction Instruction that specifies how to redirect a filesystem operation.
    /// @param [in] unredirectedFileObject Pointer to the data structure received from the application.
    /// @param [in] redirectedFileObject Pointer to the data structure generated by querying for file operation redirection.
    /// @return Object attributes to be tried, in order.
    template <typename FileObjectType> std::array<FileObjectType*, 2> SelectFilesToTry(const wchar_t* functionName, unsigned int functionRequestIdentifier, const FileOperationInstruction& instruction, FileObjectType* unredirectedFileObject, FileObjectType* redirectedFileObject)
    {
        switch (instruction.GetFilenamesToTry())
        {
        case FileOperationInstruction::ETryFiles::UnredirectedOnly:
            return {unredirectedFileObject, nullptr};

        case FileOperationInstruction::ETryFiles::UnredirectedFirst:
            return {unredirectedFileObject, redirectedFileObject};

        case FileOperationInstruction::ETryFiles::RedirectedFirst:
            return {redirectedFileObject, unredirectedFileObject};

        case FileOperationInstruction::ETryFiles::RedirectedOnly:
            return {redirectedFileObject};

        default:
            Message::OutputFormatted(Message::ESeverity::Error, L"%s(%u): Internal error: unrecognized file operation instruction (FileOperationInstruction::ETryFiles = %u).", functionName, functionRequestIdentifier, static_cast<unsigned int>(instruction.GetFilenamesToTry()));
            return {nullptr, nullptr};
        }
    }

    /// Determines if the next possible filename should be tried or if the existing system call result should be returned to the application.
    /// @param [in] systemCallResult Result of the system call for the present attempt.
    /// @return `true` if the result indicates that the next filename should be tried, `false` if the result indicates to stop trying and move on.
    static inline bool ShouldTryNextFilename(NTSTATUS systemCallResult)
    {
        // If the error code is related to a file not being found then it is safe to try the next file.
        // All other codes, including I/O errors, permission issues, or even success, should be passed to the application.
        switch (systemCallResult)
        {
        case NtStatus::kObjectNameInvalid:
        case NtStatus::kObjectNameNotFound:
        case NtStatus::kObjectPathInvalid:
        case NtStatus::kObjectPathNotFound:
            return true;

        default:
            return false;
        }
    }

    /// Inserts a newly-opened handle into the open handle store, selecting an associated path based on the file operation redirection instruction.
    /// @param [in] functionName Name of the API function whose hook function is invoking this function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of the named function. Used only for logging.
    /// @param [in] newlyOpenedHandle Handle to add to the open handles store.
    /// @param [in] instruction Instruction that specifies how to redirect a filesystem operation.
    /// @param [in] successfulPath Path that was used successfully to create the file handle.
    /// @param [in] unredirectedPath Original file name supplied by the application.
    static void SelectFilenameAndStoreNewlyOpenedHandle(const wchar_t* functionName, unsigned int functionRequestIdentifier, HANDLE newlyOpenedHandle, const FileOperationInstruction& instruction, std::wstring_view successfulPath, std::wstring_view unredirectedPath)
    {
        std::wstring_view selectedPath;

        switch (instruction.GetFilenameHandleAssociation())
        {
        case FileOperationInstruction::EAssociateNameWithHandle::None:
            break;

        case FileOperationInstruction::EAssociateNameWithHandle::WhicheverWasSuccessful:
            selectedPath = successfulPath;
            break;

        case FileOperationInstruction::EAssociateNameWithHandle::Unredirected:
            selectedPath = unredirectedPath;
            break;

        case FileOperationInstruction::EAssociateNameWithHandle::Redirected:
            selectedPath = instruction.GetRedirectedFilename();
            break;

        default:
            Message::OutputFormatted(Message::ESeverity::Error, L"%s(%u): Internal error: unrecognized file operation instruction (FileOperationInstruction::EAssociateNameWithHandle = %u).", functionName, functionRequestIdentifier, static_cast<unsigned int>(instruction.GetFilenameHandleAssociation()));
            break;
        }

        if (false == selectedPath.empty())
        {
            successfulPath = Strings::RemoveTrailing(successfulPath, L'\\');
            selectedPath = Strings::RemoveTrailing(selectedPath, L'\\');

            OpenHandleStore::Singleton().InsertHandle(newlyOpenedHandle, std::wstring(selectedPath), std::wstring(successfulPath));
            Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Handle %zu was opened for path \"%.*s\" and stored in association with path \"%.*s\".", functionName, functionRequestIdentifier, reinterpret_cast<size_t>(newlyOpenedHandle), static_cast<int>(successfulPath.length()), successfulPath.data(), static_cast<int>(selectedPath.length()), selectedPath.data());
        }
    }

    /// Updates a handle that might already be in the open handle store, selecting an associated path based on the file operation redirection instruction.
    /// @param [in] functionName Name of the API function whose hook function is invoking this function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of the named function. Used only for logging.
    /// @param [in] handleToUpdate Handle to update in the open handles store, if it is present.
    /// @param [in] instruction Instruction that specifies how to redirect a filesystem operation.
    /// @param [in] successfulPath Path that was used successfully to create the file handle.
    /// @param [in] unredirectedPath Original file name supplied by the application.
    static void SelectFilenameAndUpdateOpenHandle(const wchar_t* functionName, unsigned int functionRequestIdentifier, HANDLE handleToUpdate, const FileOperationInstruction& instruction, std::wstring_view successfulPath, std::wstring_view unredirectedPath)
    {
        std::wstring_view selectedPath;

        switch (instruction.GetFilenameHandleAssociation())
        {
        case FileOperationInstruction::EAssociateNameWithHandle::None:
            do {
                OpenHandleStore::SHandleData erasedHandleData;
                if (true == OpenHandleStore::Singleton().RemoveHandle(handleToUpdate, &erasedHandleData))
                    Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Handle %zu associated with path \"%s\" was erased from storage.", functionName, functionRequestIdentifier, reinterpret_cast<size_t>(handleToUpdate), erasedHandleData.associatedPath.c_str());
            } while (false);
            break;

        case FileOperationInstruction::EAssociateNameWithHandle::WhicheverWasSuccessful:
            selectedPath = successfulPath;
            break;

        case FileOperationInstruction::EAssociateNameWithHandle::Unredirected:
            selectedPath = unredirectedPath;
            break;

        case FileOperationInstruction::EAssociateNameWithHandle::Redirected:
            selectedPath = instruction.GetRedirectedFilename();
            break;

        default:
            Message::OutputFormatted(Message::ESeverity::Error, L"%s(%u): Internal error: unrecognized file operation instruction (FileOperationInstruction::EAssociateNameWithHandle = %u).", functionName, functionRequestIdentifier, static_cast<unsigned int>(instruction.GetFilenameHandleAssociation()));
            break;
        }

        if (false == selectedPath.empty())
        {
            successfulPath = Strings::RemoveTrailing(successfulPath, L'\\');
            selectedPath = Strings::RemoveTrailing(selectedPath, L'\\');

            OpenHandleStore::Singleton().InsertOrUpdateHandle(handleToUpdate, std::wstring(selectedPath), std::wstring(successfulPath));
            Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Handle %zu was updated in storage to be opened with path \"%.*s\" and associated with path \"%.*s\".", functionName, functionRequestIdentifier, reinterpret_cast<size_t>(handleToUpdate), static_cast<int>(successfulPath.length()), successfulPath.data(), static_cast<int>(selectedPath.length()), selectedPath.data());
        }
    }

    /// Common internal implementation of hook functions that create or open files, resulting in the creation of a new file handle.
    /// Parameters correspond to the `NtCreateFile` system call, with the exception of `functionName` which is just the hook function name for logging purposes.
    /// @return Result to be returned to the application on system call completion, or nothing at all if the request should be forwarded to the application.
    static std::optional<NTSTATUS> HookFunctionCommonImplementationCreateOrOpenFile(const wchar_t* functionName, PHANDLE fileHandle, ACCESS_MASK desiredAccess, POBJECT_ATTRIBUTES objectAttributes, PIO_STATUS_BLOCK ioStatusBlock, PLARGE_INTEGER allocationSize, ULONG fileAttributes, ULONG shareAccess, ULONG createDisposition, ULONG createOptions, PVOID eaBuffer, ULONG eaLength)
    {
        const unsigned int requestIdentifier = GetRequestIdentifier();

        // There is overhead involved with producing a dump of parameter values.
        // This is why it is helpful to guard the block on whether or not the output would actually be logged.
        if (true == Message::WillOutputMessageOfSeverity(Message::ESeverity::SuperDebug))
        {
            const std::wstring_view functionNameView = std::wstring_view(functionName);
            const std::wstring_view objectNameParam = Strings::NtConvertUnicodeStringToStringView(*objectAttributes->ObjectName);

            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): Invoked with these parameters:", functionName, requestIdentifier);
            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u):   ObjectName = \"%.*s\"", functionName, requestIdentifier, static_cast<int>(objectNameParam.length()), objectNameParam.data());
            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u):   RootDirectory = %zu", functionName, requestIdentifier, reinterpret_cast<size_t>(objectAttributes->RootDirectory));
            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u):   DesiredAccess = %s", functionName, requestIdentifier, AccessMaskToString(desiredAccess).AsCString());
            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u):   ShareAccess = %s", functionName, requestIdentifier, ShareAccessToString(shareAccess).AsCString());

            if (functionNameView.contains(L"Create"))
            {
                Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u):   CreateDisposition = %s", functionName, requestIdentifier, CreateDispositionToString(createDisposition).AsCString());
                Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u):   CreateOptions = %s", functionName, requestIdentifier, CreateOrOpenOptionsToString(createOptions).AsCString());
            }
            else if (functionNameView.contains(L"Open"))
            {
                Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u):   OpenOptions = %s", functionName, requestIdentifier, CreateOrOpenOptionsToString(createOptions).AsCString());
            }
        }

        const SFileOperationContext operationContext = GetFileOperationRedirectionInformation(functionName, requestIdentifier, objectAttributes->RootDirectory, Strings::NtConvertUnicodeStringToStringView(*(objectAttributes->ObjectName)), FileAccessModeFromNtParameter(desiredAccess), CreateDispositionFromNtParameter(createDisposition));
        const FileOperationInstruction& redirectionInstruction = operationContext.instruction;

        if (true == Globals::GetConfigurationData().isDryRunMode)
            return std::nullopt;

        NTSTATUS preOperationResult = ExecuteExtraPreOperations(functionName, requestIdentifier, operationContext.instruction);
        if (!(NT_SUCCESS(preOperationResult)))
            return preOperationResult;

        SObjectNameAndAttributes redirectedObjectNameAndAttributes = {};
        FillRedirectedObjectNameAndAttributesForInstruction(redirectedObjectNameAndAttributes, operationContext.instruction, *objectAttributes);

        HANDLE newlyOpenedHandle = nullptr;
        NTSTATUS systemCallResult = NtStatus::kSuccess;

        std::wstring_view unredirectedPath = ((true == operationContext.composedInputPath.has_value()) ? operationContext.composedInputPath->AsStringView() : Strings::NtConvertUnicodeStringToStringView(*objectAttributes->ObjectName));
        std::wstring_view lastAttemptedPath;

        for (POBJECT_ATTRIBUTES objectAttributesToTry : SelectFilesToTry(functionName, requestIdentifier, redirectionInstruction, objectAttributes, &redirectedObjectNameAndAttributes.objectAttributes))
        {
            if (nullptr == objectAttributesToTry)
                continue;

            lastAttemptedPath = Strings::NtConvertUnicodeStringToStringView(*(objectAttributesToTry->ObjectName));
            systemCallResult = Hooks::ProtectedDependency::NtCreateFile::SafeInvoke(&newlyOpenedHandle, desiredAccess, objectAttributesToTry, ioStatusBlock, allocationSize, fileAttributes, shareAccess, createDisposition, createOptions, eaBuffer, eaLength);
            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x, ObjectName = \"%.*s\".", functionName, requestIdentifier, systemCallResult, static_cast<int>(lastAttemptedPath.length()), lastAttemptedPath.data());

            if (false == ShouldTryNextFilename(systemCallResult))
                break;
        }

        if (true == lastAttemptedPath.empty())
            return std::nullopt;

        if (NT_SUCCESS(systemCallResult))
            SelectFilenameAndStoreNewlyOpenedHandle(functionName, requestIdentifier, newlyOpenedHandle, redirectionInstruction, lastAttemptedPath, unredirectedPath);

        *fileHandle = newlyOpenedHandle;
        return systemCallResult;
    }

    /// Common internal implementation of hook functions that perform directory enumeration.
    /// Parameters correspond to the `NtQueryDirectoryFileEx` system call, with the exception of `functionName` which is just the hook function name for logging purposes.
    /// @return Result to be returned to the application on system call completion, or nothing at all if the request should be forwarded to the application.
    static std::optional<NTSTATUS> HookFunctionCommonImplementationQueryDirectoryForEnumeration(const wchar_t* functionName, HANDLE fileHandle, HANDLE event, PIO_APC_ROUTINE apcRoutine, PVOID apcContext, PIO_STATUS_BLOCK ioStatusBlock, PVOID fileInformation, ULONG length, FILE_INFORMATION_CLASS fileInformationClass, ULONG queryFlags, PUNICODE_STRING fileName)
    {
        std::optional<FileInformationStructLayout> maybeFileInformationStructLayout = FileInformationStructLayout::LayoutForFileInformationClass(fileInformationClass);
        if (false == maybeFileInformationStructLayout.has_value())
            return std::nullopt;

        std::optional<OpenHandleStore::SHandleDataView> maybeHandleData = OpenHandleStore::Singleton().GetDataForHandle(fileHandle);
        if (false == maybeHandleData.has_value())
            return std::nullopt;

        const unsigned int requestIdentifier = GetRequestIdentifier();

        switch (GetInputOutputModeForHandle(fileHandle))
        {
        case EInputOutputMode::Synchronous:
            break;

        case EInputOutputMode::Asynchronous:
            Message::OutputFormatted(Message::ESeverity::Warning, L"%s(%u): Application requested asynchronous directory enumeration with handle %zu, which is unimplemented.", functionName, requestIdentifier, reinterpret_cast<size_t>(fileHandle));
            return std::nullopt;

        default:
            Message::OutputFormatted(Message::ESeverity::Error, L"%s(%u): Failed to determine I/O mode during directory enumeration for handle %zu.", functionName, requestIdentifier, reinterpret_cast<size_t>(fileHandle));
            return std::nullopt;
        }

        std::wstring_view queryFilePattern = ((nullptr == fileName) ? std::wstring_view() : Strings::NtConvertUnicodeStringToStringView(*fileName));
        if (true == queryFilePattern.empty())
            Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Invoked with handle %zu, which is associated with path \"%.*s\" and opened for path \"%.*s\".", functionName, requestIdentifier, reinterpret_cast<size_t>(fileHandle), static_cast<int>(maybeHandleData->associatedPath.length()), maybeHandleData->associatedPath.data(), static_cast<int>(maybeHandleData->realOpenedPath.length()), maybeHandleData->realOpenedPath.data());
        else
            Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Invoked with file pattern \"%.*s\" and handle %zu, which is associated with path \"%.*s\" and opened for path \"%.*s\".", functionName, requestIdentifier, static_cast<int>(queryFilePattern.length()), queryFilePattern.data(), reinterpret_cast<size_t>(fileHandle), static_cast<int>(maybeHandleData->associatedPath.length()), maybeHandleData->associatedPath.data(), static_cast<int>(maybeHandleData->realOpenedPath.length()), maybeHandleData->realOpenedPath.data());

        // The underlying system calls are expected to behave slightly differently on a first invocation versus subsequent invocations.
        bool newDirectoryEnumerationCreated = false;

        if (false == maybeHandleData->directoryEnumeration.has_value())
        {
            // A new directory enumeration queue needs to be created because a directory enumeration is being requested for the first time.

            DirectoryEnumerationInstruction directoryEnumerationInstruction = FilesystemDirector::Singleton().GetInstructionForDirectoryEnumeration(maybeHandleData->associatedPath, maybeHandleData->realOpenedPath);

            if (true == Globals::GetConfigurationData().isDryRunMode)
            {
                // This will mark the directory enumeration object as present but no-op.
                // Future invocations will therefore not attempt to query for a directory enumeration instruction and will just be forwarded to the system.
                OpenHandleStore::Singleton().AssociateDirectoryEnumerationState(fileHandle, nullptr, *maybeFileInformationStructLayout);
                return std::nullopt;
            }

            std::unique_ptr<IDirectoryOperationQueue> directoryOperationQueueUniquePtr = CreateDirectoryOperationQueueForInstruction(directoryEnumerationInstruction, fileInformationClass, queryFilePattern, maybeHandleData->associatedPath, maybeHandleData->realOpenedPath);
            OpenHandleStore::Singleton().AssociateDirectoryEnumerationState(fileHandle, std::move(directoryOperationQueueUniquePtr), *maybeFileInformationStructLayout);
            newDirectoryEnumerationCreated = true;
        }

        maybeHandleData = OpenHandleStore::Singleton().GetDataForHandle(fileHandle);
        DebugAssert((true == maybeHandleData->directoryEnumeration.has_value()), "Failed to locate an in-progress directory enumearation stat data structure which should already exist.");

        // At this point a directory enumeration queue will be present.
        // If it is `nullptr` then it is a no-op and the original request just needs to be forwarded to the system.
        OpenHandleStore::SInProgressDirectoryEnumeration& directoryEnumerationState = *(*(maybeHandleData->directoryEnumeration));
        if (nullptr == directoryEnumerationState.queue)
            return std::nullopt;

        return AdvanceDirectoryEnumerationOperation(functionName, requestIdentifier, directoryEnumerationState, newDirectoryEnumerationCreated, ioStatusBlock, fileInformation, length, queryFlags, queryFilePattern);
    }
}


// -------- PROTECTED HOOK FUNCTIONS --------------------------------------- //
// See original function and Hookshot documentation for details.

NTSTATUS Pathwinder::Hooks::DynamicHook_NtClose::Hook(HANDLE Handle)
{
    using namespace Pathwinder;


    std::optional<OpenHandleStore::SHandleDataView> maybeClosedHandleData = OpenHandleStore::Singleton().GetDataForHandle(Handle);
    if (false == maybeClosedHandleData.has_value())
        return Original(Handle);

    const unsigned int requestIdentifier = GetRequestIdentifier();

    OpenHandleStore::SHandleData closedHandleData;
    NTSTATUS closeHandleResult = OpenHandleStore::Singleton().RemoveAndCloseHandle(Handle, &closedHandleData);

    if (NT_SUCCESS(closeHandleResult))
        Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Handle %zu for path \"%s\" was closed and erased from storage.", GetFunctionName(), requestIdentifier, reinterpret_cast<size_t>(Handle), closedHandleData.associatedPath.c_str());

    return closeHandleResult;
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtCreateFile::Hook(PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock, PLARGE_INTEGER AllocationSize, ULONG FileAttributes, ULONG ShareAccess, ULONG CreateDisposition, ULONG CreateOptions, PVOID EaBuffer, ULONG EaLength)
{
    using namespace Pathwinder;


    auto maybeHookFunctionResult = HookFunctionCommonImplementationCreateOrOpenFile(GetFunctionName(), FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
    if (false == maybeHookFunctionResult.has_value())
        return Original(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);

    return *maybeHookFunctionResult;
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtDeleteFile::Hook(POBJECT_ATTRIBUTES ObjectAttributes)
{
    using namespace Pathwinder;


    // TODO
    return Original(ObjectAttributes);
}
// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtOpenFile::Hook(PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock, ULONG ShareAccess, ULONG OpenOptions)
{
    using namespace Pathwinder;


    auto maybeHookFunctionResult = HookFunctionCommonImplementationCreateOrOpenFile(GetFunctionName(), FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, nullptr, 0, ShareAccess, FILE_OPEN, OpenOptions, nullptr, 0);
    if (false == maybeHookFunctionResult.has_value())
        return Original(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions);

    return *maybeHookFunctionResult;
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryDirectoryFile::Hook(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length, FILE_INFORMATION_CLASS FileInformationClass, BOOLEAN ReturnSingleEntry, PUNICODE_STRING FileName, BOOLEAN RestartScan)
{
    using namespace Pathwinder;


    ULONG queryFlags = 0;
    if (RestartScan != 0) queryFlags |= QueryFlag::kRestartScan;
    if (ReturnSingleEntry != 0) queryFlags |= QueryFlag::kReturnSingleEntry;

    auto maybeHookFunctionResult = HookFunctionCommonImplementationQueryDirectoryForEnumeration(GetFunctionName(), FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, FileInformation, Length, FileInformationClass, queryFlags, FileName);
    if (false == maybeHookFunctionResult.has_value())
        return Original(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, FileInformation, Length, FileInformationClass, ReturnSingleEntry, FileName, RestartScan);

    return *maybeHookFunctionResult;
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryDirectoryFileEx::Hook(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length, FILE_INFORMATION_CLASS FileInformationClass, ULONG QueryFlags, PUNICODE_STRING FileName)
{
    using namespace Pathwinder;


    auto maybeHookFunctionResult = HookFunctionCommonImplementationQueryDirectoryForEnumeration(GetFunctionName(), FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, FileInformation, Length, FileInformationClass, QueryFlags, FileName);
    if (false == maybeHookFunctionResult.has_value())
        return Original(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, FileInformation, Length, FileInformationClass, QueryFlags, FileName);

    return *maybeHookFunctionResult;
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryInformationFile::Hook(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length, FILE_INFORMATION_CLASS FileInformationClass)
{
    using namespace Pathwinder;


    NTSTATUS systemCallResult = Original(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);
    switch (systemCallResult)
    {
    case NtStatus::kBufferOverflow:
        // Buffer overflows are allowed because the filename part will be overwritten and a true overflow condition detected at that time.
        break;

    default:
        if (!(NT_SUCCESS(systemCallResult)))
            return systemCallResult;
        break;
    }

    SFileNameInformation* fileNameInformation = nullptr;
    switch (FileInformationClass)
    {
    case SFileNameInformation::kFileInformationClass:
        fileNameInformation = reinterpret_cast<SFileNameInformation*>(FileInformation);
        break;

    case SFileAllInformation::kFileInformationClass:
        fileNameInformation = &(reinterpret_cast<SFileAllInformation*>(FileInformation)->nameInformation);
        break;

    default:
        return systemCallResult;
    }

    // If the buffer is not big enough to hold any part of the filename then it is not necessary to try replacing it.
    const size_t fileNameInformationBufferOffset = reinterpret_cast<size_t>(fileNameInformation) - reinterpret_cast<size_t>(FileInformation);
    if ((fileNameInformationBufferOffset + offsetof(SFileNameInformation, fileName)) >= static_cast<size_t>(Length))
        return systemCallResult;

    std::wstring_view systemReturnedFileName = GetFileInformationStructFilename(*fileNameInformation);
    if (false == systemReturnedFileName.starts_with(L'\\'))
        return systemCallResult;

    std::optional<OpenHandleStore::SHandleDataView> maybeHandleData = OpenHandleStore::Singleton().GetDataForHandle(FileHandle);
    if (false == maybeHandleData.has_value())
        return systemCallResult;

    std::wstring_view replacementFileName = maybeHandleData->associatedPath;

    // Paths in the open handle store are expected to have a Windows name prefix and begin with a drive letter.
    // According to the documentation for `NtQueryInformationFile` absolute paths begin with a backslash and skip over the drive letter component.
    // So if the actual path is "C:\Dir\file.txt" the path returned by the system call is "\Dir\file.txt" and hence Pathwinder needs to do the same transformation.
    replacementFileName.remove_prefix(Strings::PathGetWindowsNamespacePrefix(replacementFileName).length());
    replacementFileName.remove_prefix(replacementFileName.find_first_of(L'\\'));
    if (replacementFileName == systemReturnedFileName)
        return systemCallResult;


    const unsigned int requestIdentifier = GetRequestIdentifier();

    Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Invoked with handle %zu, the system returned path \"%.*s\", and it is being replaced with path \"%.*s\".", GetFunctionName(), requestIdentifier, reinterpret_cast<size_t>(FileHandle), static_cast<int>(systemReturnedFileName.length()), systemReturnedFileName.data(), static_cast<int>(replacementFileName.length()), replacementFileName.data());

    const size_t numReplacementCharsWritten = SetFileInformationStructFilename(*fileNameInformation, (static_cast<size_t>(Length) - fileNameInformationBufferOffset), replacementFileName);
    if (numReplacementCharsWritten < replacementFileName.length())
        return NtStatus::kBufferOverflow;

    // If the original system call resulted in a buffer overflow, but the buffer was large enough to hold the replacement filename, then the application should be told that the operation succeeded.
    // Any other return code should be passed back to the application without modification.
    return ((NtStatus::kBufferOverflow == systemCallResult) ? NtStatus::kSuccess : systemCallResult);
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryInformationByName::Hook(POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length, FILE_INFORMATION_CLASS FileInformationClass)
{
    using namespace Pathwinder;


    const unsigned int requestIdentifier = GetRequestIdentifier();

    const SFileOperationContext operationContext = GetFileOperationRedirectionInformation(GetFunctionName(), requestIdentifier, ObjectAttributes->RootDirectory, Strings::NtConvertUnicodeStringToStringView(*(ObjectAttributes->ObjectName)), FileAccessMode::ReadOnly(), CreateDisposition::OpenExistingFile());
    const FileOperationInstruction& redirectionInstruction = operationContext.instruction;

    if (true == Globals::GetConfigurationData().isDryRunMode)
        return Original(ObjectAttributes, IoStatusBlock, FileInformation, Length, FileInformationClass);

    NTSTATUS preOperationResult = ExecuteExtraPreOperations(GetFunctionName(), requestIdentifier, operationContext.instruction);
    if (!(NT_SUCCESS(preOperationResult)))
        return preOperationResult;

    SObjectNameAndAttributes redirectedObjectNameAndAttributes = {};
    FillRedirectedObjectNameAndAttributesForInstruction(redirectedObjectNameAndAttributes, operationContext.instruction, *ObjectAttributes);

    std::wstring_view lastAttemptedPath;

    NTSTATUS systemCallResult = NtStatus::kSuccess;

    for (POBJECT_ATTRIBUTES objectAttributesToTry : SelectFilesToTry(GetFunctionName(), requestIdentifier, redirectionInstruction, ObjectAttributes, &redirectedObjectNameAndAttributes.objectAttributes))
    {
        if (nullptr == objectAttributesToTry)
            continue;

        lastAttemptedPath = Strings::NtConvertUnicodeStringToStringView(*(objectAttributesToTry->ObjectName));
        systemCallResult = Original(objectAttributesToTry, IoStatusBlock, FileInformation, Length, FileInformationClass);
        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x, ObjectName = \"%.*s\".", GetFunctionName(), requestIdentifier, systemCallResult, static_cast<int>(lastAttemptedPath.length()), lastAttemptedPath.data());

        if (false == ShouldTryNextFilename(systemCallResult))
            break;
    }

    if (true == lastAttemptedPath.empty())
        return Original(ObjectAttributes, IoStatusBlock, FileInformation, Length, FileInformationClass);

    return systemCallResult;
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtSetInformationFile::Hook(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length, FILE_INFORMATION_CLASS FileInformationClass)
{
    using namespace Pathwinder;


    // This invocation is only interesting if it is a rename operation. Otherwise there is no change being made to the input file handle, which is already open.
    if (SFileRenameInformation::kFileInformationClass != FileInformationClass)
        return Original(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);

    const unsigned int requestIdentifier = GetRequestIdentifier();

    SFileRenameInformation& unredirectedFileRenameInformation = *(reinterpret_cast<SFileRenameInformation*>(FileInformation));
    std::wstring_view unredirectedPath = GetFileInformationStructFilename(unredirectedFileRenameInformation);

    const SFileOperationContext operationContext = GetFileOperationRedirectionInformation(GetFunctionName(), requestIdentifier, unredirectedFileRenameInformation.rootDirectory, unredirectedPath, FileAccessMode::Delete(), CreateDisposition::CreateNewFile());
    const FileOperationInstruction& redirectionInstruction = operationContext.instruction;

    if (true == Globals::GetConfigurationData().isDryRunMode)
        return Original(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);

    NTSTATUS preOperationResult = ExecuteExtraPreOperations(GetFunctionName(), requestIdentifier, operationContext.instruction);
    if (!(NT_SUCCESS(preOperationResult)))
        return preOperationResult;

    // Due to how the file rename information structure is laid out, including an embedded filename buffer of variable size, there is overhead to generating a new one.
    // Without a redirected filename present it is better to bail early than to generate a new one unconditionally.
    if (false == redirectionInstruction.HasRedirectedFilename())
        return Original(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);

    FileRenameInformationAndFilename redirectedFileRenameInformationAndFilename = CopyFileRenameInformationAndReplaceFilename(unredirectedFileRenameInformation, redirectionInstruction.GetRedirectedFilename());
    SFileRenameInformation& redirectedFileRenameInformation = redirectedFileRenameInformationAndFilename.GetFileRenameInformation();

    NTSTATUS systemCallResult = NtStatus::kSuccess;
    std::wstring_view lastAttemptedPath;

    for (SFileRenameInformation* renameInformationToTry : SelectFilesToTry(GetFunctionName(), requestIdentifier, redirectionInstruction, &unredirectedFileRenameInformation, &redirectedFileRenameInformation))
    {
        if (nullptr == renameInformationToTry)
            continue;

        lastAttemptedPath = GetFileInformationStructFilename(*renameInformationToTry);
        systemCallResult = Original(FileHandle, IoStatusBlock, reinterpret_cast<PVOID>(renameInformationToTry), Length, FileInformationClass);
        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x, ObjectName = \"%.*s\".", GetFunctionName(), requestIdentifier, systemCallResult, static_cast<int>(lastAttemptedPath.length()), lastAttemptedPath.data());

        if (false == ShouldTryNextFilename(systemCallResult))
            break;
    }

    if (true == lastAttemptedPath.empty())
        return Original(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);

    if (NT_SUCCESS(systemCallResult))
        SelectFilenameAndUpdateOpenHandle(GetFunctionName(), requestIdentifier, FileHandle, redirectionInstruction, lastAttemptedPath, unredirectedPath);

    return systemCallResult;
}


// -------- UNPROTECTED HOOK FUNCTIONS ------------------------------------- //
// See original function and Hookshot documentation for details.

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryAttributesFile::Hook(POBJECT_ATTRIBUTES ObjectAttributes, PFILE_BASIC_INFO FileInformation)
{
    using namespace Pathwinder;


    const unsigned int requestIdentifier = GetRequestIdentifier();

    const SFileOperationContext operationContext = GetFileOperationRedirectionInformation(GetFunctionName(), requestIdentifier, ObjectAttributes->RootDirectory, Strings::NtConvertUnicodeStringToStringView(*(ObjectAttributes->ObjectName)), FileAccessMode::ReadOnly(), CreateDisposition::OpenExistingFile());
    const FileOperationInstruction& redirectionInstruction = operationContext.instruction;

    if (true == Globals::GetConfigurationData().isDryRunMode)
        return Original(ObjectAttributes, FileInformation);

    NTSTATUS preOperationResult = ExecuteExtraPreOperations(GetFunctionName(), requestIdentifier, operationContext.instruction);
    if (!(NT_SUCCESS(preOperationResult)))
        return preOperationResult;

    SObjectNameAndAttributes redirectedObjectNameAndAttributes = {};
    FillRedirectedObjectNameAndAttributesForInstruction(redirectedObjectNameAndAttributes, operationContext.instruction, *ObjectAttributes);

    std::wstring_view lastAttemptedPath;

    NTSTATUS systemCallResult = NtStatus::kSuccess;

    for (POBJECT_ATTRIBUTES objectAttributesToTry : SelectFilesToTry(GetFunctionName(), requestIdentifier, redirectionInstruction, ObjectAttributes, &redirectedObjectNameAndAttributes.objectAttributes))
    {
        if (nullptr == objectAttributesToTry)
            continue;

        lastAttemptedPath = Strings::NtConvertUnicodeStringToStringView(*(objectAttributesToTry->ObjectName));
        systemCallResult = Original(objectAttributesToTry, FileInformation);
        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x, ObjectName = \"%.*s\".", GetFunctionName(), requestIdentifier, systemCallResult, static_cast<int>(lastAttemptedPath.length()), lastAttemptedPath.data());

        if (false == ShouldTryNextFilename(systemCallResult))
            break;
    }

    if (true == lastAttemptedPath.empty())
        return Original(ObjectAttributes, FileInformation);

    return systemCallResult;
}
