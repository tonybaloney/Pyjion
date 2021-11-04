/*
* The MIT License (MIT)
*
* Copyright (c) Microsoft Corporation
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included
* in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
* OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
* OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
* ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
* OTHER DEALINGS IN THE SOFTWARE.
*
*/

#include "peloader.h"

PEDecoder::PEDecoder(const char* filePath) {
    pe = peparse::ParsePEFromFile(filePath);
    if (!pe) {
        throw InvalidImageException(peparse::GetPEErrString().c_str());
    }
    vector<uint8_t> data = vector<uint8_t>(sizeof(IMAGE_COR20_HEADER));
    if (GetDataDirectoryEntry(pe, peparse::data_directory_kind::DIR_COM_DESCRIPTOR, data)) {
        memcpy(&corHeader, data.data(), sizeof(IMAGE_COR20_HEADER));
    } else {
        throw InvalidImageException("Failed to load image. Not a .NET DLL");
    }
    NativeFormat::NativeReader m_nativeReader = NativeFormat::NativeReader((PTR_CBYTE) pe->fileBuffer->buf, pe->peHeader.nt.OptionalHeader64.SizeOfImage);

    uint64_t offsetOfManagedHeader;
    if (convertAddress(pe, corHeader.ManagedNativeHeader.VirtualAddress, AddressType::RelativeVirtualAddress, AddressType::PhysicalOffset, offsetOfManagedHeader) && offsetOfManagedHeader) {
        memcpy(&r2rHeader, pe->fileBuffer->buf + offsetOfManagedHeader, sizeof(READYTORUN_HEADER));
        READYTORUN_SECTION sections[r2rHeader.CoreHeader.NumberOfSections];

        for (int i = 0; i < r2rHeader.CoreHeader.NumberOfSections; i++) {
            memcpy(&sections[i], pe->fileBuffer->buf + offsetOfManagedHeader + sizeof(READYTORUN_HEADER) + (i * sizeof(READYTORUN_SECTION)), sizeof(READYTORUN_SECTION));
            switch ((ReadyToRunSectionType)sections[i].Type) {
                case ReadyToRunSectionType::CompilerIdentifier: {
                    uint64_t offsetOfCompilerIdentifier;
                    if (!convertAddress(pe, sections[i].Section.VirtualAddress, AddressType::RelativeVirtualAddress, AddressType::PhysicalOffset, offsetOfCompilerIdentifier)) {
                        throw InvalidImageException("Failed to load image. Corrupt section table.");
                    }
                    strncpy(compilerIdentifier, reinterpret_cast<const char*>(pe->fileBuffer->buf + offsetOfCompilerIdentifier), sections[i].Section.Size <= COMPILER_ID_SIZE ? sections[i].Section.Size : COMPILER_ID_SIZE);
                } break;
                case ReadyToRunSectionType::ImportSections: {
                    READYTORUN_IMPORT_SECTION importSections[sections[i].Section.Size / sizeof(READYTORUN_IMPORT_SECTION)];
                    uint64_t offset;
                    if (!convertAddress(pe, sections[i].Section.VirtualAddress, AddressType::RelativeVirtualAddress, AddressType::PhysicalOffset, offset)) {
                        throw InvalidImageException("Failed to load image. Corrupt section table.");
                    }
                    memcpy(importSections, pe->fileBuffer->buf + offset, sections[i].Section.Size);
                    // Map import sections.
                    for(int j = 0; j < sections[i].Section.Size / sizeof(READYTORUN_IMPORT_SECTION); j++){
                        allImportSections.push_back(importSections[j]);
                    }
                } break;
                case ReadyToRunSectionType::RuntimeFunctions: {
                    RUNTIME_FUNCTION runtimeFunctions[sections[i].Section.Size / sizeof(RUNTIME_FUNCTION)];
                    uint64_t offset;
                    if (!convertAddress(pe, sections[i].Section.VirtualAddress, AddressType::RelativeVirtualAddress, AddressType::PhysicalOffset, offset)) {
                        throw InvalidImageException("Failed to load image. Corrupt section table.");
                    }
                    if (!offset)
                        break;
                    memcpy(runtimeFunctions, pe->fileBuffer->buf + offset, sections[i].Section.Size);
                    // Map import sections.
                    for(int j = 0; j < sections[i].Section.Size / sizeof(RUNTIME_FUNCTION); j++){
                        allRuntimeFunctions.push_back(runtimeFunctions[j]);
                    }
                } break;
                case ReadyToRunSectionType::MethodDefEntryPoints: {
                    uint64_t offset;
                    if (!convertAddress(pe, sections[i].Section.VirtualAddress, AddressType::RelativeVirtualAddress, AddressType::PhysicalOffset, offset)) {
                        throw InvalidImageException("Failed to load image. Corrupt section table.");
                    }
                    if (!offset)
                        break;
                    m_methodDefEntryPoints = NativeFormat::NativeArray(&m_nativeReader, offset);
                }
                break;
            }
        }
    }

    // Run import sections to get fixups and signatures..

    for (auto & section : allImportSections){
        if (section.Signatures) {
            uint64_t sigOffset;
            if (!convertAddress(pe, section.Signatures, AddressType::RelativeVirtualAddress, AddressType::PhysicalOffset, sigOffset)) {
                throw InvalidImageException("Failed to load image. Corrupt section table.");
            }

            uint64_t fixupsOffset;
            if (!convertAddress(pe, section.Section.VirtualAddress, AddressType::RelativeVirtualAddress, AddressType::PhysicalOffset, fixupsOffset)) {
                throw InvalidImageException("Failed to load image. Corrupt section table.");
            }

            PVOID *pFixups = (PVOID *)(DWORD *)(pe->fileBuffer->buf + fixupsOffset);
            DWORD nFixups = section.Section.Size / sizeof(void*);
            DWORD * signatures = (DWORD *)(pe->fileBuffer->buf + sigOffset);
            for (DWORD i = 0; i < nFixups; i++)
            {
                uint64_t pOffset;
                convertAddress(pe, signatures[i], AddressType::RelativeVirtualAddress, AddressType::PhysicalOffset, pOffset);
                PBYTE pSig = (PBYTE)(pe->fileBuffer->buf + pOffset);
            }
        }
    }
    // Read metadata tables
    uint64_t metaDataBase;
    if (!convertAddress(pe, corHeader.MetaData.VirtualAddress, AddressType::RelativeVirtualAddress, AddressType::PhysicalOffset, metaDataBase)){
        throw InvalidImageException("Failed to load image. Corrupt metadata table.");
    }
    ECMA_STORAGE_HEADER header = {};
    auto cursor = pe->fileBuffer->buf + metaDataBase;
    memcpy(&header, cursor, sizeof(ECMA_STORAGE_HEADER));
    if (header.lSignature != 0x424A5342){
        throw InvalidImageException("Failed to load image. Corrupt metadata table.");
    }
    cursor += sizeof(ECMA_STORAGE_HEADER);
    assemblyVersion = std::string();
    for (int i = 0; i < header.iVersionString; i++){
        assemblyVersion.push_back(*(cursor));
        cursor ++;
    }
    ECMA_STORAGE_HEADER2 header2 = {};
    memcpy(&header2, cursor, sizeof(ECMA_STORAGE_HEADER2));
    cursor += sizeof(ECMA_STORAGE_HEADER2);
    streamHeaders = vector<ECMA_STREAM_HEADER>(header2.iStreams);
    for (int idx = 0; idx < header2.iStreams; idx ++){
        memcpy(&streamHeaders[idx], cursor, sizeof(ECMA_STREAM_HEADER));
        int nameLen = (int)(strlen(streamHeaders[idx].name) + 1);
        nameLen = ALIGN4BYTE(nameLen);
        cursor += (sizeof(ULONG) * 2) + nameLen;
        if (strcmp(streamHeaders[idx].name, "#~") == 0){
            // Read logical tables.
            HOME_TABLE_HEADER htHeader = {};

            memcpy(&htHeader, pe->fileBuffer->buf + metaDataBase + streamHeaders[idx].offset, sizeof(HOME_TABLE_HEADER));
            assert(htHeader.Reserved1 == 0);
            if (htHeader.MajorVersion != 2){
                throw InvalidImageException("Image version meta data table not supported");
            }
            uint32_t tableSizes [htHeader.MaskValid.count()];
            auto tableCursor = pe->fileBuffer->buf + metaDataBase + streamHeaders[idx].offset + sizeof(HOME_TABLE_HEADER);

            memcpy(tableSizes, tableCursor, sizeof(uint32_t) * htHeader.MaskValid.count());
            tableCursor += sizeof(uint32_t) * htHeader.MaskValid.count();
            int pt = 0;

            assert(sizeof(ModuleTableRow) == 10);
            assert(sizeof(TypeRefRow) == 6);
            assert(sizeof(TypeDefRow) == 14);
            assert(sizeof(FieldRow) == 6);

            for (const auto t: AllMetaDataTables){
                if (htHeader.MaskValid[t]) {
                    metaDataTableSizes[t] = tableSizes[pt];
                    // Load Table.
                    switch (t){
                        case MetaDataTable_Module:
                            moduleTable = vector<ModuleTableRow>(metaDataTableSizes[t]);
                            memcpy(moduleTable.data(), tableCursor, sizeof(ModuleTableRow) * metaDataTableSizes[t]);
                            tableCursor += sizeof(ModuleTableRow) * metaDataTableSizes[t];
                            break;
                        case MetaDataTable_TypeRef:
                            typeRefTable = vector<TypeRefRow>(metaDataTableSizes[t]);
                            memcpy(typeRefTable.data(), tableCursor, sizeof(TypeRefRow) * metaDataTableSizes[t]);
                            tableCursor += sizeof(TypeRefRow) * metaDataTableSizes[t];
                            break;
                        case MetaDataTable_TypeDef:
                            typeDefTable = vector<TypeDefRow>(metaDataTableSizes[t]);
                            memcpy(typeDefTable.data(), tableCursor, sizeof(TypeDefRow) * metaDataTableSizes[t]);
                            tableCursor += sizeof(TypeDefRow) * metaDataTableSizes[t];
                            break;
                        case MetaDataTable_FieldPtr:
                            // Not implemented
                            break;
                        case MetaDataTable_Field:
                            fieldTable = vector<FieldRow>(metaDataTableSizes[t]);
                            memcpy(fieldTable.data(), tableCursor, sizeof(FieldRow) * metaDataTableSizes[t]);
                            tableCursor += sizeof(FieldRow) * metaDataTableSizes[t];
                            break;
                        case MetaDataTable_MethodPtr:
                            // Not implemented
                            break;
                        case MetaDataTable_Method:
                            methodTable = vector<MethodRow>(metaDataTableSizes[t]);
                            memcpy(methodTable.data(), tableCursor, sizeof(MethodRow) * metaDataTableSizes[t]);
                            tableCursor += sizeof(MethodRow) * metaDataTableSizes[t];
                            break;
                        case MetaDataTable_ParamPtr:
                            // Not implemented
                            break;
                        case MetaDataTable_Param:
                            paramTable = vector<ParamRow>(metaDataTableSizes[t]);
                            memcpy(paramTable.data(), tableCursor, sizeof(ParamRow) * metaDataTableSizes[t]);
                            tableCursor += sizeof(ParamRow) * metaDataTableSizes[t];
                            break;
                        case MetaDataTable_InterfaceImpl:
                            interfaceImplTable = vector<InterfaceImplRow>(metaDataTableSizes[t]);
                            memcpy(interfaceImplTable.data(), tableCursor, sizeof(InterfaceImplRow) * metaDataTableSizes[t]);
                            tableCursor += sizeof(InterfaceImplRow) * metaDataTableSizes[t];
                            break;
                    }
                    pt++;
                } else {
                    metaDataTableSizes[t] = 0;
                }
            }
        } else if(strcmp(streamHeaders[idx].name, "#Strings") == 0){
            auto stringHeapEnd = pe->fileBuffer->buf + metaDataBase + streamHeaders[idx].offset + streamHeaders[idx].size;
            auto stringHeapPosition = pe->fileBuffer->buf + metaDataBase + streamHeaders[idx].offset;
            while (stringHeapPosition <= stringHeapEnd){
                stringHeap.push_back(*stringHeapPosition);
                stringHeapPosition++;
            }
        } else if(strcmp(streamHeaders[idx].name, "#Blob") == 0){
            blobHeap.resize(streamHeaders[idx].size);
            memcpy(blobHeap.data(), pe->fileBuffer->buf + metaDataBase + streamHeaders[idx].offset, streamHeaders[idx].size);
        } else if(strcmp(streamHeaders[idx].name, "#GUID") == 0){
            guidHeap.resize(streamHeaders[idx].size);
            memcpy(guidHeap.data(), pe->fileBuffer->buf + metaDataBase + streamHeaders[idx].offset, streamHeaders[idx].size);
        } else if(strcmp(streamHeaders[idx].name, "#US") == 0){
            usHeap.resize(streamHeaders[idx].size);
            memcpy(usHeap.data(), pe->fileBuffer->buf + metaDataBase + streamHeaders[idx].offset, streamHeaders[idx].size);
        }
    }
}

