//
//  file:  rematch.cpp    
//
//         Contains the implementation of class RegexMatcher,
//         which is one of the main API classes for the ICU regular expression package.
//
/*
**********************************************************************
*   Copyright (C) 2002 International Business Machines Corporation   *
*   and others. All rights reserved.                                 *
**********************************************************************
*/

#include "unicode/utypes.h"
#if !UCONFIG_NO_REGULAR_EXPRESSIONS

#include "unicode/regex.h"
#include "unicode/uniset.h"
#include "unicode/uchar.h"
#include "unicode/ustring.h"
#include "uassert.h"
#include "uvector.h"
#include "uvectr32.h"
#include "regeximp.h"

#include "stdio.h"

U_NAMESPACE_BEGIN

//-----------------------------------------------------------------------------
//
//   Constructor and Destructor
//
//-----------------------------------------------------------------------------
RegexMatcher::RegexMatcher(const RegexPattern *pat)  { 
    fPattern           = pat;
    fInput             = NULL;
    fInputUC           = NULL;
    fInputLength       = 0;
    UErrorCode  status = U_ZERO_ERROR;
    fBackTrackStack    = new UVector32(status);   // TODO:  do something with status.
    fCaptureStarts     = new UVector32(status);
    fCaptureEnds       = new UVector32(status);
    int i;
    for (i=0; i<=fPattern->fNumCaptureGroups; i++) {
        fCaptureStarts->addElement(-1, status);
        fCaptureEnds  ->addElement(-1, status);
    }
    reset();
}



RegexMatcher::~RegexMatcher() {
    delete fBackTrackStack;
    delete fCaptureStarts;
    delete fCaptureEnds;
}



static const UChar BACKSLASH  = 0x5c;
static const UChar DOLLARSIGN = 0x24;
//--------------------------------------------------------------------------------
//
//    appendReplacement
//
//--------------------------------------------------------------------------------
RegexMatcher &RegexMatcher::appendReplacement(UnicodeString &dest,
                                              const UnicodeString &replacement,
                                              UErrorCode &status) {
    if (U_FAILURE(status)) {
        return *this;
    }
    if (fMatch == FALSE) {
        status = U_REGEX_INVALID_STATE;
        return *this;
    }

    // Copy input string from the end of previous match to start of current match
    int32_t  len = fMatchStart-fLastMatchEnd;
    if (len > 0) {
        dest.append(*fInput, fLastMatchEnd, len);
    }
    

    // scan the replacement text, looking for substitutions ($n) and \escapes.
    //  TODO:  optimize this loop by efficiently scanning for '$' or '\'
    int32_t  replLen = replacement.length();
    int32_t  replIdx = 0;
    while (replIdx<replLen) {
        UChar  c = replacement.charAt(replIdx);
        replIdx++;
        if (c == BACKSLASH) {
            // Backslash Escape.  Copy the following char out without further checks.
            //                    Note:  Surrogate pairs don't need any special handling
            //                           The second half wont be a '$' or a '\', and
            //                           will move to the dest normally on the next
            //                           loop iteration.
            if (replIdx >= replLen) {
                break;
            }
            c = replacement.charAt(replIdx);
            replIdx++;
            dest.append(c);
            continue;
        }

        if (c != DOLLARSIGN) {
            // Normal char, not a $.  Copy it out without further checks.
            dest.append(c);
            continue;
        }

        // We've got a $.  Pick up a capture group number if one follows.
        // Consume at most the number of digits necessary for the largest capture
        // number that is valid for this pattern.

        int32_t numDigits = 0;
        int32_t groupNum  = 0;
        UChar32 digitC;
        for (;;) {
            if (replIdx >= replLen) {
                break;
            }
            digitC = replacement.char32At(replIdx);
            if (u_isdigit(digitC) == FALSE) {
                break;
            }
            replIdx = replacement.moveIndex32(replIdx, 1);
            groupNum=groupNum*10 + u_charDigitValue(digitC);
            numDigits++;
            if (numDigits >= fPattern->fMaxCaptureDigits) {
                break;
            }
        }


        if (numDigits == 0) {
            // The $ didn't introduce a group number at all.
            // Treat it as just part of the substitution text.
            dest.append(DOLLARSIGN);
            continue;
        }

        // Finally, append the capture group data to the destination.
        dest.append(group(groupNum, status));
        if (U_FAILURE(status)) {
            // Can fail if group number is out of range.
            break;
        }

    }

    return *this;
}



