/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file FileInformationStruct.cpp
 *   Unit tests for all functionality related to manipulating file
 *   information structures used by Windows system calls.
 *****************************************************************************/

#include "ApiWindowsInternal.h"
#include "Configuration.h"
#include "FileInformationStruct.h"
#include "TestCase.h"

#include <cstring>
#include <string_view>


namespace PathwinderTest
{
    using namespace ::Pathwinder;


    // -------- INTERNAL FUNCTIONS ----------------------------------------- //

    /// Converts a structure size, in bytes, to a wide-character index for the last wide character in a trailing `fileName` field.
    /// Assumes that all structure sizes are evenly divisible by the size of a wide character.
    /// Works only when the trailing `fileName` field is actually large enough to extend the size of the structure beyond its base size.
    /// To read the wide character at the returned index, treat the entire structure as an array of wide characters and then access the array at that index.
    /// @param [in] structSizeInBytes Structure size in bytes, including the trailing `fileName` field.
    /// @return Index of the last wide character in a trailing `fileName` field, relative to the start address of the structure.
    static constexpr unsigned int StructSizeToLastWideCharacterIndex(unsigned int structSizeInBytes)
    {
        return (structSizeInBytes / sizeof(wchar_t)) - 1;
    }

    /// Reads and returns the last wide character in a trailing `fileName` field from a file information struct whose size in bytes is known.
    /// Assumes that all structure sizes are evenly divisible by the size of a wide character.
    /// Works only when the trailing `fileName` field is actually large enough to extend the size of the structure beyond its base size.
    /// @param [in] fileInformationStruct Start address of the file information structure.
    /// @param [in] structSizeInBytes Structure size in bytes, including the trailing `fileName` field.
    /// @return Last wide character in the trailing `fileName` field of the file information structure.
    static inline wchar_t LastWideCharacterInFileName(const void* fileInformationStruct, unsigned int structSizeInBytes)
    {
        return (reinterpret_cast<const wchar_t*>(fileInformationStruct))[StructSizeToLastWideCharacterIndex(structSizeInBytes)];
    }

    /// Allocates and initializes the contents of a file information structure buffer object for use in test cases.
    /// Initialization simply involves writing 0 to the entire buffer.
    /// @return Newly-initialized file information structure buffer.
    static inline FileInformationStructBuffer InitializeFileInformationStructBuffer(void)
    {
        FileInformationStructBuffer buffer;
        std::memset(buffer.Data(), 0, static_cast<size_t>(buffer.Size()));
        return buffer;
    }


    // -------- TEST CASES ------------------------------------------------- //

    // Verifies that all the supported information classes produce valid layout objects.
    TEST_CASE(FileInformationStructLayout_LayoutForFileInformationClass)
    {
        constexpr FILE_INFORMATION_CLASS kTestInputs[] = {
            SFileDirectoryInformation::kFileInformationClass,
            SFileFullDirectoryInformation::kFileInformationClass,
            SFileBothDirectoryInformation::kFileInformationClass,
            SFileNamesInformation::kFileInformationClass,
            SFileIdBothDirectoryInformation::kFileInformationClass,
            SFileIdFullDirectoryInformation::kFileInformationClass,
            SFileIdGlobalTxDirectoryInformation::kFileInformationClass,
            SFileIdExtdDirectoryInformation::kFileInformationClass,
            SFileIdExtdBothDirectoryInformation::kFileInformationClass,
        };

        for (const auto& testInput : kTestInputs)
        {
            TEST_ASSERT(true == FileInformationStructLayout::LayoutForFileInformationClass(testInput).has_value());
        }
    }

