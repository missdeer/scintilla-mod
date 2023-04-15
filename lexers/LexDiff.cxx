// Scintilla source code edit control
/** @file LexDiff.cxx
 ** Lexer for diff results.
 **/
// Copyright 1998-2001 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#include <cstdlib>
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
#include "StringUtils.h"
#include "LexerModule.h"

using namespace Lexilla;

namespace {

#define DIFF_BUFFER_START_SIZE 16
// Note that ColouriseDiffLine analyzes only the first DIFF_BUFFER_START_SIZE
// characters of each line to classify the line.

inline int ColouriseDiffLine(const char (&lineBuffer)[DIFF_BUFFER_START_SIZE]) noexcept {
	// It is needed to remember the current state to recognize starting
	// comment lines before the first "diff " or "--- ". If a real
	// difference starts then each line starting with ' ' is a whitespace
	// otherwise it is considered a comment (Only in..., Binary file...)
	if (StrStartsWith(lineBuffer, "diff ")) {
		return SCE_DIFF_COMMAND;
	}
	if (StrStartsWith(lineBuffer, "Index: ")) { // For subversion's diff
		return SCE_DIFF_COMMAND;
	}
	if (StrStartsWith(lineBuffer, "---") && lineBuffer[CStrLen("---")] != '-') {
		// In a context diff, --- appears in both the header and the position markers
		if (lineBuffer[CStrLen("---")] == ' ' && atoi(lineBuffer + CStrLen("--- ")) && !strchr(lineBuffer, '/')) {
			return SCE_DIFF_POSITION;
		}
		if (IsEOLChar(lineBuffer[CStrLen("---")])) {
			return SCE_DIFF_POSITION;
		}
		if (lineBuffer[CStrLen("---")] == ' ') {
			return SCE_DIFF_HEADER;
		}
		return SCE_DIFF_DELETED;
	}
	if (StrStartsWith(lineBuffer, "+++ ")) {
		// I don't know of any diff where "+++ " is a position marker, but for
		// consistency, do the same as with "--- " and "*** ".
		if (atoi(lineBuffer + CStrLen("+++ ")) && !strchr(lineBuffer, '/')) {
			return SCE_DIFF_POSITION;
		}
		return SCE_DIFF_HEADER;
	}
	if (StrStartsWith(lineBuffer, "====")) { // For p4's diff
		return SCE_DIFF_HEADER;
	}
	if (StrStartsWith(lineBuffer, "***")) {
		// In a context diff, *** appears in both the header and the position markers.
		// Also ******** is a chunk header, but here it's treated as part of the
		// position marker since there is no separate style for a chunk header.
		if (lineBuffer[CStrLen("***")] == ' ' && atoi(lineBuffer + CStrLen("*** ")) && !strchr(lineBuffer, '/')) {
			return SCE_DIFF_POSITION;
		}
		if (lineBuffer[CStrLen("***")] == '*') {
			return SCE_DIFF_POSITION;
		}
		return SCE_DIFF_HEADER;
	}
	if (StrStartsWith(lineBuffer, "? ")) {    // For difflib
		return SCE_DIFF_HEADER;
	}
	if (lineBuffer[0] == '@') {
		return SCE_DIFF_POSITION;
	}
	if (IsADigit(lineBuffer[0])) {
		return SCE_DIFF_POSITION;
	}
	if (StrStartsWith(lineBuffer, "++")) {
		return SCE_DIFF_PATCH_ADD;
	}
	if (StrStartsWith(lineBuffer, "+-")) {
		return SCE_DIFF_PATCH_DELETE;
	}
	if (StrStartsWith(lineBuffer, "-+")) {
		return SCE_DIFF_REMOVED_PATCH_ADD;
	}
	if (StrStartsWith(lineBuffer, "--")) {
		return SCE_DIFF_REMOVED_PATCH_DELETE;
	}
	if (lineBuffer[0] == '-' || lineBuffer[0] == '<') {
		return SCE_DIFF_DELETED;
	}
	if (lineBuffer[0] == '+' || lineBuffer[0] == '>') {
		return SCE_DIFF_ADDED;
	}
	if (lineBuffer[0] == '!') {
		return SCE_DIFF_CHANGED;
	}
	if (lineBuffer[0] != ' ') {
		return SCE_DIFF_COMMENT;
	}
	return SCE_DIFF_DEFAULT;
}

void ColouriseDiffDoc(Sci_PositionU startPos, Sci_Position lengthDoc, int initStyle, LexerWordList, Accessor &styler) {
	const bool fold = styler.GetPropertyBool("fold");

	styler.StartAt(startPos);
	styler.StartSegment(startPos);
	const Sci_Position endPos = startPos + lengthDoc;
	const Sci_Line maxLines = styler.GetLine((endPos == styler.Length()) ? endPos : endPos - 1);

	Sci_Line lineCurrent = styler.GetLine(startPos);
	int prevLevel = (lineCurrent > 0) ? styler.LevelAt(lineCurrent - 1) : SC_FOLDLEVELBASE;

	Sci_PositionU lineStartCurrent = styler.LineStart(lineCurrent);
	Sci_PositionU lineStartNext = styler.LineStart(lineCurrent + 1);

	while (lineCurrent <= maxLines) {
		char lineBuffer[DIFF_BUFFER_START_SIZE];
		styler.GetRange(lineStartCurrent, lineStartNext, lineBuffer, sizeof(lineBuffer));
		const int lineType = ColouriseDiffLine(lineBuffer);
		if (initStyle != lineType) {
			styler.ColorTo(lineStartCurrent, initStyle);
			initStyle = lineType;
		}

		if (fold) {
			int nextLevel;
			if (lineType == SCE_DIFF_COMMAND) {
				nextLevel = SC_FOLDLEVELBASE | SC_FOLDLEVELHEADERFLAG;
			} else if (lineType == SCE_DIFF_HEADER) {
				nextLevel = (SC_FOLDLEVELBASE + 1) | SC_FOLDLEVELHEADERFLAG;
			} else if (lineType == SCE_DIFF_POSITION && lineBuffer[0] != '-') {
				nextLevel = (SC_FOLDLEVELBASE + 2) | SC_FOLDLEVELHEADERFLAG;
			} else if (prevLevel & SC_FOLDLEVELHEADERFLAG) {
				nextLevel = (prevLevel & SC_FOLDLEVELNUMBERMASK) + 1;
			} else {
				nextLevel = prevLevel;
			}

			if ((nextLevel & SC_FOLDLEVELHEADERFLAG) && (nextLevel == prevLevel)) {
				styler.SetLevel(lineCurrent - 1, prevLevel & ~SC_FOLDLEVELHEADERFLAG);
			}

			styler.SetLevel(lineCurrent, nextLevel);
			prevLevel = nextLevel;
		}

		lineStartCurrent = lineStartNext;
		lineCurrent++;
		lineStartNext = styler.LineStart(lineCurrent + 1);
	}

	styler.ColorTo(endPos, initStyle);
}

}

LexerModule lmDiff(SCLEX_DIFF, ColouriseDiffDoc, "diff");