//--------------------------------------------------------------------------------
//
//    appendTail     Intended to be used in conjunction with appendReplacement()
//                   To the destination string, append everything following
//                   the last match position from the input string.
//
//--------------------------------------------------------------------------------
UnicodeString &RegexMatcher::appendTail(UnicodeString &dest) {
    int32_t  len = fInputLength-fMatchEnd;
    if (len > 0) {
        dest.append(*fInput, fMatchEnd, len);
    }
    return dest;
}



//--------------------------------------------------------------------------------
//
//   end
//
//--------------------------------------------------------------------------------
int32_t RegexMatcher::end(UErrorCode &err) const {
    return end(0, err);
}



int32_t RegexMatcher::end(int group, UErrorCode &err) const {
    if (U_FAILURE(err)) {
        return -1;
    }
    if (fMatch == FALSE) {
        err = U_REGEX_INVALID_STATE;
        return -1;
    }
    if (group < 0 || group > fPattern->fNumCaptureGroups) {
        err = U_INDEX_OUTOFBOUNDS_ERROR;
        return -1;
    }
    int32_t e = -1;
    if (group == 0) {
        e = fMatchEnd; 
    } else {
        // Note:  When the match engine backs out of a capture group, it sets the
        //        group's start position to -1.  The end position is left with junk.
        //        So, before returning an end position, we must first check that
        //        the start position indicates that the group matched something.
        int32_t s = fCaptureStarts->elementAti(group);
        if (s  != -1) {
            e = fCaptureEnds->elementAti(group);
        }
    }
    return e;
}



//--------------------------------------------------------------------------------
//
//   find()
//
//--------------------------------------------------------------------------------
UBool RegexMatcher::find() {
    // Start at the position of the last match end.  (Will be zero if the
    //   matcher has been reset.
    UErrorCode status = U_ZERO_ERROR;

    int32_t  startPos;
    for (startPos=fMatchEnd; startPos < fInputLength; startPos = fInput->moveIndex32(startPos, 1)) {
        MatchAt(startPos, status);
        if (U_FAILURE(status)) {
            return FALSE;
        }
        if (fMatch) {
            return TRUE;
        }
    }
    return FALSE;
}



UBool RegexMatcher::find(int32_t start, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return FALSE;
    }
    if (start < 0 || start >= fInputLength) {
        status = U_INDEX_OUTOFBOUNDS_ERROR;
        return FALSE;
    }
    this->reset();

    // TODO:  optimize a search for the first char of a possible match.
    // TODO:  optimize the search for a leading literal string.
    // TODO:  optimize based on the minimum length of a possible match
    int32_t  startPos;
    for (startPos=start; startPos < fInputLength; startPos=fInput->moveIndex32(startPos, 1)) {
        MatchAt(startPos, status);
        if (U_FAILURE(status)) {
            return FALSE;
        }
        if (fMatch) {
            return TRUE;
        }
    }
    return FALSE;
}



//--------------------------------------------------------------------------------
//
//  group()
//
//--------------------------------------------------------------------------------
UnicodeString RegexMatcher::group(UErrorCode &status) const {
    return group(0, status);
}



