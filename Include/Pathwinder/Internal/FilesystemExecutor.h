/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 ***********************************************************************************************//**
 * @file FilesystemExecutor.h
 *   Declaration of types and functions used to execute filesystem operations under control of
 *   filesystem instructions.
 **************************************************************************************************/

#pragma once

#include <cstdint>
#include <functional>

#include "ApiWindowsInternal.h"
#include "ArrayList.h"
#include "FilesystemDirector.h"
#include "OpenHandleStore.h"
#include "Strings.h"
#include "TemporaryBuffer.h"
#include "ValueOrError.h"

namespace Pathwinder
{
  namespace FilesystemExecutor
  {
    /// Enumerates the possible modes for I/O using a file handle.
    enum class EInputOutputMode : uint8_t
    {
      /// I/O mode is not known. This represents an error case.
      Unknown,

      /// I/O is asynchronous. System calls will return immediately, and completion information is
      /// provided out-of-band.
      Asynchronous,

      /// I/O is synchronous. System calls will return only after the requested operation
      /// completes.
      Synchronous,

      /// Not used as a value. Identifies the number of enumerators present in this enumeration.
      Count
    };

    /// Holds file rename information, including the variably-sized filename, in a bytewise buffer
    /// and exposes it in a type-safe way. This object is used for representation only, not for any
    /// functionality surrounding actually creating a byte-wise buffer representation of a file
    /// rename information structure.
    class FileRenameInformationAndFilename
    {
    private:

      /// Byte-wise buffer, which will hold the file rename information data structure.
      TemporaryVector<uint8_t> bytewiseBuffer;

    public:

      inline FileRenameInformationAndFilename(TemporaryVector<uint8_t>&& bytewiseBuffer)
          : bytewiseBuffer(std::move(bytewiseBuffer))
      {}

      /// Convenience method for retrieving a properly-typed file rename information structure.
      inline SFileRenameInformation& GetFileRenameInformation(void)
      {
        return *(reinterpret_cast<SFileRenameInformation*>(bytewiseBuffer.Data()));
      }

      /// Convenience method for retrieving the size, in bytes, of the stored file rename
      /// information structure including filename.
      inline unsigned int GetFileRenameInformationSizeBytes(void)
      {
        return bytewiseBuffer.Size();
      }
    };

    /// Contains all of the information associated with a file operation.
    struct SFileOperationContext
    {
      /// How the redirection should be performed.
      FileOperationInstruction instruction;

      /// If an input path was composed, for example due to combination with a root directory,
      /// then that input path is stored here.
      std::optional<TemporaryString> composedInputPath;
    };

    /// Holds all of the information needed to represent a create disposition that should be
    /// attempted.
    struct SCreateDispositionToTry
    {
      /// Enumerates possible conditions on whether or not the create disposition should be
      /// attempted.
      enum class ECondition : ULONG
      {
        /// Unconditionally attempt the file operation using the supplied create disposition.
        Unconditional,

        /// Attempt the file operation using the supplied create disposition only if the file
        /// exists.
        FileMustExist,

        /// Attempt the file operation using the supplied create disposition only if the file
        /// does not exist.
        FileMustNotExist
      };

      /// Condition on whether the supplied create disposition should be attempted or skipped.
      ECondition condition : 8;

      /// Create disposition parameter to provide to the underlying system call.
      ULONG ntParamCreateDisposition : 24;
    };

    /// Contains all of the information needed to represent a file name and attributes in the format
    /// needed to interact with underlying system calls.
    struct SObjectNameAndAttributes
    {
      /// Name of the object, as a Unicode string in the format supported by system calls.
      UNICODE_STRING objectName;

      /// Attributes of the object, which includes a field that points to the name.
      OBJECT_ATTRIBUTES objectAttributes;
    };