    // Verifies that base structure sizes are reported correctly.
    TEST_CASE(FileInformationStructLayout_BaseStructureSize)
    {
        constexpr std::pair<FILE_INFORMATION_CLASS, size_t> kTestInputsAndExpectedOutputs[] = {
            {SFileDirectoryInformation::kFileInformationClass,              sizeof(SFileDirectoryInformation)},
            {SFileFullDirectoryInformation::kFileInformationClass,          sizeof(SFileFullDirectoryInformation)},
            {SFileBothDirectoryInformation::kFileInformationClass,          sizeof(SFileBothDirectoryInformation)},
            {SFileNamesInformation::kFileInformationClass,                  sizeof(SFileNamesInformation)},
            {SFileIdBothDirectoryInformation::kFileInformationClass,        sizeof(SFileIdBothDirectoryInformation)},
            {SFileIdFullDirectoryInformation::kFileInformationClass,        sizeof(SFileIdFullDirectoryInformation)},
            {SFileIdGlobalTxDirectoryInformation::kFileInformationClass,    sizeof(SFileIdGlobalTxDirectoryInformation)},
            {SFileIdExtdDirectoryInformation::kFileInformationClass,        sizeof(SFileIdExtdDirectoryInformation)},
            {SFileIdExtdBothDirectoryInformation::kFileInformationClass,    sizeof(SFileIdExtdBothDirectoryInformation)},
        };

        for (const auto& testRecord : kTestInputsAndExpectedOutputs)
        {
            const FILE_INFORMATION_CLASS testInput = testRecord.first;
            const unsigned int expectedOutput = static_cast<unsigned int>(testRecord.second);

            const auto maybeLayout = FileInformationStructLayout::LayoutForFileInformationClass(testInput);
            TEST_ASSERT(true == maybeLayout.has_value());

            const unsigned int actualOutput = maybeLayout->BaseStructureSize();
            TEST_ASSERT(actualOutput == expectedOutput);
        }
    }

    // Verifies that file name pointers are reported correctly.
    TEST_CASE(FileInformationStructLayout_FileNamePointer)
    {
        constexpr std::pair<FILE_INFORMATION_CLASS, size_t> kTestInputsAndExpectedOutputs[] = {
            {SFileDirectoryInformation::kFileInformationClass,              offsetof(SFileDirectoryInformation, fileName)},
            {SFileFullDirectoryInformation::kFileInformationClass,          offsetof(SFileFullDirectoryInformation, fileName)},
            {SFileBothDirectoryInformation::kFileInformationClass,          offsetof(SFileBothDirectoryInformation, fileName)},
            {SFileNamesInformation::kFileInformationClass,                  offsetof(SFileNamesInformation, fileName)},
            {SFileIdBothDirectoryInformation::kFileInformationClass,        offsetof(SFileIdBothDirectoryInformation, fileName)},
            {SFileIdFullDirectoryInformation::kFileInformationClass,        offsetof(SFileIdFullDirectoryInformation, fileName)},
            {SFileIdGlobalTxDirectoryInformation::kFileInformationClass,    offsetof(SFileIdGlobalTxDirectoryInformation, fileName)},
            {SFileIdExtdDirectoryInformation::kFileInformationClass,        offsetof(SFileIdExtdDirectoryInformation, fileName)},
            {SFileIdExtdBothDirectoryInformation::kFileInformationClass,    offsetof(SFileIdExtdBothDirectoryInformation, fileName)},
        };

        for (const auto& testRecord : kTestInputsAndExpectedOutputs)
        {
            const FILE_INFORMATION_CLASS testInput = testRecord.first;
            const unsigned int expectedOutput = static_cast<unsigned int>(testRecord.second);

            const auto maybeLayout = FileInformationStructLayout::LayoutForFileInformationClass(testInput);
            TEST_ASSERT(true == maybeLayout.has_value());

            const size_t actualOutput = reinterpret_cast<size_t>(maybeLayout->FileNamePointer(nullptr));
            TEST_ASSERT(actualOutput == expectedOutput);
        }
    }

