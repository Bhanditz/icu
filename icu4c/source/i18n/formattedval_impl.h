// © 2018 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#ifndef __FORMVAL_IMPL_H__
#define __FORMVAL_IMPL_H__

#include "unicode/utypes.h"
#if !UCONFIG_NO_FORMATTING

// This file contains compliant implementations of FormattedValue which can be
// leveraged by ICU formatters.
//
// Each implementation is defined in its own cpp file in order to split
// dependencies more modularly.

#include "unicode/formattedvalue.h"
#include "capi_helper.h"
#include "fphdlimp.h"
#include "util.h"
#include "uvectr32.h"

U_NAMESPACE_BEGIN


/** Implementation using FieldPositionHandler to accept fields. */
class FormattedValueFieldPositionIteratorImpl : public UMemory, public FormattedValue {
public:

    /** @param initialFieldCapacity Initially allocate space for this many fields. */
    FormattedValueFieldPositionIteratorImpl(int32_t initialFieldCapacity, UErrorCode& status);

    virtual ~FormattedValueFieldPositionIteratorImpl();

    // Implementation of FormattedValue (const):

    UnicodeString toString(UErrorCode& status) const U_OVERRIDE;
    UnicodeString toTempString(UErrorCode& status) const U_OVERRIDE;
    Appendable& appendTo(Appendable& appendable, UErrorCode& status) const U_OVERRIDE;
    UBool nextPosition(ConstrainedFieldPosition& cfpos, UErrorCode& status) const U_OVERRIDE;

    // Additional methods used during construction phase only (non-const):

    FieldPositionIteratorHandler getHandler(UErrorCode& status);
    void appendString(UnicodeString string, UErrorCode& status);

private:
    // Final data:
    UnicodeString fString;
    UVector32 fFields;
};


// C API Helpers for FormattedValue
// Magic number as ASCII == "UFV"
struct UFormattedValueImpl;
typedef IcuCApiHelper<UFormattedValue, UFormattedValueImpl, 0x55465600> UFormattedValueApiHelper;
struct UFormattedValueImpl : public UMemory, public UFormattedValueApiHelper {
    // This pointer should be set by the child class.
    FormattedValue* fFormattedValue = nullptr;
};


/** Implementation of the methods from U_FORMATTED_VALUE_SUBCLASS_AUTO. */
#define UPRV_FORMATTED_VALUE_SUBCLASS_AUTO_IMPL(Name) \
    Name::Name(Name&& src) U_NOEXCEPT { \
        fData = src.fData; \
        src.fData = nullptr; \
        fErrorCode = src.fErrorCode; \
        src.fErrorCode = U_INVALID_STATE_ERROR; \
    } \
    Name::~Name() { \
        delete fData; \
        fData = nullptr; \
    } \
    Name& Name::operator=(Name&& src) U_NOEXCEPT { \
        delete fData; \
        fData = src.fData; \
        src.fData = nullptr; \
        fErrorCode = src.fErrorCode; \
        src.fErrorCode = U_INVALID_STATE_ERROR; \
        return *this; \
    } \
    UnicodeString Name::toString(UErrorCode& status) const { \
        if (U_FAILURE(status)) { \
            return ICU_Utility::makeBogusString(); \
        } \
        if (fData == nullptr) { \
            status = fErrorCode; \
            return ICU_Utility::makeBogusString(); \
        } \
        return fData->toString(status); \
    } \
    UnicodeString Name::toTempString(UErrorCode& status) const { \
        if (U_FAILURE(status)) { \
            return ICU_Utility::makeBogusString(); \
        } \
        if (fData == nullptr) { \
            status = fErrorCode; \
            return ICU_Utility::makeBogusString(); \
        } \
        return fData->toTempString(status); \
    } \
    Appendable& Name::appendTo(Appendable& appendable, UErrorCode& status) const { \
        if (U_FAILURE(status)) { \
            return appendable; \
        } \
        if (fData == nullptr) { \
            status = fErrorCode; \
            return appendable; \
        } \
        return fData->appendTo(appendable, status); \
    } \
    UBool Name::nextPosition(ConstrainedFieldPosition& cfpos, UErrorCode& status) const { \
        if (U_FAILURE(status)) { \
            return FALSE; \
        } \
        if (fData == nullptr) { \
            status = fErrorCode; \
            return FALSE; \
        } \
        return fData->nextPosition(cfpos, status); \
    }


/**
 * Implementation of the standard methods for a UFormattedValue "subclass" C API.
 * @param CPPType The public C++ type, like FormattedList
 * @param CType The public C type, like UFormattedList
 * @param ImplType A name to use for the implementation class
 * @param HelperType A name to use for the "mixin" typedef for C API conversion
 * @param Prefix The C API prefix, like ulistfmt
 * @param MagicNumber A unique 32-bit number to use to identify this type
 */
#define UPRV_FORMATTED_VALUE_CAPI_AUTO_IMPL(CPPType, CType, ImplType, HelperType, Prefix, MagicNumber) \
    U_NAMESPACE_BEGIN \
    class ImplType; \
    typedef IcuCApiHelper<CType, ImplType, MagicNumber> HelperType; \
    class ImplType : public UFormattedValueImpl, public HelperType { \
    public: \
        ImplType(); \
        ~ImplType(); \
        CPPType fImpl; \
    }; \
    ImplType::ImplType() { \
        fFormattedValue = &fImpl; \
    } \
    ImplType::~ImplType() {} \
    U_NAMESPACE_END \
    U_CAPI CType* U_EXPORT2 \
    Prefix ## _openResult (UErrorCode* ec) { \
        if (U_FAILURE(*ec)) { \
            return nullptr; \
        } \
        ImplType* impl = new ImplType(); \
        if (impl == nullptr) { \
            *ec = U_MEMORY_ALLOCATION_ERROR; \
            return nullptr; \
        } \
        return static_cast<HelperType*>(impl)->exportForC(); \
    } \
    U_DRAFT const UFormattedValue* U_EXPORT2 \
    Prefix ## _resultAsValue (const CType* uresult, UErrorCode* ec) { \
        const ImplType* result = HelperType::validate(uresult, *ec); \
        if (U_FAILURE(*ec)) { return nullptr; } \
        return static_cast<const UFormattedValueApiHelper*>(result)->exportConstForC(); \
    } \
    U_CAPI void U_EXPORT2 \
    Prefix ## _closeResult (CType* uresult) { \
        UErrorCode localStatus = U_ZERO_ERROR; \
        const ImplType* impl = HelperType::validate(uresult, localStatus); \
        delete impl; \
    }


U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_FORMATTING */
#endif // __FORMVAL_IMPL_H__
