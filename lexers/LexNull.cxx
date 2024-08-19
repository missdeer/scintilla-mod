// Scintilla source code edit control
/** @file LexNull.cxx
 ** Lexer for no language. Used for plain text and unrecognized files.
 **/
// Copyright 1998-2001 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#include <cassert>
#include <cstring>

#include <string>
#include <string_view>

#include "ILexer.h"
#include "Scintilla.h"
#include "SciLexer.h"

#include "WordList.h"
#include "LexAccessor.h"
#include "Accessor.h"
#include "CharacterSet.h"
#include "LexerModule.h"

using namespace Lexilla;

namespace {

#define ENABLE_FOLD_NULL_DOCUMENT	1

#if !ENABLE_FOLD_NULL_DOCUMENT
void ColouriseNullDoc(Sci_PositionU startPos, Sci_Position lengthDoc, int /*initStyle*/, LexerWordList /*keywordLists*/, Accessor &styler) {
	// Null language means all style bytes are 0 so just mark the end - no need to fill in.
#if 1
	styler.StartAt(startPos + lengthDoc);
#else
	if (lengthDoc > 0) {
		styler.StartAt(startPos + lengthDoc - 1);
		styler.StartSegment(startPos + lengthDoc - 1);
		styler.ColorTo(startPos + lengthDoc, 0);
	}
#endif
}
#else
// indentation based code folding
void FoldNullDoc(Sci_PositionU startPos, Sci_Position lengthDoc, int /*initStyle*/, LexerWordList /*keywordLists*/, Accessor &styler) {
	const Sci_Position maxPos = startPos + lengthDoc;
	styler.StartAt(maxPos);
	if (!styler.GetPropertyInt("fold")) {
		return;
	}

	const Sci_Line docLines = styler.GetLine(styler.Length());	// Available last line
	const Sci_Line maxLines = (maxPos == styler.Length()) ? docLines : styler.GetLine(maxPos - 1);	// Requested last line

	// Backtrack to previous non-blank line so we can determine indent level
	// for any white space lines
	// and so we can fix any preceding fold level (which is why we go back
	// at least one line in all cases)
	Sci_Line lineCurrent = styler.GetLine(startPos);
	int indentCurrent = styler.IndentAmount(lineCurrent);
	while (lineCurrent > 0) {
		lineCurrent--;
		indentCurrent = styler.IndentAmount(lineCurrent);
		if (!(indentCurrent & SC_FOLDLEVELWHITEFLAG)){
			break;
		}
	}

	// Process all characters to end of requested range
	// Cap processing in all cases
	// to end of document (in case of unclosed quote at end).
	while (lineCurrent <= maxLines) {
		// Gather info
		Sci_Line lineNext = lineCurrent + 1;
		int indentNext = indentCurrent;
		if (lineNext <= docLines) {
			// Information about next line is only available if not at end of document
			indentNext = styler.IndentAmount(lineNext);
		}

		// Skip past any blank lines for next indent level info
		while ((lineNext < docLines) && (indentNext & SC_FOLDLEVELWHITEFLAG)) {
			lineNext++;
			indentNext = styler.IndentAmount(lineNext);
		}

		// Set fold header
		int lev = indentCurrent;
		if (!(indentCurrent & SC_FOLDLEVELWHITEFLAG)) {
			if ((indentCurrent & SC_FOLDLEVELNUMBERMASK) < (indentNext & SC_FOLDLEVELNUMBERMASK)) {
				lev |= SC_FOLDLEVELHEADERFLAG;
			}
		}

		// Set fold level for this line and move to next line
		styler.SetLevel(lineCurrent, lev & ~SC_FOLDLEVELWHITEFLAG);
		lineCurrent++;
		indentCurrent = indentNext;

		const int levelAfterBlank = indentNext & SC_FOLDLEVELNUMBERMASK;
		const int skipLevel = levelAfterBlank;

		// Now set all the indent levels on the lines we skipped
		// [ignore following comment: only blank block is skipped]
		// Do this from end to start. Once we encounter one line
		// which is indented more than the line after the end of
		// the blank-block, use the level of the block before

		for (; lineCurrent < lineNext; lineCurrent++) {
			styler.SetLevel(lineCurrent, skipLevel);
		}
	}

	// NOTE: Cannot set level of last line here because indentCurrent doesn't have
	// header flag set; the loop above is crafted to take care of this case!
	//styler.SetLevel(lineCurrent, indentCurrent);
}
#endif
}

#if !ENABLE_FOLD_NULL_DOCUMENT
extern const LexerModule lmNull(SCLEX_NULL, ColouriseNullDoc, "null");
#else
extern const LexerModule lmNull(SCLEX_NULL, FoldNullDoc, "null");
#endif