    // Verifies that the next entry offset field is correctly read.
    template <typename FileInformationStructType> static void TestCaseBodyReadNextEntryOffset(void)
    {
        constexpr FILE_INFORMATION_CLASS testFileInformationClass = FileInformationStructType::kFileInformationClass;
        constexpr ULONG testValue = 0xccddeeff;

        FileInformationStructType testStruct{};
        testStruct.nextEntryOffset = testValue;

        TEST_ASSERT(testValue == FileInformationStructLayout::LayoutForFileInformationClass(testFileInformationClass)->ReadNextEntryOffset(&testStruct));
    }
    TEST_CASE(FileInformationStructLayout_ReadNextEntryOffset)
    {
        TestCaseBodyReadNextEntryOffset<SFileDirectoryInformation>();
        TestCaseBodyReadNextEntryOffset<SFileFullDirectoryInformation>();
        TestCaseBodyReadNextEntryOffset<SFileBothDirectoryInformation>();
        TestCaseBodyReadNextEntryOffset<SFileNamesInformation>();
        TestCaseBodyReadNextEntryOffset<SFileIdBothDirectoryInformation>();
        TestCaseBodyReadNextEntryOffset<SFileIdFullDirectoryInformation>();
        TestCaseBodyReadNextEntryOffset<SFileIdGlobalTxDirectoryInformation>();
        TestCaseBodyReadNextEntryOffset<SFileIdExtdDirectoryInformation>();
        TestCaseBodyReadNextEntryOffset<SFileIdExtdBothDirectoryInformation>();
    }

    // Verifies that the file name length field is correctly read.
    template <typename FileInformationStructType> static void TestCaseBodyReadFileNameLength(void)
    {
        constexpr FILE_INFORMATION_CLASS testFileInformationClass = FileInformationStructType::kFileInformationClass;
        constexpr ULONG testValue = 0xccddeeff;

        FileInformationStructType testStruct{};
        testStruct.fileNameLength = testValue;

        TEST_ASSERT(testValue == FileInformationStructLayout::LayoutForFileInformationClass(testFileInformationClass)->ReadFileNameLength(&testStruct));
    }
    TEST_CASE(FileInformationStructLayout_ReadFileNameLength)
    {
        TestCaseBodyReadFileNameLength<SFileDirectoryInformation>();
        TestCaseBodyReadFileNameLength<SFileFullDirectoryInformation>();
        TestCaseBodyReadFileNameLength<SFileBothDirectoryInformation>();
        TestCaseBodyReadFileNameLength<SFileNamesInformation>();
        TestCaseBodyReadFileNameLength<SFileIdBothDirectoryInformation>();
        TestCaseBodyReadFileNameLength<SFileIdFullDirectoryInformation>();
        TestCaseBodyReadFileNameLength<SFileIdGlobalTxDirectoryInformation>();
        TestCaseBodyReadFileNameLength<SFileIdExtdDirectoryInformation>();
        TestCaseBodyReadFileNameLength<SFileIdExtdBothDirectoryInformation>();
    }

    // Verifies that the trailing file name field is correctly read.
    template <typename FileInformationStructType> static void TestCaseBodyReadFileName(void)
    {
        constexpr FILE_INFORMATION_CLASS testFileInformationClass = FileInformationStructType::kFileInformationClass;
        constexpr std::wstring_view testValue = L"AbCdEfG hIjKlMnOp";

        auto testStructBuffer = InitializeFileInformationStructBuffer();
        auto testStruct = reinterpret_cast<FileInformationStructType*>(testStructBuffer.Data());

        FileInformationStructLayout testStructLayout = FileInformationStructLayout::LayoutForFileInformationClass(testFileInformationClass).value();

        std::wmemcpy(testStructLayout.FileNamePointer(testStruct), testValue.data(), testValue.length());
        testStruct->fileNameLength = static_cast<ULONG>(testValue.length() * sizeof(testValue[0]));

        TEST_ASSERT(testValue == testStructLayout.ReadFileName(testStruct));
    }
    TEST_CASE(FileInformationStructLayout_ReadFileName)
    {
        TestCaseBodyReadFileName<SFileDirectoryInformation>();
        TestCaseBodyReadFileName<SFileFullDirectoryInformation>();
        TestCaseBodyReadFileName<SFileBothDirectoryInformation>();
        TestCaseBodyReadFileName<SFileNamesInformation>();
        TestCaseBodyReadFileName<SFileIdBothDirectoryInformation>();
        TestCaseBodyReadFileName<SFileIdFullDirectoryInformation>();
        TestCaseBodyReadFileName<SFileIdGlobalTxDirectoryInformation>();
        TestCaseBodyReadFileName<SFileIdExtdDirectoryInformation>();
        TestCaseBodyReadFileName<SFileIdExtdBothDirectoryInformation>();
    }