bool convertAddress(peparse::parsed_pe * pe,
                    std::uint64_t address,
                    AddressType source_type,
                    AddressType destination_type,
                    std::uint64_t &result) {
    if (source_type == destination_type) {
        result = address;
        return true;
    }

    std::uint64_t image_base_address = 0U;
    if (pe->peHeader.nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64) {
        image_base_address = pe->peHeader.nt.OptionalHeader64.ImageBase;
    } else {
        image_base_address = pe->peHeader.nt.OptionalHeader.ImageBase;
    }

    struct SectionAddressLimits final {
        std::uintptr_t lowest_rva;
        std::uintptr_t lowest_offset;

        std::uintptr_t highest_rva;
        std::uintptr_t highest_offset;
    };

    auto L_getSectionAddressLimits =
            [](void *N,
               const peparse::VA &secBase,
               const std::string &secName,
               const peparse::image_section_header &s,
               const peparse::bounded_buffer *data) -> int {
        static_cast<void>(secBase);
        static_cast<void>(secName);
        static_cast<void>(data);

        SectionAddressLimits *section_address_limits =
                static_cast<SectionAddressLimits *>(N);

        section_address_limits->lowest_rva =
                std::min(section_address_limits->lowest_rva,
                         static_cast<std::uintptr_t>(s.VirtualAddress));

        section_address_limits->lowest_offset =
                std::min(section_address_limits->lowest_offset,
                         static_cast<std::uintptr_t>(s.PointerToRawData));

        std::uintptr_t sectionSize;
        if (s.SizeOfRawData != 0) {
            sectionSize = s.SizeOfRawData;
        } else {
            sectionSize = s.Misc.VirtualSize;
        }

        section_address_limits->highest_rva =
                std::max(section_address_limits->highest_rva,
                         static_cast<std::uintptr_t>(s.VirtualAddress + sectionSize));

        section_address_limits->highest_offset =
                std::max(section_address_limits->highest_offset,
                         static_cast<std::uintptr_t>(s.PointerToRawData + sectionSize));

        return 0;
    };

    SectionAddressLimits section_address_limits = {
            std::numeric_limits<std::uintptr_t>::max(),
            std::numeric_limits<std::uintptr_t>::max(),
            std::numeric_limits<std::uintptr_t>::min(),
            std::numeric_limits<std::uintptr_t>::min()};

    IterSec(pe, L_getSectionAddressLimits, &section_address_limits);

    switch (source_type) {
        case AddressType::PhysicalOffset: {
            if (address >= section_address_limits.highest_offset) {
                return false;
            }

            if (destination_type == AddressType::RelativeVirtualAddress) {
                struct CallbackData final {
                    bool found;
                    std::uint64_t address;
                    std::uint64_t result;
                };

                auto L_inspectSection = [](void *N,
                                           const peparse::VA &secBase,
                                           const std::string &secName,
                                           const peparse::image_section_header &s,
                                           const peparse::bounded_buffer *data) -> int {
                    static_cast<void>(secBase);
                    static_cast<void>(secName);
                    static_cast<void>(data);

                    std::uintptr_t sectionBaseOffset = s.PointerToRawData;

                    std::uintptr_t sectionEndOffset = sectionBaseOffset;
                    if (s.SizeOfRawData != 0) {
                        sectionEndOffset += s.SizeOfRawData;
                    } else {
                        sectionEndOffset += s.Misc.VirtualSize;
                    }

                    auto callback_data = static_cast<CallbackData *>(N);
                    if (callback_data->address >= sectionBaseOffset &&
                        callback_data->address < sectionEndOffset) {
                        callback_data->result = s.VirtualAddress + (callback_data->address -
                                                                    s.PointerToRawData);

                        callback_data->found = true;
                        return 1;
                    }

                    return 0;
                };

                CallbackData callback_data = {false, address, 0U};
                IterSec(pe, L_inspectSection, &callback_data);

                if (!callback_data.found) {
                    return false;
                }

                result = callback_data.result;
                return true;

            } else if (destination_type == AddressType::VirtualAddress) {
                std::uint64_t rva = 0U;
                if (!convertAddress(pe,
                                    address,
                                    source_type,
                                    AddressType::RelativeVirtualAddress,
                                    rva)) {
                    return false;
                }

                result = image_base_address + rva;
                return true;
            }

            return false;
        }

        case AddressType::RelativeVirtualAddress: {
            if (address < section_address_limits.lowest_rva) {
                result = address;
                return true;
            } else if (address >= section_address_limits.highest_rva) {
                return false;
            }

            if (destination_type == AddressType::PhysicalOffset) {
                struct CallbackData final {
                    bool found;
                    std::uint64_t address;
                    std::uint64_t result;
                };

                auto L_inspectSection = [](void *N,
                                           const peparse::VA &secBase,
                                           const std::string &secName,
                                           const peparse::image_section_header &s,
                                           const peparse::bounded_buffer *data) -> int {
                    static_cast<void>(secBase);
                    static_cast<void>(secName);
                    static_cast<void>(data);

                    std::uintptr_t sectionBaseAddress = s.VirtualAddress;
                    std::uintptr_t sectionEndAddress =
                            sectionBaseAddress + s.Misc.VirtualSize;

                    auto callback_data = static_cast<CallbackData *>(N);
                    if (callback_data->address >= sectionBaseAddress &&
                        callback_data->address < sectionEndAddress) {
                        callback_data->result =
                                s.PointerToRawData +
                                (callback_data->address - sectionBaseAddress);

                        callback_data->found = true;
                        return 1;
                    }

                    return 0;
                };

                CallbackData callback_data = {false, address, 0U};
                IterSec(pe, L_inspectSection, &callback_data);

                if (!callback_data.found) {
                    return false;
                }

                result = callback_data.result;
                return true;

            } else if (destination_type == AddressType::VirtualAddress) {
                result = image_base_address + address;
                return true;
            }

            return false;
        }

        case AddressType::VirtualAddress: {
            if (address < image_base_address) {
                return false;
            }

            std::uint64_t rva = address - image_base_address;
            return convertAddress(pe,
                                  rva,
                                  AddressType::RelativeVirtualAddress,
                                  destination_type,
                                  result);
        }

        default: {
            return false;
        }
    }
}