    /// Holds multiple create dispositions that should be tried in order when attempting file
    /// operations. Each element is either a create disposition that should be attempted or a forced
    /// result code, in which case the file operation should not be attempted but rather assumed to
    /// have the forced result.
    using TCreateDispositionsList = ArrayList<ValueOrError<SCreateDispositionToTry, NTSTATUS>, 2>;

    /// Holds multiple file operations to attempt in a small list, ordered by priority.
    /// Each element is either a single file operation that should be submitted to the system or a
    /// forced result code, in which case submitting to the system is skipped and assumed to have
    /// the forced result.
    /// @tparam FileObjectType Data structure type that identifies files to try.
    template <typename FileObjectType> using TFileOperationsList =
        ArrayList<ValueOrError<FileObjectType*, NTSTATUS>, 2>;

    /// Common internal entry point for intercepting attempts to close an existing file handle.
    /// @param [in] functionName Name of the API function whose hook function is invoking this
    /// function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of
    /// the named function. Used only for logging.
    /// @param [in] handle Handle that the application has requested to close.
    /// @param [in] underlyingSystemCallInvoker Invokable function object that performs the actual
    /// operation, with the only variable parameter being the handle to close. Any and all other
    /// information is expected to be captured within the object itself.
    /// @return Result of the system call, which should be returned to the application.
    NTSTATUS EntryPointCloseHandle(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        HANDLE handle,
        std::function<NTSTATUS(HANDLE)> underlyingSystemCallInvoker);

    /// Common internal entry point for intercepting directory enumerations. Parameters correspond
    /// to the `NtQueryDirectoryFileEx` system call, with the exception of `functionName` and
    /// `functionRequestIdentifier` which are the hook function name and request identifier for
    /// logging purposes.
    /// @return Result to be returned to the application on system call completion, or nothing at
    /// all if the request should be forwarded unmodified to the system.
    std::optional<NTSTATUS> EntryPointDirectoryEnumeration(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        HANDLE fileHandle,
        HANDLE event,
        PIO_APC_ROUTINE apcRoutine,
        PVOID apcContext,
        PIO_STATUS_BLOCK ioStatusBlock,
        PVOID fileInformation,
        ULONG length,
        FILE_INFORMATION_CLASS fileInformationClass,
        ULONG queryFlags,
        PUNICODE_STRING fileName);

    /// Common internal entry point for intercepting attempts to create or open files, resulting in
    /// the creation of a new file handle.
    /// @param [in] functionName Name of the API function whose hook function is invoking this
    /// function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of
    /// the named function. Used only for logging.
    /// @param [in] fileHandle Address that will receive the newly-created file handle, if this
    /// function is successful.
    /// @param [in] desiredAccess Desired file access types requested by the application.
    /// @param [in] objectAttributes Object attributes that identify the filesystem entity for which
    /// a new handle should be created. As received from the application.
    /// @param [in] shareAccess Sharing mask received from the application.
    /// @param [in] createDisposition Create disposition received from the application. Identifies
    /// whether a new file should be created, existing file should be opened, and so on.
    /// @param [in] createOptions File creation or opening options received from the application.
    /// @param [in] underlyingSystemCallInvoker Invokable function object that performs the actual
    /// operation, with the variable parameters being destination file handle address, object
    /// attributes of the file to attempt, and a create disposition.
    /// @return Result of the system call, which should be returned to the application.
    NTSTATUS EntryPointNewFileHandle(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        PHANDLE fileHandle,
        ACCESS_MASK desiredAccess,
        POBJECT_ATTRIBUTES objectAttributes,
        ULONG shareAccess,
        ULONG createDisposition,
        ULONG createOptions,
        std::function<NTSTATUS(PHANDLE, POBJECT_ATTRIBUTES, ULONG)> underlyingSystemCallInvoker);