    // Verifies that the total size of a file information structure, in bytes, is correctly computed.
    template <typename FileInformationStructType> static void TestCaseBodySizeOfStruct(void)
    {
        constexpr FILE_INFORMATION_CLASS testFileInformationClass = FileInformationStructType::kFileInformationClass;
        constexpr std::wstring_view testValue = L"AbCdEfG hIjKlMnOp";

        auto testStructBuffer = InitializeFileInformationStructBuffer();
        auto testStruct = reinterpret_cast<FileInformationStructType*>(testStructBuffer.Data());

        FileInformationStructLayout testStructLayout = FileInformationStructLayout::LayoutForFileInformationClass(testFileInformationClass).value();

        std::wmemcpy(testStructLayout.FileNamePointer(testStruct), testValue.data(), testValue.length());
        testStruct->fileNameLength = static_cast<ULONG>(testValue.length() * sizeof(testValue[0]));

        TEST_ASSERT(LastWideCharacterInFileName(testStruct, testStructLayout.SizeOfStruct(testStruct)) == testValue.back());
    }
    TEST_CASE(FileInformationStructLayout_SizeOfStruct)
    {
        TestCaseBodySizeOfStruct<SFileDirectoryInformation>();
        TestCaseBodySizeOfStruct<SFileFullDirectoryInformation>();
        TestCaseBodySizeOfStruct<SFileBothDirectoryInformation>();
        TestCaseBodySizeOfStruct<SFileNamesInformation>();
        TestCaseBodySizeOfStruct<SFileIdBothDirectoryInformation>();
        TestCaseBodySizeOfStruct<SFileIdFullDirectoryInformation>();
        TestCaseBodySizeOfStruct<SFileIdGlobalTxDirectoryInformation>();
        TestCaseBodySizeOfStruct<SFileIdExtdDirectoryInformation>();
        TestCaseBodySizeOfStruct<SFileIdExtdBothDirectoryInformation>();
    }

    // Verifies that the next entry offset is correctly updated to reflect the total size of a file information structure.
    template <typename FileInformationStructType> static void TestCaseBodyUpdateNextEntryOffset(void)
    {
        constexpr FILE_INFORMATION_CLASS testFileInformationClass = FileInformationStructType::kFileInformationClass;

        auto testStructBuffer = InitializeFileInformationStructBuffer();
        auto testStruct = reinterpret_cast<FileInformationStructType*>(testStructBuffer.Data());

        FileInformationStructLayout testStructLayout = FileInformationStructLayout::LayoutForFileInformationClass(testFileInformationClass).value();

        testStructLayout.UpdateNextEntryOffset(testStruct);
        TEST_ASSERT(sizeof(*testStruct) == testStruct->nextEntryOffset);

        constexpr std::wstring_view testValue = L"AbCdEfG hIjKlMnOp";

        std::wmemcpy(testStructLayout.FileNamePointer(testStruct), testValue.data(), testValue.length());
        testStruct->fileNameLength = static_cast<ULONG>(testValue.length() * sizeof(testValue[0]));

        testStructLayout.UpdateNextEntryOffset(testStruct);
        TEST_ASSERT(testStructLayout.SizeOfStruct(testStruct) == testStruct->nextEntryOffset);
    }
    TEST_CASE(FileInformationStructLayout_UpdateNextEntryOffset)
    {
        TestCaseBodyUpdateNextEntryOffset<SFileDirectoryInformation>();
        TestCaseBodyUpdateNextEntryOffset<SFileFullDirectoryInformation>();
        TestCaseBodyUpdateNextEntryOffset<SFileBothDirectoryInformation>();
        TestCaseBodyUpdateNextEntryOffset<SFileNamesInformation>();
        TestCaseBodyUpdateNextEntryOffset<SFileIdBothDirectoryInformation>();
        TestCaseBodyUpdateNextEntryOffset<SFileIdFullDirectoryInformation>();
        TestCaseBodyUpdateNextEntryOffset<SFileIdGlobalTxDirectoryInformation>();
        TestCaseBodyUpdateNextEntryOffset<SFileIdExtdDirectoryInformation>();
        TestCaseBodyUpdateNextEntryOffset<SFileIdExtdBothDirectoryInformation>();
    }