UnicodeString RegexMatcher::group(int32_t groupNum, UErrorCode &status) const {
    int32_t  s = start(groupNum, status);
    int32_t  e = end(groupNum, status);

    // Note:  calling start() and end() above will do all necessary checking that
    //        the group number is OK and that a match exists.  status will be set.
    if (U_FAILURE(status)) {
        return UnicodeString();
    }

    if (s < 0) {
        // A capture group wasn't part of the match 
        return UnicodeString();
    }
    U_ASSERT(s <= e);
    return UnicodeString(*fInput, s, e-s);
}




int32_t RegexMatcher::groupCount() const {
    return fPattern->fNumCaptureGroups;
}



const UnicodeString &RegexMatcher::input() const {
    return *fInput;
}




UBool RegexMatcher::lookingAt(UErrorCode &status) {
    if (U_FAILURE(status)) {
        return FALSE;
    }
    reset();
    MatchAt(0, status);
    return fMatch;
}



UBool RegexMatcher::matches(UErrorCode &status) {
    if (U_FAILURE(status)) {
        return FALSE;
    }
    reset();
    MatchAt(0, status);
    UBool   success  = (fMatch && fMatchEnd==fInputLength);
    return success;
}




const RegexPattern &RegexMatcher::pattern() const {
    return *fPattern;
}



//--------------------------------------------------------------------------------
//
//    replaceAll
//
//--------------------------------------------------------------------------------
UnicodeString RegexMatcher::replaceAll(const UnicodeString &replacement, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return *fInput;
    }
    UnicodeString destString;
    for (reset(); find(); ) {
        appendReplacement(destString, replacement, status);
        if (U_FAILURE(status)) {
            break;
        }
    }
    appendTail(destString);
    return destString;
}




//--------------------------------------------------------------------------------
//
//    replaceFirst
//
//--------------------------------------------------------------------------------
UnicodeString RegexMatcher::replaceFirst(const UnicodeString &replacement, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return *fInput;
    }
    reset();
    if (!find()) {
        return *fInput;
    }

    UnicodeString destString;
    appendReplacement(destString, replacement, status);
    appendTail(destString);
    return destString;
}



//--------------------------------------------------------------------------------
//
//     reset
//
//--------------------------------------------------------------------------------
RegexMatcher &RegexMatcher::reset() {
    fMatchStart   = 0;
    fMatchEnd     = 0;
    fLastMatchEnd = 0;
    fMatch        = FALSE;
    int i;
    for (i=0; i<=fPattern->fNumCaptureGroups; i++) {
        fCaptureStarts->setElementAt(-1, i);
    }
    
    return *this;
}



RegexMatcher &RegexMatcher::reset(const UnicodeString &input) {
    fInput          = &input;
    fInputLength    = input.length();
    fInputUC        = fInput->getBuffer();
    reset();
    return *this;
}



//--------------------------------------------------------------------------------
//
//     start
//
//--------------------------------------------------------------------------------
int32_t RegexMatcher::start(UErrorCode &err) const {
    return start(0, err);
}




int32_t RegexMatcher::start(int group, UErrorCode &err) const {
    if (U_FAILURE(err)) {
        return -1;
    }
    if (fMatch == FALSE) {
        err = U_REGEX_INVALID_STATE;
        return -1;
    }
    if (group < 0 || group > fPattern->fNumCaptureGroups) {
        err = U_INDEX_OUTOFBOUNDS_ERROR;
        return -1;
    }
    int32_t s;
    if (group == 0) {
        s = fMatchStart; 
    } else {
        s = fCaptureStarts->elementAti(group);
    }
    return s;
}