    /// Common internal entry point for intercepting queries for file information such that the
    /// input is a name identified in an `OBJECT_ATTRIBUTES` structure but the operation does not
    /// result in a new file handle being created.
    /// @param [in] functionName Name of the API function whose hook function is invoking this
    /// function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of
    /// the named function. Used only for logging.
    /// @param [in] fileAccessMode Type of accesses that the underlying system call is expected to
    /// perform on the file.
    /// @param [in] objectAttributes Object attributes received as input from the application.
    /// @param [in] underlyingSystemCallInvoker Invokable function object that performs the actual
    /// operation, with the only variable parameter being object attributes. Any and all other
    /// information is expected to be captured within the object itself, including other
    /// application-specified parameters.
    /// @return Result of the system call, which should be returned to the application.
    NTSTATUS EntryPointQueryByObjectAttributes(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        FileAccessMode fileAccessMode,
        POBJECT_ATTRIBUTES objectAttributes,
        std::function<NTSTATUS(POBJECT_ATTRIBUTES)> underlyingSystemCallInvoker);

    /// Generates a string representation of the specified access mask. Useful for logging.
    /// @param [in] accessMask Access mask, typically received from an application when creating or
    /// opening a file.
    /// @return String representation of the access mask.
    TemporaryString NtAccessMaskToString(ACCESS_MASK accessMask);

    /// Generates a string representation of the specified create disposition value. Useful for
    /// logging.
    /// @param [in] createDisposition Creation disposition options, typically received from an
    /// application when creating or opening a file.
    /// @return String representation of the create disposition.
    TemporaryString NtCreateDispositionToString(ULONG createDisposition);

    /// Generates a string representation of the specified create/open options flags. Useful for
    /// logging.
    /// @param [in] createOrOpenOptions Create or open options flags.
    /// @return String representation of the create or open options flags.
    TemporaryString NtCreateOrOpenOptionsToString(ULONG createOrOpenOptions);

    /// Generates a string representation of the specified share access flags. Useful for logging.
    /// @param [in] shareAccess Share access flags, typically received from an application when
    /// creating or opening a file.
    /// @return String representation of the share access flags.
    TemporaryString NtShareAccessToString(ULONG shareAccess);

    /// Advances an in-progress directory enumeration operation by copying file information
    /// structures to an application-supplied buffer. Most parameters come directly from
    /// `NtQueryDirectoryFileEx` but those that do not are documented.
    /// @param [in] functionName Name of the API function whose hook function is invoking this
    /// function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of
    /// the named function. Used only for logging.
    /// @param [in] enumerationState Enumeration state data structure, which must be mutable so it
    /// can be updated as the enumeration proceeds.
    /// @param [in] isFirstInvocation Whether or not to enable special behavior for the first
    /// invocation of a directory enumeration function, as specified by `NtQueryDirectoryFileEx`
    /// documentation.
    /// @return Windows error code corresponding to the result of advancing the directory
    /// enumeration operation.
    NTSTATUS AdvanceDirectoryEnumerationOperation(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        OpenHandleStore::SInProgressDirectoryEnumeration& enumerationState,
        bool isFirstInvocation,
        PIO_STATUS_BLOCK ioStatusBlock,
        PVOID outputBuffer,
        ULONG outputBufferSizeBytes,
        ULONG queryFlags,
        std::wstring_view queryFilePattern);

    /// Copies the contents of the supplied file rename information structure but replaces the
    /// filename. This has the effect of changing the new name of the specified file after the
    /// rename operation completes.
    /// @param [in] inputFileRenameInformation Original file rename information structurewhose
    /// contents are to be copied.
    /// @param [in] replacementFilename Replacement filename.
    /// @return Buffer containing a new file rename information structure with the filename
    /// replaced.
    FileRenameInformationAndFilename CopyFileRenameInformationAndReplaceFilename(
        const SFileRenameInformation& inputFileRenameInformation,
        std::wstring_view replacementFilename);