    // Verifies that the file name length field can be correctly updated and consistency is maintained with the overall size of the structure.
    template <typename FileInformationStructType> static void TestCaseBodyWriteFileNameLength(void)
    {
        constexpr FILE_INFORMATION_CLASS testFileInformationClass = FileInformationStructType::kFileInformationClass;
        constexpr ULONG testValue = 100;

        FileInformationStructType testStruct{};
        FileInformationStructLayout testStructLayout = FileInformationStructLayout::LayoutForFileInformationClass(testFileInformationClass).value();

        testStructLayout.WriteFileNameLength(&testStruct, testValue);
        TEST_ASSERT(testValue == testStruct.fileNameLength);
        TEST_ASSERT(testStructLayout.SizeOfStruct(&testStruct) == testStruct.nextEntryOffset);
    }
    TEST_CASE(FileInformationStructLayout_WriteFileNameLength)
    {
        TestCaseBodyWriteFileNameLength<SFileDirectoryInformation>();
        TestCaseBodyWriteFileNameLength<SFileFullDirectoryInformation>();
        TestCaseBodyWriteFileNameLength<SFileBothDirectoryInformation>();
        TestCaseBodyWriteFileNameLength<SFileNamesInformation>();
        TestCaseBodyWriteFileNameLength<SFileIdBothDirectoryInformation>();
        TestCaseBodyWriteFileNameLength<SFileIdFullDirectoryInformation>();
        TestCaseBodyWriteFileNameLength<SFileIdGlobalTxDirectoryInformation>();
        TestCaseBodyWriteFileNameLength<SFileIdExtdDirectoryInformation>();
        TestCaseBodyWriteFileNameLength<SFileIdExtdBothDirectoryInformation>();
    }

    // Verifies that the trailing file name field can be correctly updated and consistency is maintained with both the length field and the overall size of the structure.
    // The buffer is large enough to contain both the structure and the entire filename.
    template <typename FileInformationStructType> static void TestCaseBodyWriteFileNameNominal(void)
    {
        constexpr FILE_INFORMATION_CLASS testFileInformationClass = FileInformationStructType::kFileInformationClass;
        constexpr std::wstring_view testValue = L"AbCdEfG hIjKlMnOp";

        auto testStructBuffer = InitializeFileInformationStructBuffer();
        auto testStruct = reinterpret_cast<FileInformationStructType*>(testStructBuffer.Data());

        FileInformationStructLayout testStructLayout = FileInformationStructLayout::LayoutForFileInformationClass(testFileInformationClass).value();

        testStructLayout.WriteFileName(testStruct, testValue, testStructBuffer.Size());

        TEST_ASSERT((testValue.length() * sizeof(testValue[0])) == testStruct->fileNameLength);
        TEST_ASSERT(testStructLayout.SizeOfStruct(testStruct) == testStruct->nextEntryOffset);
        TEST_ASSERT(LastWideCharacterInFileName(testStruct, testStructLayout.SizeOfStruct(testStruct)) == testValue.back());
    }
    TEST_CASE(FileInformationStructLayout_WriteFileName_Nominal)
    {
        TestCaseBodyWriteFileNameNominal<SFileDirectoryInformation>();
        TestCaseBodyWriteFileNameNominal<SFileFullDirectoryInformation>();
        TestCaseBodyWriteFileNameNominal<SFileBothDirectoryInformation>();
        TestCaseBodyWriteFileNameNominal<SFileNamesInformation>();
        TestCaseBodyWriteFileNameNominal<SFileIdBothDirectoryInformation>();
        TestCaseBodyWriteFileNameNominal<SFileIdFullDirectoryInformation>();
        TestCaseBodyWriteFileNameNominal<SFileIdGlobalTxDirectoryInformation>();
        TestCaseBodyWriteFileNameNominal<SFileIdExtdDirectoryInformation>();
        TestCaseBodyWriteFileNameNominal<SFileIdExtdBothDirectoryInformation>();
    }