//--------------------------------------------------------------------------------
//
//   isWordBoundary 
//                     in perl, "xab..cd..", \b is true at positions 0,3,5,7
//                     For us,
//                       If the current char is a combining mark,
//                          \b is FALSE.
//                       Else Scan backwards to the first non-combining char.
//                            We are at a boundary if the this char and the original chars are
//                               opposite in membership in \w set
//
//--------------------------------------------------------------------------------
UBool RegexMatcher::isWordBoundary(int32_t pos) {
    UBool isBoundary = FALSE;
    if (pos >=  fInputLength) {
        // off end of string.  Not a boundary.
        return FALSE;
    }
    
    // Determine whether char c at Pos is a member of the word set of chars.
    UChar32  c = fInput->char32At(pos);
    int8_t ctype = u_charType(c);
    if (ctype==U_NON_SPACING_MARK || ctype==U_ENCLOSING_MARK) {
        // Current char is a combining one.  Not a boundary.
        return FALSE;
    }
    UBool cIsWord = fPattern->fStaticSets[URX_ISWORD_SET]->contains(c);
    
    // Back up until we come to a non-combining char, determine whether
    //  that char is a word char.
    UBool prevCIsWord = FALSE;
    int32_t prevPos = pos;
    for (;;) {
        if (prevPos == 0) {
            break;
        }
        prevPos = fInput->moveIndex32(prevPos, -1);
        UChar32 prevChar = fInput->char32At(prevPos);
        int8_t prevCType = u_charType(prevChar);
        if (!(prevCType==U_NON_SPACING_MARK || prevCType==U_ENCLOSING_MARK)) {
            prevCIsWord = fPattern->fStaticSets[URX_ISWORD_SET]->contains(prevChar);
            break;
        }
    }
    isBoundary = cIsWord ^ prevCIsWord;
    return isBoundary;
}


