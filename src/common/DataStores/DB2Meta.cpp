/*
 * Copyright (C) 2008-2018 TrinityCore <https://www.trinitycore.org/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "DB2Meta.h"
#include "Errors.h"

DB2Meta::DB2Meta(int32 indexField, uint32 fieldCount, uint32 layoutHash, char const* types, uint8 const* arraySizes, int32 parentIndexField)
    : IndexField(indexField), ParentIndexField(parentIndexField), FieldCount(fieldCount), LayoutHash(layoutHash), Types(types), ArraySizes(arraySizes)
{
}

bool DB2Meta::HasIndexFieldInData() const
{
    return IndexField != -1;
}

uint32 DB2Meta::GetIndexField() const
{
    return IndexField == -1 ? 0 : uint32(IndexField);
}

uint32 DB2Meta::GetRecordSize() const
{
    uint32 size = 0;
    for (uint32 i = 0; i < FieldCount; ++i)
    {
        for (uint8 j = 0; j < ArraySizes[i]; ++j)
        {
            switch (Types[i])
            {
                case FT_BYTE:
                    size += 1;
                    break;
                case FT_SHORT:
                    size += 2;
                    break;
                case FT_FLOAT:
                case FT_INT:
                    size += 4;
                    break;
                case FT_LONG:
                    size += 8;
                    break;
                case FT_STRING:
                    size += sizeof(LocalizedString);
                    break;
                case FT_STRING_NOT_LOCALIZED:
                    size += sizeof(char*);
                    break;
                default:
                    ASSERT(false, "Unsupported column type specified %c", Types[i]);
            }
        }
    }

    if (!HasIndexFieldInData())
        size += 4;

    return size;
}

uint32 DB2Meta::GetIndexFieldOffset() const
{
    if (IndexField == -1)
        return 0;

    uint32 offset = 0;

    for (int32 i = 0; i < IndexField; ++i)
    {
        for (uint8 j = 0; j < ArraySizes[i]; ++j)
        {
            switch (Types[i])
            {
                case FT_BYTE:
                    offset += 1;
                    break;
                case FT_SHORT:
                    offset += 2;
                    break;
                case FT_FLOAT:
                case FT_INT:
                    offset += 4;
                    break;
                case FT_LONG:
                    offset += 8;
                    break;
                case FT_STRING:
                    offset += sizeof(LocalizedString);
                    break;
                case FT_STRING_NOT_LOCALIZED:
                    offset += sizeof(char*);
                    break;
                default:
                    ABORT_MSG("Unsupported column type specified %c", Types[i]);
                    break;
            }
        }
    }

    return offset;
}

int32 DB2Meta::GetParentIndexFieldOffset() const
{
    if (ParentIndexField == -1)
        return -1;

    uint32 offset = 0;
    if (!HasIndexFieldInData())
        offset += 4;

    for (int32 i = 0; i < ParentIndexField; ++i)
    {
        for (uint8 j = 0; j < ArraySizes[i]; ++j)
        {
            switch (Types[i])
            {
                case FT_BYTE:
                    offset += 1;
                    break;
                case FT_SHORT:
                    offset += 2;
                    break;
                case FT_FLOAT:
                case FT_INT:
                    offset += 4;
                    break;
                case FT_LONG:
                    offset += 8;
                    break;
                case FT_STRING:
                    offset += sizeof(LocalizedString);
                    break;
                case FT_STRING_NOT_LOCALIZED:
                    offset += sizeof(char*);
                    break;
                default:
                    ASSERT(false, "Unsupported column type specified %c", Types[i]);
                    break;
            }
        }
    }

    return offset;
}

uint32 DB2Meta::GetDbIndexField() const
{
    if (IndexField == -1)
        return 0;

    uint32 index = 0;
    for (uint32 i = 0; i < FieldCount && i < uint32(IndexField); ++i)
        index += ArraySizes[i];

    return index;
}

uint32 DB2Meta::GetDbFieldCount() const
{
    uint32 fields = 0;
    for (uint32 i = 0; i < FieldCount; ++i)
        fields += ArraySizes[i];

    if (!HasIndexFieldInData())
        ++fields;

    return fields;
}

DB2FieldMeta::DB2FieldMeta(bool isSigned, DBCFormer type, char const* name)
    : IsSigned(isSigned), Type(type), Name(name)
{
}