    // Verifies that the trailing file name field can be correctly updated and consistency is maintained with both the length field and the overall size of the structure.
    // The buffer is too small to contain the entire filename.
    template <typename FileInformationStructType> static void TestCaseBodyWriteFileNameShortWrite(void)
    {
        constexpr FILE_INFORMATION_CLASS testFileInformationClass = FileInformationStructType::kFileInformationClass;
        constexpr std::wstring_view testValue = L"AbCdEfG hIjKlMnOp QrStUv WxYz";
        constexpr unsigned int bufferSize = static_cast<unsigned int>(sizeof(FileInformationStructType)) + 10;
        constexpr unsigned int expectedFileNameLength = bufferSize - offsetof(FileInformationStructType, fileName);
        constexpr wchar_t expectedLastFileNameChar = testValue[(expectedFileNameLength / sizeof(wchar_t)) - 1];

        auto testStructBuffer = InitializeFileInformationStructBuffer();
        auto testStruct = reinterpret_cast<FileInformationStructType*>(testStructBuffer.Data());

        FileInformationStructLayout testStructLayout = FileInformationStructLayout::LayoutForFileInformationClass(testFileInformationClass).value();

        testStructLayout.WriteFileName(testStruct, testValue, bufferSize);

        TEST_ASSERT(expectedFileNameLength == testStruct->fileNameLength);
        TEST_ASSERT(testStructLayout.SizeOfStruct(testStruct) == testStruct->nextEntryOffset);
        TEST_ASSERT(LastWideCharacterInFileName(testStruct, testStructLayout.SizeOfStruct(testStruct)) == expectedLastFileNameChar);

        // This loop verifies that the next 100 bytes after the size of the buffer provided to write the filename has not been touched and is still all 0 as initialized.
        const uint8_t* remainingBuffer = &testStructBuffer[bufferSize];
        for (int i = 0; i < 100; ++i)
            TEST_ASSERT(0 == remainingBuffer[i]);
    }
    TEST_CASE(FileInformationStructLayout_WriteFileName_ShortWrite)
    {
        TestCaseBodyWriteFileNameShortWrite<SFileDirectoryInformation>();
        TestCaseBodyWriteFileNameShortWrite<SFileFullDirectoryInformation>();
        TestCaseBodyWriteFileNameShortWrite<SFileBothDirectoryInformation>();
        TestCaseBodyWriteFileNameShortWrite<SFileNamesInformation>();
        TestCaseBodyWriteFileNameShortWrite<SFileIdBothDirectoryInformation>();
        TestCaseBodyWriteFileNameShortWrite<SFileIdFullDirectoryInformation>();
        TestCaseBodyWriteFileNameShortWrite<SFileIdGlobalTxDirectoryInformation>();
        TestCaseBodyWriteFileNameShortWrite<SFileIdExtdDirectoryInformation>();
        TestCaseBodyWriteFileNameShortWrite<SFileIdExtdBothDirectoryInformation>();
    }
}
