// This file is part of Notepad2.
// See License.txt for details about distribution and modification.
//! Lexer for Makefile of gmake, nmake, bmake, qmake

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
#include "StyleContext.h"
#include "CharacterSet.h"
#include "StringUtils.h"
#include "LexerModule.h"

using namespace Lexilla;

namespace {

#define MAKE_TYPE_GMAKE 0
#define MAKE_TYPE_NMAKE 1
#define MAKE_TYPE_BMAKE 2
#define MAKE_TYPE_QMAKE 3
#define MAKE_TYPE_NINJA 4
constexpr bool IsMakeOp(int ch, int chNext) noexcept {
	return ch == '=' || ch == ':' || ch == '{' || ch == '}' || ch == '(' || ch == ')' || ch == ','
		|| ch == '$' || ch == '@' || ch == '%' || ch == '<' || ch == '?' || ch == '^'
		|| ch == '|' || ch == '*' || ch == '>' || ch == ';' || ch == '&' || ch == '!'
		|| (chNext == '=' && (ch == '+' || ch == '-' || ch == ':'));
}

#define MAX_WORD_LENGTH	15
void ColouriseMakeDoc(Sci_PositionU startPos, Sci_Position length, int initStyle, LexerWordList keywordLists, Accessor &styler) {
	const WordList &keywordsGP = keywordLists[0];		// gnu make Preprocessor
	const WordList &keywordsDP2 = keywordLists[6];		// bmake
	const WordList &keywordsNinja = keywordLists[7];	// ninja

	int state = initStyle;
	int ch = 0;
	int chNext = styler[startPos];
	styler.StartAt(startPos);
	styler.StartSegment(startPos);
	const Sci_PositionU endPos = startPos + length;

	int visibleChars = 0;
	Sci_Line lineCurrent = styler.GetLine(startPos);
	char buf[MAX_WORD_LENGTH + 1] = "";
	int wordLen = 0;
	int varCount = 0;
	static int makeType = MAKE_TYPE_GMAKE;

	for (Sci_PositionU i = startPos; i < endPos; i++) {
		const int chPrev = ch;
		ch = chNext;
		chNext = styler.SafeGetCharAt(i + 1);

		const bool atEOL = (ch == '\r' && chNext != '\n') || (ch == '\n');
		const bool atLineStart = i == static_cast<Sci_PositionU>(styler.LineStart(lineCurrent));

		switch (state) {
		case SCE_MAKE_OPERATOR:
			styler.ColorTo(i, state);
			state = SCE_MAKE_DEFAULT;
			break;
		case SCE_MAKE_IDENTIFIER:
			if (IsMakeOp(ch, chNext) || IsASpace(ch)) {
				buf[wordLen] = '\0';
				if (ch == ':' && chNext == ':') {
					styler.ColorTo(i, SCE_MAKE_TARGET);
				} else if (makeType == MAKE_TYPE_BMAKE && keywordsDP2.InList(buf)) {
					styler.ColorTo(i, SCE_MAKE_PREPROCESSOR);
				}
				state = SCE_MAKE_DEFAULT;
			} else if (wordLen < MAX_WORD_LENGTH) {
				buf[wordLen++] = static_cast<char>(ch);
			}
			break;
		case SCE_MAKE_TARGET:
			if (IsMakeOp(ch, chNext) || IsASpace(ch)) {
				buf[wordLen] = '\0';
				if (keywordsGP.InList(buf)) { // gmake
					styler.ColorTo(i, SCE_MAKE_PREPROCESSOR);
					makeType = MAKE_TYPE_GMAKE;
				} else if (keywordsNinja.InList(buf)) { // ninja
					styler.ColorTo(i, SCE_MAKE_PREPROCESSOR);
					makeType = MAKE_TYPE_NINJA;
				} else {
					Sci_Position pos = i;
					while (IsASpace(styler.SafeGetCharAt(pos++)));
					if (styler[pos - 1] == '=' || styler[pos] == '=') {
						styler.ColorTo(i, SCE_MAKE_VARIABLE);
					} else if (styler[pos - 1] == ':') {
						styler.ColorTo(i, SCE_MAKE_TARGET);
					} else if (buf[0] == '.' && IsASpace(ch)) { // bmake
						styler.ColorTo(i, SCE_MAKE_PREPROCESSOR);
						makeType = MAKE_TYPE_BMAKE;
					} else {
						styler.ColorTo(i, SCE_MAKE_DEFAULT);
					}
				}
				state = SCE_MAKE_DEFAULT;
			} else if (wordLen < MAX_WORD_LENGTH) {
				buf[wordLen++] = static_cast<char>(ch);
			}
			break;
		case SCE_MAKE_VARIABLE:
			if (!(ch == '$' || iswordchar(ch))) {
				styler.ColorTo(i, state);
				state = SCE_MAKE_DEFAULT;
			}
			break;
		case SCE_MAKE_VARIABLE2:
			if (ch == '$' && chNext == '(') {
				varCount++;
			} else if (ch == ')') {
				varCount--;
				if (varCount <= 0) {
					styler.ColorTo(i + 1, state);
					state = SCE_MAKE_DEFAULT;
					continue;
				}
			}
			break;
		case SCE_MAKE_VARIABLE3:
			if (chPrev == '}') {
				styler.ColorTo(i, state);
				state = SCE_MAKE_DEFAULT;
			}
			break;
		case SCE_MAKE_PREPROCESSOR:
			if (!iswordchar(ch)) {
				styler.ColorTo(i, state);
				state = SCE_MAKE_DEFAULT;
			}
			break;
		case SCE_MAKE_COMMENT:
			if (atLineStart) {
				styler.ColorTo(i, state);
				state = SCE_MAKE_DEFAULT;
			}
			break;
		}

		if (state != SCE_MAKE_COMMENT && ch == '\\' && IsEOLChar(chNext)) {
			i++;
			lineCurrent++;
			ch = chNext;
			chNext = styler.SafeGetCharAt(i + 1);
			if (ch == '\r' && chNext == '\n') {
				i++;
				ch = chNext;
				chNext = styler.SafeGetCharAt(i + 1);
			}
			continue;
		}

		if (state == SCE_MAKE_DEFAULT) {
			if (ch == '#') {
				styler.ColorTo(i, state);
				state = SCE_MAKE_COMMENT;
			} else if ((ch == '$' && chNext == '(') || (ch == '$' && chNext == '$' && styler.SafeGetCharAt(i + 2) == '(')) {
				styler.ColorTo(i, state);
				Sci_PositionU pos = i + 1;
				if (chNext == '$') ++pos;
				ch = chNext;
				while (pos < endPos && ch != ')') {
					chNext = styler.SafeGetCharAt(pos + 1);
					if (ch == '$' && chNext == '(')
						break;
					if (IsASpace(ch) || ch == ',')
						break;
					++pos;
					ch = chNext;
				}
				if (ch == ')' || ch == '$') {
					styler.ColorTo(pos + 1, SCE_MAKE_VARIABLE2);
					if (ch == '$') {
						state = SCE_MAKE_VARIABLE2;
						varCount = 2;
					} else if (atLineStart) {
						state = SCE_MAKE_TARGET;
					}
				} else {
					styler.ColorTo(i + 2, SCE_MAKE_OPERATOR);
					styler.ColorTo(pos, SCE_MAKE_FUNCTION);
					if (ch == ',')
						styler.ColorTo(pos + 1, SCE_MAKE_OPERATOR);
				}
				i = pos;
				ch = chNext;
				chNext = styler.SafeGetCharAt(i + 1);
			} else if (ch == '$' && chNext == '{') { // bmake
				styler.ColorTo(i, state);
				state = SCE_MAKE_VARIABLE3;
			} else if (ch == '$' && (chNext == '$' || iswordstart(chNext))) {
				styler.ColorTo(i, state);
				state = SCE_MAKE_VARIABLE;
			} else if (visibleChars == 0 && ch == '!' && iswordstart(chNext)) {
				styler.ColorTo(i, state);
				state = SCE_MAKE_PREPROCESSOR;
				makeType = MAKE_TYPE_NMAKE;
			} else if (IsMakeOp(ch, chNext) || (visibleChars == 0 && ch == '-')) {
				styler.ColorTo(i, state);
				state = SCE_MAKE_OPERATOR;
			} else if (IsGraphic(ch)) {
				styler.ColorTo(i, state);
				buf[0] = static_cast<char>(ch);
				wordLen = 1;
				state = (visibleChars == 0) ? SCE_MAKE_TARGET : SCE_MAKE_IDENTIFIER;
			}
		}

		if (atEOL || i == endPos - 1) {
			lineCurrent++;
			visibleChars = 0;
		}
		if (!isspacechar(ch) && !(visibleChars == 0 && ch == '-')) {
			visibleChars++;
		}
	}

	// Colourise remaining document
	styler.ColorTo(endPos, state);
}

#define IsCommentLine(line)	IsLexCommentLine(styler, line, SCE_MAKE_COMMENT)

void FoldMakeDoc(Sci_PositionU startPos, Sci_Position length, int initStyle, LexerWordList /*keywordLists*/, Accessor &styler) {
	const Sci_PositionU endPos = startPos + length;
	int visibleChars = 0;
	Sci_Line lineCurrent = styler.GetLine(startPos);
	int levelCurrent = SC_FOLDLEVELBASE;
	if (lineCurrent > 0)
		levelCurrent = styler.LevelAt(lineCurrent - 1) >> 16;
	int levelNext = levelCurrent;

	char chNext = styler[startPos];
	int styleNext = styler.StyleAt(startPos);
	int style = initStyle;

	for (Sci_PositionU i = startPos; i < endPos; i++) {
		const char ch = chNext;
		chNext = styler.SafeGetCharAt(i + 1);
		const int stylePrev = style;
		style = styleNext;
		styleNext = styler.StyleAt(i + 1);
		const bool atEOL = (ch == '\r' && chNext != '\n') || (ch == '\n');

		if (atEOL) {
		 	if (IsCommentLine(lineCurrent)) {
				levelNext += IsCommentLine(lineCurrent + 1) - IsCommentLine(lineCurrent - 1);
			} else {
				levelNext += IsBackslashLine(styler, lineCurrent) - IsBackslashLine(styler, lineCurrent - 1);
			}
		}

		if (visibleChars == 0 && (ch == '!' || ch == 'i' || ch == 'e' || ch == 'd' || ch == '.')
			&& style == SCE_MAKE_PREPROCESSOR && stylePrev != SCE_MAKE_PREPROCESSOR) {
			char buf[MAX_WORD_LENGTH + 1];
			Sci_Position j = i;
			if (ch == '!' || ch == '.')
				j++;
			LexGetRangeLowered(styler, j, iswordchar, buf, MAX_WORD_LENGTH);
			if (StrStartsWith(buf, "if") || StrEqualsAny(buf, "define", "for"))
				levelNext++;
			else if (StrEqualsAny(buf, "endif", "endef", "endfor"))
				levelNext--;
		}

		if (style == SCE_MAKE_OPERATOR) { // qmake
			if (/*ch == '(' || */ch == '{')
				levelNext++;
			else if (/*ch == ')' || */ch == '}')
				levelNext--;
		}

		if (visibleChars == 0 && !isspacechar(ch))
			visibleChars++;

		if (atEOL || (i == endPos - 1)) {
			levelNext = sci::max(levelNext, SC_FOLDLEVELBASE);
			const int levelUse = levelCurrent;
			int lev = levelUse | levelNext << 16;
			if (levelUse < levelNext)
				lev |= SC_FOLDLEVELHEADERFLAG;
			styler.SetLevel(lineCurrent, lev);
			lineCurrent++;
			levelCurrent = levelNext;
			visibleChars = 0;
		}
	}
}

}

LexerModule lmMakefile(SCLEX_MAKEFILE, ColouriseMakeDoc, "makefile", FoldMakeDoc);
