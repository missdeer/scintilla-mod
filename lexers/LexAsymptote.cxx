// This file is part of Notepad2.
// See License.txt for details about distribution and modification.
//! Lexer for Asymptote.

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

struct EscapeSequence {
	int outerState = SCE_ASY_DEFAULT;
	int digitsLeft = 0;
	bool hex = false;

	// highlight any character as escape sequence.
	bool resetEscapeState(int state, int chNext) noexcept {
		if (state == SCE_ASY_STRING_DQ) {
			if (!AnyOf(chNext, '\\', '\"')) {
				return false;
			}
		} else {
			if (IsEOLChar(chNext)) {
				return false;
			}
		}
		outerState = state;
		digitsLeft = 1;
		if (IsOctalDigit(chNext)) {
			digitsLeft = 3;
			hex = false;
		} else if (UnsafeLower(chNext) == 'x') {
			digitsLeft = 3;
			hex = true;
		}
		return true;
	}
	bool atEscapeEnd(int ch) noexcept {
		--digitsLeft;
		return digitsLeft <= 0 || !IsOctalOrHex(ch, hex);
	}
};

enum {
	AsymptoteLineStateMaskLineComment = 1, // line comment
	AsymptoteLineStateMaskImport = 1 << 1, // import
};

//KeywordIndex++Autogenerated -- start of section automatically generated
enum {
	KeywordIndex_Keyword = 0,
	KeywordIndex_Type = 1,
	KeywordIndex_Struct = 2,
	KeywordIndex_Constant = 3,
};
//KeywordIndex--Autogenerated -- end of section automatically generated

enum class KeywordType {
	None = SCE_ASY_DEFAULT,
	Struct = SCE_ASY_STRUCT,
	Return = 0x40,
};

constexpr bool IsSpaceEquiv(int state) noexcept {
	return state <= SCE_ASY_TASKMARKER;
}

void ColouriseAsyDoc(Sci_PositionU startPos, Sci_Position lengthDoc, int initStyle, LexerWordList keywordLists, Accessor &styler) {
	int lineStateLineType = 0;

	KeywordType kwType = KeywordType::None;

	int visibleChars = 0;
	int chBefore = 0;
	int chPrevNonWhite = 0;
	EscapeSequence escSeq;

	StyleContext sc(startPos, lengthDoc, initStyle, styler);

	while (sc.More()) {
		switch (sc.state) {
		case SCE_ASY_OPERATOR:
			sc.SetState(SCE_ASY_DEFAULT);
			break;

		case SCE_ASY_NUMBER:
			if (!IsNumberStart(sc.ch, sc.chNext)) {
				sc.SetState(SCE_ASY_DEFAULT);
			}
			break;

		case SCE_ASY_IDENTIFIER:
			if (!IsIdentifierChar(sc.ch)) {
				char s[128];
				sc.GetCurrent(s, sizeof(s));
				if (keywordLists[KeywordIndex_Keyword].InList(s)) {
					sc.ChangeState(SCE_ASY_WORD);
					if (StrEqualsAny(s, "import", "include")) {
						lineStateLineType = AsymptoteLineStateMaskImport;
					} else if (StrEqualsAny(s, "new", "struct")) {
						kwType = KeywordType::Struct;
					} else if (StrEqual(s, "return")) {
						kwType = KeywordType::Return;
					}
				} else if (keywordLists[KeywordIndex_Type].InList(s)) {
					sc.ChangeState(SCE_ASY_TYPE);
				} else if (kwType == KeywordType::Struct || keywordLists[KeywordIndex_Struct].InList(s)) {
					sc.ChangeState(SCE_ASY_STRUCT);
				} else if (keywordLists[KeywordIndex_Constant].InList(s)) {
					sc.ChangeState(SCE_ASY_CONSTANT);
				} else if (sc.ch != '.') {
					const int chNext = sc.GetDocNextChar();
					if (chNext == '(') {
						// type function()
						// type[] function()
						if (kwType != KeywordType::Return && (IsIdentifierChar(chBefore) || chBefore == ']')) {
							sc.ChangeState(SCE_ASY_FUNCTION_DEFINITION);
						} else {
							sc.ChangeState(SCE_ASY_FUNCTION);
						}
					} else if (sc.Match('[', ']') || IsIdentifierStart(chNext)) {
						// type[]
						// type identifier
						sc.ChangeState(SCE_ASY_STRUCT);
					}
				}
				if (sc.state != SCE_ASY_WORD) {
					kwType = KeywordType::None;
				}
				sc.SetState(SCE_ASY_DEFAULT);
			}
			break;

		case SCE_ASY_COMMENTLINE:
			if (sc.atLineStart) {
				sc.SetState(SCE_ASY_DEFAULT);
			}
			break;

		case SCE_ASY_COMMENTBLOCK:
			if (sc.Match('*', '/')) {
				sc.Forward();
				sc.ForwardSetState(SCE_ASY_DEFAULT);
			}
			break;

		case SCE_ASY_STRING_DQ:
		case SCE_ASY_STRING_SQ:
			if (sc.ch == '\\') {
				if (escSeq.resetEscapeState(sc.state, sc.chNext)) {
					sc.SetState(SCE_ASY_ESCAPECHAR);
					sc.Forward();
				}
			} else if (sc.ch == ((sc.state == SCE_ASY_STRING_DQ) ? '\"' : '\'')) {
				sc.ForwardSetState(SCE_ASY_DEFAULT);
			}
			break;

		case SCE_ASY_ESCAPECHAR:
			if (escSeq.atEscapeEnd(sc.ch)) {
				sc.SetState(escSeq.outerState);
				continue;
			}
			break;
		}

		if (sc.state == SCE_ASY_DEFAULT) {
			if (sc.Match('/', '/')) {
				if (visibleChars == 0) {
					lineStateLineType = AsymptoteLineStateMaskLineComment;
				}
				sc.SetState(SCE_ASY_COMMENTLINE);
			} else if (sc.Match('/', '*')) {
				sc.SetState(SCE_ASY_COMMENTBLOCK);
				sc.Forward();
			} else if (sc.ch == '\"') {
				sc.SetState(SCE_ASY_STRING_DQ);
			} else if (sc.ch == '\'') {
				sc.SetState(SCE_ASY_STRING_SQ);
			} else if (IsADigit(sc.ch)) {
				sc.SetState(SCE_ASY_NUMBER);
			} else if (IsIdentifierStart(sc.ch)) {
				chBefore = chPrevNonWhite;
				sc.SetState(SCE_ASY_IDENTIFIER);
			} else if (IsAGraphic(sc.ch) && !(sc.ch == '\\' || sc.ch == '`')) {
				sc.SetState(SCE_ASY_OPERATOR);
			}
		}

		if (!isspacechar(sc.ch)) {
			visibleChars++;
			if (!IsSpaceEquiv(sc.state)) {
				chPrevNonWhite = sc.ch;
			}
		}
		if (sc.atLineEnd) {
			styler.SetLineState(sc.currentLine, lineStateLineType);
			lineStateLineType = 0;
			visibleChars = 0;
			kwType = KeywordType::None;
		}
		sc.Forward();
	}

	sc.Complete();
}