//--------------------------------------------------------------------------------
//
//     backTrack    Within the match engine, this function is called when
//                  a local match failure occurs, and the match needs to back
//                  track and proceed down another path.
//
//                  Note:  Inline function.  Keep its body above MatchAt().
//
//--------------------------------------------------------------------------------
void RegexMatcher::backTrack(int32_t &inputIdx, int32_t &patIdx)  {
    int32_t *sp = fBackTrackStack->popBlock(fCaptureStateSize);
    int i;
    for (i=fPattern->fNumCaptureGroups; i>=1; i--) {
        fCapStarts[i] = *sp++;
        fCapEnds[i]   = *sp++;
    }
    patIdx   = *sp++;
    inputIdx = *sp++;
}


            
//--------------------------------------------------------------------------------
//
//   MatchAt      This is the actual matching engine.
//
//--------------------------------------------------------------------------------
void RegexMatcher::MatchAt(int32_t startIdx, UErrorCode &status) {
    int32_t     inputIdx = startIdx;   // Current position in the input string.
    int32_t     patIdx   = 0;          // Current position in the compiled pattern.
    UBool       isMatch  = FALSE;      // True if the we have a match.

    int32_t     op;                    // Operation from the compiled pattern, split into
    int32_t     opType;                //    the opcode
    int32_t     opValue;               //    and the operand value.

    #ifdef REGEX_RUN_DEBUG
    {
        printf("MatchAt(startIdx=%d)\n", startIdx);
        printf("Original Pattern: ");
        int i;
        for (i=0; i<fPattern->fPattern.length(); i++) {
            printf("%c", fPattern->fPattern.charAt(i));
        }
        printf("\n");
        printf("Input String: ");
        for (i=0; i<fInput->length(); i++) {
            UChar c = fInput->charAt(i);
            if (c<32 || c>256) {
                c = '.';
            }
            printf("%c", c);
        }
        printf("\n");
        printf("\n");
        printf("PatLoc  inputIdx  char\n");
    }
    #endif

    if (U_FAILURE(status)) {
        return;
    }

    // Clear out capture results from any previous match.
    // Required for capture groups in patterns with | operations that may not match at all,
    //   although the pattern as a whole does match.
    int i;
    for (i=0; i<=fPattern->fNumCaptureGroups; i++) {
        fCaptureStarts->setElementAt(-1, i);
    }

    //  Cache frequently referenced items from the compiled pattern
    //  in local variables.
    //
    int32_t             *pat               = fPattern->fCompiledPat->getBuffer();
                         fCapStarts        = fCaptureStarts->getBuffer();
                         fCapEnds          = fCaptureEnds->getBuffer();
                         fCaptureStateSize  = fPattern->fNumCaptureGroups*2 + 2;   

    const UChar         *litText       = fPattern->fLiteralText.getBuffer();
    UVector             *sets          = fPattern->fSets;
    int32_t              inputLen      = fInput->length();


    //
    //  Main loop for interpreting the compiled pattern.
    //  One iteration of the loop per pattern operation performed.
    //
    for (;;) {
        op      = pat[patIdx];
        opType  = URX_TYPE(op);
        opValue = URX_VAL(op);
        #ifdef REGEX_RUN_DEBUG
            printf("inputIdx=%d   inputChar=%c    ", inputIdx, fInput->char32At(inputIdx));
            fPattern->dumpOp(patIdx);
        #endif
        patIdx++;

        switch (opType) {


        case URX_NOP:
            break;


        case URX_BACKTRACK:
            // Force a backtrack.  In some circumstances, the pattern compiler
            //   will notice that the pattern can't possibly match anything, and will
            //   emit one of these at that point.
            backTrack(inputIdx, patIdx);
            break;


        case URX_ONECHAR:
            if (inputIdx < fInputLength) {
                UChar32   c;
                U16_NEXT(fInputUC, inputIdx, fInputLength, c);
                if (c == opValue) {           
                    break;
                }
            }
            backTrack(inputIdx, patIdx);
            break;


        case URX_STRING:
            {
                // Test input against a literal string.
                // Strings require two slots in the compiled pattern, one for the
                //   offset to the string text, and one for the length.
                int32_t stringStartIdx, stringLen;
                stringStartIdx = opValue;

                op      = pat[patIdx];
                patIdx++;
                opType  = URX_TYPE(op);
                opValue = URX_VAL(op);
                U_ASSERT(opType == URX_STRING_LEN);
                stringLen = opValue;

                int32_t stringEndIndex = inputIdx + stringLen;
                if (stringEndIndex <= inputLen &&
                    u_strncmp(fInputUC+inputIdx, litText+stringStartIdx, stringLen) == 0) {
                    // Success.  Advance the current input position.
                    inputIdx = stringEndIndex;
                } else {
                    // No match.  Back up matching to a saved state
                    backTrack(inputIdx, patIdx);
                }
            }
            break;



        case URX_STATE_SAVE:
            //   Save the state of all capture groups, the pattern continuation
            //   postion and the input position.  
            {
                int32_t   *stackPtr  = fBackTrackStack->reserveBlock(fCaptureStateSize, status);
                int i;
                for (i=fPattern->fNumCaptureGroups; i>0; i--) {
                    *stackPtr++ = fCapStarts[i];
                    *stackPtr++ = fCapEnds[i];
                }
                *stackPtr++ = opValue;
                *stackPtr++ = inputIdx;
            }
            break;


        case URX_END:
            // The match loop will exit via this path on a successful match,
            //   when we reach the end of the pattern.
            isMatch = TRUE;
            goto  breakFromLoop;

        case URX_START_CAPTURE:
            U_ASSERT(opValue > 0 && opValue <= fPattern->fNumCaptureGroups);
            fCapStarts[opValue] = inputIdx;
            break;


        case URX_END_CAPTURE:
            U_ASSERT(opValue > 0 && opValue <= fPattern->fNumCaptureGroups);
            U_ASSERT(fCaptureStarts->elementAti(opValue) >= 0);
            fCapEnds[opValue] = inputIdx;
            break;


        case URX_DOLLAR:                   //  $, test for End of line
                                           //     or for position before new line at end of input
            if (inputIdx < inputLen-2) {
                // We are no where near the end of input.  Fail.
                backTrack(inputIdx, patIdx);
                break;
            }
            if (inputIdx >= inputLen) {
                // We really are at the end of input.  Success.
                break;
            }
            // If we are positioned just before a new-line that is located at the
            //   end of input, succeed.
            if (inputIdx == inputLen-1) {
                UChar32 c = fInput->char32At(inputIdx);
                if (c == 0x0a || c==0x0d || c==0x0c || c==0x85 ||c==0x2028 || c==0x2029) {
                    break;                         // At new-line at end of input. Success
                }
            }

            if (inputIdx == inputLen-2) {
                if (fInput->char32At(inputIdx) == 0x0d && fInput->char32At(inputIdx+1) == 0x0a) {
                    break;                         // At CR/LF at end of input.  Success
                }
            }

            backTrack(inputIdx, patIdx);

            // TODO:  support for multi-line mode.
            break;


        case URX_CARET:                    //  ^, test for start of line
            if (inputIdx != 0) {
                backTrack(inputIdx, patIdx);
            }                              // TODO:  support for multi-line mode.
            break;


        case URX_BACKSLASH_A:          // Test for start of input
            if (inputIdx != 0) {
                backTrack(inputIdx, patIdx);
            }
            break;

        case URX_BACKSLASH_B:          // Test for word boundaries
            {
                UBool success = isWordBoundary(inputIdx);
                success ^= (opValue != 0);     // flip sense for \B
                if (!success) {
                    backTrack(inputIdx, patIdx);
                }
            }
            break;


        case URX_BACKSLASH_D:            // Test for decimal digit
            {
                if (inputIdx >= fInputLength) {
                    backTrack(inputIdx, patIdx);
                    break;
                }

                UChar32 c = fInput->char32At(inputIdx);   
                int8_t ctype = u_charType(c);
                UBool success = (ctype == U_DECIMAL_DIGIT_NUMBER);
                success ^= (opValue != 0);        // flip sense for \D
                if (success) {
                    inputIdx = fInput->moveIndex32(inputIdx, 1);
                } else {
                    backTrack(inputIdx, patIdx);
                }
            }
            break;




        case URX_BACKSLASH_G:          // Test for position at end of previous match
            if (!((fMatch && inputIdx==fMatchEnd) || fMatch==FALSE && inputIdx==0)) {
                backTrack(inputIdx, patIdx);
            }
            break;


        case URX_BACKSLASH_X:          // Match combining character sequence
            {                          //  Closer to Grapheme cluster than to Perl \X
                // Fail if at end of input
                if (inputIdx >= fInputLength) {
                    backTrack(inputIdx, patIdx);
                    break;
                }

                // Always consume one char
                UChar32 c = fInput->char32At(inputIdx);   
                inputIdx = fInput->moveIndex32(inputIdx, 1);

                // Consume CR/LF as a pair
                if (c == 0x0d)  { 
                    UChar32 c = fInput->char32At(inputIdx);   
                    if (c == 0x0a) {
                         inputIdx = fInput->moveIndex32(inputIdx, 1);
                         break;
                    }
                }

                // Consume any combining marks following a non-control char
                int8_t ctype = u_charType(c);
                if (ctype != U_CONTROL_CHAR) {
                    for(;;) {   
                        c = fInput->char32At(inputIdx);   
                        ctype = u_charType(c);
                        // TODO:  make a set and add the "other grapheme extend" chars
                        //        to the list of stuff to be skipped over.
                        if (!(ctype == U_NON_SPACING_MARK || ctype == U_ENCLOSING_MARK)) {
                            break;
                        }
                        inputIdx = fInput->moveIndex32(inputIdx, 1);
                        if (inputIdx >= fInputLength) {
                            break; 
                        }
                    }
                }
            }
            break;



        case URX_BACKSLASH_Z:          // Test for end of line
            if (inputIdx < inputLen) {
                backTrack(inputIdx, patIdx);
            }
            break;



        case URX_STATIC_SETREF:
            {
                // Test input character against one of the predefined sets
                //    (Word Characters, for example)
                // The high bit of the op value is a flag for the match polarity.
                //    0:   success if input char is in set.
                //    1:   success if input char is not in set.
                UBool success = ((opValue & URX_NEG_SET) == URX_NEG_SET);  
                opValue &= ~URX_NEG_SET;
                if (inputIdx < fInputLength) {
                    // There is input left.  Pick up one char and test it for set membership.
                    UChar32  c;
                    U16_NEXT(fInputUC, inputIdx, fInputLength, c);
                    U_ASSERT(opValue > 0 && opValue < URX_LAST_SET);
                    const UnicodeSet *s = fPattern->fStaticSets[opValue];
                    if (s->contains(c)) {
                        success = !success;
                    }
                }
                if (!success) {
                    backTrack(inputIdx, patIdx);
                }
            }
            break;
            

        case URX_SETREF:
            if (inputIdx < fInputLength) {
                // There is input left.  Pick up one char and test it for set membership.
                UChar32   c;
                U16_NEXT(fInputUC, inputIdx, fInputLength, c);
                U_ASSERT(opValue > 0 && opValue < sets->size());
                UnicodeSet *s = (UnicodeSet *)sets->elementAt(opValue);
                if (s->contains(c)) {
                    // The character is in the set.  A Match.
                    break;
                }
            }
            // Either at end of input, or the character wasn't in the set.
            // Either way, we need to back track out.
            backTrack(inputIdx, patIdx);
            break;
            

        case URX_DOTANY:
            {
                // . matches anything
                if (inputIdx >= fInputLength) {
                    // At end of input.  Match failed.  Backtrack out.
                    backTrack(inputIdx, patIdx);
                    break;
                }
                // There is input left.  Advance over one char, unless we've hit end-of-line
                UChar32 c;
                U16_NEXT(fInputUC, inputIdx, fInputLength, c);
                if (((c & 0x7f) <= 0x29) &&     // First quickly bypass as many chars as possible
                    (c == 0x0a || c==0x0d || c==0x0c || c==0x85 ||c==0x2028 || c==0x2029)) {
                    // End of line in normal mode.   . does not match.
                    backTrack(inputIdx, patIdx);
                    break;
                }
            }
            break;
            
            
        case URX_DOTANY_ALL:
            {
                // ., in dot-matches-all (including new lines) mode
                // . matches anything
                if (inputIdx >= fInputLength) {
                    // At end of input.  Match failed.  Backtrack out.
                    backTrack(inputIdx, patIdx);
                    break;
                }
                // There is input left.  Advance over one char, unless we've hit end-of-line
                UChar32 c = fInput->char32At(inputIdx);
                inputIdx = fInput->moveIndex32(inputIdx, 1);
                if (c == 0x0a || c==0x0d || c==0x0c || c==0x85 ||c==0x2028 || c==0x2029) {
                    // In the case of a CR/LF, we need to advance over both.
                    UChar32 nextc = fInput->char32At(inputIdx);
                    if (c == 0x0d && nextc == 0x0a) {
                        inputIdx = fInput->moveIndex32(inputIdx, 1);
                    }
                }
            }
            break;

        case URX_JMP:
            patIdx = opValue;
            break;

        case URX_FAIL:
            isMatch = FALSE;
            goto breakFromLoop;


        default:
            // Trouble.  The compiled pattern contains an entry with an
            //           unrecognized type tag.
            U_ASSERT(FALSE);
        }

        if (U_FAILURE(status)) {
            break;
        }
    }
    
breakFromLoop:
    fMatch = isMatch;
    if (isMatch) {
        fLastMatchEnd = fMatchEnd;
        fMatchStart   = startIdx;
        fMatchEnd     = inputIdx;
        REGEX_RUN_DEBUG_PRINTF("Match.  start=%d   end=%d\n\n", fMatchStart, fMatchEnd);
        }
    else
    {
        REGEX_RUN_DEBUG_PRINTF("No match\n\n");
    }
    return;
}



const char RegexMatcher::fgClassID = 0;

U_NAMESPACE_END

#endif  // !UCONFIG_NO_REGULAR_EXPRESSIONS