    /// Converts a `CreateDisposition` parameter, which system calls use to identify filesystem
    /// behavior regarding creating new files or opening existing files, into an appropriate
    /// internal create disposition object.
    /// @param [in] ntCreateDisposition `CreateDisposition` parameter received from the application.
    /// @return Corresponding create disposition object.
    CreateDisposition CreateDispositionFromNtParameter(ULONG ntCreateDisposition);

    /// Executes any pre-operations needed ahead of invoking underlying system calls.
    /// @param [in] functionName Name of the API function whose hook function is invoking this
    /// function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of
    /// the named function. Used only for logging.
    /// @param [in] instruction Instruction that specifies how to redirect a filesystem operation,
    /// including identifying any pre-operations needed.
    /// @return Result of executing the pre-operations. The code will indicate success if they all
    /// succeed or a failure that corresponds to the first applicable pre-operation failure.
    NTSTATUS ExecuteExtraPreOperations(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        const FileOperationInstruction& instruction);

    /// Converts a `DesiredAccess` parameter, which system calls use to identify the type of access
    /// requested to a file, into an appropriate internal file access mode object.
    /// @param [in] ntDesiredAccess `DesiredAccess` parameter received from the application.
    /// @return Corresponding file access mode object.
    FileAccessMode FileAccessModeFromNtParameter(ACCESS_MASK ntDesiredAccess);

    /// Fills the supplied object name and attributes structure with the name and attributes needed
    /// to represent the redirected filename from a file operation redirection instruction. Does
    /// nothing if the file operation redirection instruction does not specify any redirection. This
    /// must be done in place because the `OBJECT_ATTRIBUTES` structure refers to its `ObjectName`
    /// field by pointer. Returning by value would invalidate the address of the `ObjectName` field
    /// and therefore not work.
    /// @param [out] redirectedObjectNameAndAttributes Mutable reference to the structure to be
    /// filled.
    /// @param [in] instruction Instruction that specifies how to redirect a filesystem operation.
    /// @param [in] unredirectedObjectAttributes Object attributes structure received from the
    /// application.
    void FillRedirectedObjectNameAndAttributesForInstruction(
        SObjectNameAndAttributes& redirectedObjectNameAndAttributes,
        const FileOperationInstruction& instruction,
        const OBJECT_ATTRIBUTES& unredirectedObjectAttributes);

    /// Determines how to redirect an individual file operation in which the affected file is
    /// identified by an object attributes structure.
    /// @param [in] functionName Name of the API function whose hook function is invoking this
    /// function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of
    /// the named function. Used only for logging.
    /// @param [in] rootDirectory Open handle for the root directory that contains the input
    /// filename. May be `nullptr`, in which case the input filename must be a full and absolute
    /// path. Supplied by an application that invokes a system call.
    /// @param [in] inputFilename Filename received from the application that invoked the system
    /// call. Must be a full and absolute path if the root directory handle is not provided.
    /// @param [in] fileAccessMode Type of access or accesses to be performed on the file.
    /// @param [in] createDisposition Create disposition for the requsted file operation, which
    /// specifies whether a new file should be created, an existing file opened, or either.
    /// @return Context that contains all of the information needed to submit the file operation to
    /// the underlying system call.
    SFileOperationContext GetFileOperationRedirectionInformation(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        HANDLE rootDirectory,
        std::wstring_view inputFilename,
        FileAccessMode fileAccessMode,
        CreateDisposition createDisposition);

    /// Retrieves the path associated with the specified file handle, if it exists.
    /// @param [in] handle Filesystem object handle for which an associated path is desired.
    /// @return Associated path, if it exists.
    std::optional<std::wstring_view> GetHandleAssociatedPath(HANDLE handle);

    /// Determines the input/output mode for the specified file handle.
    /// @param [in] handle Filesystem object handle to check.
    /// @return Input/output mode for the handle, or #EInputOutputMode::Unknown in the event of an
    /// error.
    EInputOutputMode GetInputOutputModeForHandle(HANDLE handle);