struct FoldLineState {
	int lineComment;
	int moduleImport;
	constexpr explicit FoldLineState(int lineState) noexcept:
		lineComment(lineState & AsymptoteLineStateMaskLineComment),
		moduleImport((lineState >> 1) & 1) {
	}
};

constexpr bool IsMultilineStringStyle(int style) noexcept {
	return style == SCE_ASY_STRING_SQ
		|| style == SCE_ASY_STRING_DQ
		|| style == SCE_ASY_ESCAPECHAR;
}

void FoldAsyDoc(Sci_PositionU startPos, Sci_Position lengthDoc, int initStyle, LexerWordList, Accessor &styler) {
	const Sci_PositionU endPos = startPos + lengthDoc;
	Sci_Line lineCurrent = styler.GetLine(startPos);
	FoldLineState foldPrev(0);
	int levelCurrent = SC_FOLDLEVELBASE;
	if (lineCurrent > 0) {
		levelCurrent = styler.LevelAt(lineCurrent - 1) >> 16;
		foldPrev = FoldLineState(styler.GetLineState(lineCurrent - 1));
		const Sci_PositionU bracePos = CheckBraceOnNextLine(styler, lineCurrent - 1, SCE_ASY_OPERATOR, SCE_ASY_TASKMARKER);
		if (bracePos) {
			startPos = bracePos + 1; // skip the brace
		}
	}

	int levelNext = levelCurrent;
	FoldLineState foldCurrent(styler.GetLineState(lineCurrent));
	Sci_PositionU lineStartNext = styler.LineStart(lineCurrent + 1);
	lineStartNext = sci::min(lineStartNext, endPos);

	int styleNext = styler.StyleAt(startPos);
	int style = initStyle;
	int visibleChars = 0;

	while (startPos < endPos) {
		const int stylePrev = style;
		style = styleNext;
		styleNext = styler.StyleAt(++startPos);

		switch (style) {
		case SCE_ASY_COMMENTBLOCK:
			if (style != stylePrev) {
				levelNext++;
			} else if (style != styleNext) {
				levelNext--;
			}
			break;

		case SCE_ASY_STRING_SQ:
		case SCE_ASY_STRING_DQ:
			if (!IsMultilineStringStyle(stylePrev)) {
				levelNext++;
			} else if (!IsMultilineStringStyle(styleNext)) {
				levelNext--;
			}
			break;

		case SCE_ASY_OPERATOR: {
			const char ch = styler[startPos - 1];
			if (ch == '{' || ch == '[' || ch == '(') {
				levelNext++;
			} else if (ch == '}' || ch == ']' || ch == ')') {
				levelNext--;
			}
		} break;
		}

		if (visibleChars == 0 && !IsSpaceEquiv(style)) {
			++visibleChars;
		}
		if (startPos == lineStartNext) {
			const FoldLineState foldNext(styler.GetLineState(lineCurrent + 1));
			levelNext = sci::max(levelNext, SC_FOLDLEVELBASE);
			if (foldCurrent.lineComment) {
				levelNext += foldNext.lineComment - foldPrev.lineComment;
			} else if (foldCurrent.moduleImport) {
				levelNext += foldNext.moduleImport - foldPrev.moduleImport;
			} else if (visibleChars) {
				const Sci_PositionU bracePos = CheckBraceOnNextLine(styler, lineCurrent, SCE_ASY_OPERATOR, SCE_ASY_TASKMARKER);
				if (bracePos) {
					levelNext++;
					startPos = bracePos + 1; // skip the brace
					style = SCE_ASY_OPERATOR;
					styleNext = styler.StyleAt(startPos);
				}
			}

			const int levelUse = levelCurrent;
			int lev = levelUse | levelNext << 16;
			if (levelUse < levelNext) {
				lev |= SC_FOLDLEVELHEADERFLAG;
			}
			styler.SetLevel(lineCurrent, lev);

			lineCurrent++;
			lineStartNext = styler.LineStart(lineCurrent + 1);
			lineStartNext = sci::min(lineStartNext, endPos);
			levelCurrent = levelNext;
			foldPrev = foldCurrent;
			foldCurrent = foldNext;
			visibleChars = 0;
		}
	}
}

}

LexerModule lmAsymptote(SCLEX_ASYMPTOTE, ColouriseAsyDoc, "asymptote", FoldAsyDoc);