    /// Identifies the create dispositions to try, in order, when determining which file operations
    /// to submit to the underlying system call. Uses the preference indicated in the file operation
    /// instruction, along with the supplied create disposition parameter, to determine which create
    /// dispositions need to be attempted.
    /// @param [in] functionName Name of the API function whose hook function is invoking this
    /// function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of
    /// the named function. Used only for logging.
    /// @param [in] instruction Instruction that specifies how to redirect a filesystem operation.
    /// @param [in] ntParamCreateDisposition Creation disposition options received from the
    /// application.
    /// @return List of create dispositions to be tried, in order.
    TCreateDispositionsList SelectCreateDispositionsToTry(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        const FileOperationInstruction& instruction,
        ULONG ntParamCreateDisposition);

    /// Identifies the file operations to try, in order, to submit to the underlying system call for
    /// a file operation. The number of file operations placed, and the order in which they are
    /// placed, is controlled by the file operation redirection instruction. Any entries placed with
    /// file object `nullptr` are invalid and should be skipped. Likewise, any entries that are
    /// error codes should use that error code as the forced result of the attempt, instead of
    /// submitting the operation to the system.
    /// @tparam FileObjectType Data structure type that identifies files to try.
    /// @param [in] functionName Name of the API function whose hook function is invoking this
    /// function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of
    /// the named function. Used only for logging.
    /// @param [in] instruction Instruction that specifies how to redirect a filesystem operation.
    /// @param [in] unredirectedFileObject Pointer to the data structure received from the
    /// application.
    /// @param [in] redirectedFileObject Pointer to the data structure generated by querying for
    /// file operation redirection.
    /// @return List of data structures that identify the files to be tried, in order.
    template <typename FileObjectType> TFileOperationsList<FileObjectType>
        SelectFileOperationsToTry(
            const wchar_t* functionName,
            unsigned int functionRequestIdentifier,
            const FileOperationInstruction& instruction,
            FileObjectType& unredirectedFileObject,
            FileObjectType& redirectedFileObject);

    /// Determines if the next possible filename should be tried or if the existing system call
    /// result should be returned to the application.
    /// @param [in] systemCallResult Result of the system call for the present attempt.
    /// @return `true` if the result indicates that the next filename should be tried, `false` if
    /// the result indicates to stop trying and move on.
    bool ShouldTryNextFilename(NTSTATUS systemCallResult);

    /// Inserts a newly-opened handle into the open handle store, selecting an associated path based
    /// on the file operation redirection instruction.
    /// @param [in] functionName Name of the API function whose hook function is invoking this
    /// function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of
    /// the named function. Used only for logging.
    /// @param [in] newlyOpenedHandle Handle to add to the open handles store.
    /// @param [in] instruction Instruction that specifies how to redirect a filesystem operation.
    /// @param [in] successfulPath Path that was used successfully to create the file handle.
    /// @param [in] unredirectedPath Original file name supplied by the application.
    void SelectFilenameAndStoreNewlyOpenedHandle(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        HANDLE newlyOpenedHandle,
        const FileOperationInstruction& instruction,
        std::wstring_view successfulPath,
        std::wstring_view unredirectedPath);

    /// Updates a handle that might already be in the open handle store, selecting an associated
    /// path based on the file operation redirection instruction.
    /// @param [in] functionName Name of the API function whose hook function is invoking this
    /// function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of
    /// the named function. Used only for logging.
    /// @param [in] handleToUpdate Handle to update in the open handles store, if it is present.
    /// @param [in] instruction Instruction that specifies how to redirect a filesystem operation.
    /// @param [in] successfulPath Path that was used successfully to create the file handle.
    /// @param [in] unredirectedPath Original file name supplied by the application.
    void SelectFilenameAndUpdateOpenHandle(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        HANDLE handleToUpdate,
        const FileOperationInstruction& instruction,
        std::wstring_view successfulPath,
        std::wstring_view unredirectedPath);
  } // namespace FilesystemExecutor
} // namespace Pathwinder
