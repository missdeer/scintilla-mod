// This file is part of Notepad2.
// See License.txt for details about distribution and modification.
//! Lexer for Rust.

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

// https://doc.rust-lang.org/reference/tokens.html#string-literals
struct EscapeSequence {
	int outerState = SCE_RUST_DEFAULT;
	int digitsLeft = 0;
	bool brace = false;

	// highlight any character as escape sequence.
	void resetEscapeState(int state, int chNext) noexcept {
		outerState = state;
		brace = false;
		digitsLeft = (chNext == 'x') ? 3 : 1;
	}
	bool atEscapeEnd(int ch) noexcept {
		--digitsLeft;
		return digitsLeft <= 0 || !IsHexDigit(ch);
	}
};

bool IsRustRawString(LexAccessor &styler, Sci_PositionU pos, bool start, int &hashCount) noexcept {
	int count = 0;
	char ch;
	while ((ch = styler[pos]) == '#') {
		++count;
		++pos;
	}

	if (start) {
		if (ch == '\"') {
			hashCount = count;
			return true;
		}
	} else {
		return count == hashCount;
	}
	return false;
}

enum {
	RustLineStateMaskLineComment = (1 << 0),	// line comment
	RustLineStateMaskPubUse = (1 << 1),			// [pub] use
	RustLineStateMaskAttribute = (1 << 2),		// attribute block
	MaxRustCharLiteralLength = 2 + 2 + 2 + 6,	// '\u{10FFFF}'
};

//KeywordIndex++Autogenerated -- start of section automatically generated
enum {
	KeywordIndex_Keyword = 0,
	KeywordIndex_ReservedKeyword = 1,
	KeywordIndex_PrimitiveType = 2,
	KeywordIndex_Struct = 3,
	KeywordIndex_Trait = 4,
	KeywordIndex_Enumeration = 5,
	KeywordIndex_Union = 6,
	KeywordIndex_Constant = 7,
};
//KeywordIndex--Autogenerated -- end of section automatically generated

enum KeywordType {
	None = SCE_RUST_DEFAULT,
	Struct = SCE_RUST_STRUCT,
	Trait = SCE_RUST_TRAIT,
	Enum = SCE_RUST_ENUMERATION,
	Type = SCE_RUST_TYPE,
	Union = SCE_RUST_UNION,
	Constant = SCE_RUST_CONSTANT,
	Function = SCE_RUST_FUNCTION_DEFINITION,
};

constexpr bool IsSpaceEquiv(int state) noexcept {
	return state <= SCE_RUST_TASKMARKER;
}

// https://doc.rust-lang.org/std/fmt/#syntax
Sci_Position CheckFormatSpecifier(const StyleContext &sc, LexAccessor &styler) noexcept {
	Sci_PositionU pos = sc.currentPos + 1; // ':'
	char ch = static_cast<char>(sc.chNext);
	// [[fill] align]
	if (!AnyOf(ch, '\r', '\n', '{', '}')) {
		Sci_Position width = 1;
		if (ch & 0x80) {
			styler.GetCharacterAndWidth(pos, &width);
		}
		const char chNext = styler[pos + width];
		if (AnyOf(ch, '<', '^', '>') || AnyOf(chNext, '<', '^', '>')) {
			pos += 1 + width;
			ch = styler[pos];
		}
	}
	// [sign]['#']['0']
	if (ch == '+' || ch == '-') {
		ch = styler[++pos];
	}
	if (ch == '#') {
		ch = styler[++pos];
	}
	if (ch == '0') {
		ch = styler[++pos];
	}
	// [width]['.' precision]type
	for (int i = 0; i < 3; i++) {
		if (i < 2 && ch == '.') {
			i = 1;
			ch = styler[++pos];
			if (ch == '*') {
				i = 2;
				ch = styler[++pos];
			}
		}
		while (IsIdentifierCharEx(static_cast<uint8_t>(ch))) {
			ch = styler[++pos];
		}
		if (i < 2 && ch == '$') {
			ch = styler[++pos];
		}
		if (ch == '?') {
			ch = styler[++pos];
			break;
		}
	}
	if (ch == '}') {
		return pos - sc.currentPos;
	}
	return 0;
}

void ColouriseRustDoc(Sci_PositionU startPos, Sci_Position lengthDoc, int initStyle, LexerWordList keywordLists, Accessor &styler) {
	int lineStateAttribute = 0;
	int lineStateLineType = 0;

	int squareBracket = 0;	// count of '[' and ']' for attribute
	int commentLevel = 0;	// nested block comment level
	int hashCount = 0;		// count of '#' for raw (byte) string
	KeywordType kwType = KeywordType::None;

	int chBeforeIdentifier = 0;
	int visibleChars = 0;
	int visibleCharsBefore = 0;
	Sci_PositionU charStartPos = 0;
	EscapeSequence escSeq;

	StyleContext sc(startPos, lengthDoc, initStyle, styler);
	if (sc.currentLine > 0) {
		const int lineState = styler.GetLineState(sc.currentLine - 1);
		/*
		2: lineStateLineType
		1: lineStateAttribute
		8: squareBracket
		8: commentLevel
		8: hashCount
		*/
		squareBracket = (lineState >> 3) & 0xff;
		commentLevel = (lineState >> 11) & 0xff;
		hashCount = (lineState >> 19) & 0xff;
		lineStateAttribute = lineState & RustLineStateMaskAttribute;
	} else if (startPos == 0 && sc.Match('#', '!')) {
		// Shell Shebang at beginning of file
		sc.SetState(SCE_RUST_COMMENTLINE);
		sc.Forward();
		lineStateLineType = RustLineStateMaskLineComment;
	}

	while (sc.More()) {
		switch (sc.state) {
		case SCE_RUST_OPERATOR:
		case SCE_RUST_ATTRIBUTE:
			sc.SetState(SCE_RUST_DEFAULT);
			break;

		case SCE_RUST_NUMBER:
			if (!IsDecimalNumber(sc.chPrev, sc.ch, sc.chNext)) {
				sc.SetState(SCE_RUST_DEFAULT);
			}
			break;

		case SCE_RUST_IDENTIFIER:
		case SCE_RUST_VARIABLE:
		case SCE_RUST_LIFETIME:
			if (!IsIdentifierCharEx(sc.ch)) {
				if (sc.state == SCE_RUST_IDENTIFIER) {
					if (lineStateAttribute) {
						sc.ChangeState(SCE_RUST_ATTRIBUTE);
					} else if (sc.ch == '!') {
						sc.ChangeState(SCE_RUST_MACRO);
						sc.Forward();
					} else {
						char s[128];
						sc.GetCurrent(s, sizeof(s));
						if (keywordLists[KeywordIndex_Keyword].InList(s)) {
							sc.ChangeState(SCE_RUST_WORD);
							if (StrEqual(s, "struct")) {
								kwType = KeywordType::Struct;
							} else if (StrEqual(s, "fn")) {
								kwType = KeywordType::Function;
							} else if (StrEqual(s, "trait")) {
								kwType = KeywordType::Trait;
							} else if (StrEqual(s, "enum")) {
								kwType = KeywordType::Enum;
							} else if (StrEqual(s, "type")) {
								kwType = KeywordType::Type; // type alias
							} else if (StrEqual(s, "const")) {
								kwType = KeywordType::Constant;
							} else if (StrEqual(s, "union")) {
								kwType = KeywordType::Union;
							}
							if (kwType != KeywordType::None) {
								const int chNext = sc.GetDocNextChar();
								if (!IsIdentifierStartEx(chNext)) {
									kwType = KeywordType::None;
								}
							}
							if ((visibleChars == 3 || visibleChars == 6) && StrEqual(s, "use")) {
								lineStateLineType = RustLineStateMaskPubUse;
							}
						} else if (keywordLists[KeywordIndex_ReservedKeyword].InList(s)) {
							sc.ChangeState(SCE_RUST_WORD2);
						} else if (keywordLists[KeywordIndex_PrimitiveType].InList(s)) {
							sc.ChangeState(SCE_RUST_TYPE);
						} else if (keywordLists[KeywordIndex_Struct].InList(s)) {
							sc.ChangeState(SCE_RUST_STRUCT);
						} else if (keywordLists[KeywordIndex_Trait].InList(s)) {
							sc.ChangeState(SCE_RUST_TRAIT);
						} else if (keywordLists[KeywordIndex_Enumeration].InList(s)) {
							sc.ChangeState(SCE_RUST_ENUMERATION);
						} else if (keywordLists[KeywordIndex_Union].InList(s)) {
							sc.ChangeState(SCE_RUST_UNION);
						} else if (keywordLists[KeywordIndex_Constant].InList(s)) {
							sc.ChangeState(SCE_RUST_CONSTANT);
						} else if (sc.ch != '.') {
							const int chNext = sc.GetDocNextChar();
							if (chNext == '(') {
								sc.ChangeState((kwType == KeywordType::Function)? static_cast<int>(kwType) : SCE_RUST_FUNCTION);
							} else if (chNext == '!') {
								sc.ChangeState(SCE_RUST_MACRO);
							} else if (kwType != KeywordType::None) {
								if (kwType != KeywordType::Constant || chNext == ':') {
									sc.ChangeState(static_cast<int>(kwType));
								} else if (chBeforeIdentifier == '[' && sc.ch == ';') {
									// array: [T; N]
									sc.ChangeState(SCE_RUST_TYPE);
								}
							}
						}
					}
					if (sc.state != SCE_RUST_WORD && sc.ch != '.') {
						kwType = KeywordType::None;
					}
				}
				sc.SetState(SCE_RUST_DEFAULT);
			}
			break;

		case SCE_RUST_COMMENTLINE:
		case SCE_RUST_COMMENTLINEDOC:
			if (sc.atLineStart) {
				sc.SetState(SCE_RUST_DEFAULT);
			} else {
				HighlightTaskMarker(sc, visibleChars, visibleCharsBefore, SCE_RUST_TASKMARKER);
			}
			break;

		case SCE_RUST_COMMENTBLOCK:
		case SCE_RUST_COMMENTBLOCKDOC:
			if (sc.Match('*', '/')) {
				sc.Forward();
				--commentLevel;
				if (commentLevel == 0) {
					sc.ForwardSetState(SCE_RUST_DEFAULT);
				}
			} else if (sc.Match('/', '*')) {
				// TODO: nested block comment
				//const int chNext = sc.GetRelative(2);
				//if (chNext == '!' || (chNext == '*' && sc.GetRelative(3) != '*')) {
				//	sc.SetState(SCE_RUST_COMMENTBLOCKDOC);
				//} else {
				//	sc.SetState(SCE_RUST_COMMENTBLOCK);
				//}
				sc.Forward();
				++commentLevel;
			} else if (HighlightTaskMarker(sc, visibleChars, visibleCharsBefore, SCE_RUST_TASKMARKER)) {
				continue;
			}
			break;

		case SCE_RUST_STRING:
		case SCE_RUST_BYTESTRING:
		case SCE_RUST_RAW_STRING:
		case SCE_RUST_RAW_BYTESTRING:
			switch (sc.ch) {
			case '\\':
				if (sc.state < SCE_RUST_RAW_STRING) {
					const int state = sc.state;
					if (IsEOLChar(sc.chNext)) {
						sc.SetState(SCE_RUST_LINE_CONTINUATION);
						sc.ForwardSetState(state);
					} else {
						escSeq.resetEscapeState(state, sc.chNext);
						sc.SetState(SCE_RUST_ESCAPECHAR);
						sc.Forward();
						if (state == SCE_RUST_STRING && sc.Match('u', '{')) {
							escSeq.brace = true;
							escSeq.digitsLeft = 7; // 24-bit code point escape
							sc.Forward();
						}
					}
				}
				break;

			case '\"':
				if (hashCount == 0 || (sc.chNext == '#' && IsRustRawString(styler, sc.currentPos + 1, false, hashCount))) {
					sc.Advance(hashCount);
					hashCount = 0;
					sc.ForwardSetState(SCE_RUST_DEFAULT);
				}
				break;

			case '{':
			case '}':
				if (sc.ch == sc.chNext) {
					escSeq.outerState = sc.state;
					escSeq.digitsLeft = 1;
					sc.SetState(SCE_RUST_ESCAPECHAR);
					sc.Forward();
				} else if (sc.ch == '{' && (sc.chNext == '}' || sc.chNext == ':' || IsIdentifierCharEx(sc.chNext))) {
					escSeq.outerState = sc.state;
					sc.SetState(SCE_RUST_PLACEHOLDER);
				}
				break;
			}
			break;

		case SCE_RUST_PLACEHOLDER:
			if (!IsIdentifierCharEx(sc.ch)) {
				if (sc.ch == ':') {
					const Sci_Position length = CheckFormatSpecifier(sc, styler);
					if (length != 0) {
						sc.SetState(SCE_RUST_FORMAT_SPECIFIER);
						sc.Advance(length);
						sc.SetState(SCE_RUST_PLACEHOLDER);
						sc.ForwardSetState(escSeq.outerState);
						continue;
					}
				}
				if (sc.ch != '}') {
					sc.Rewind();
					sc.ChangeState(escSeq.outerState);
				}
				sc.ForwardSetState(escSeq.outerState);
				continue;
			}
			break;

		case SCE_RUST_CHARACTER:
		case SCE_RUST_BYTE_CHARACTER:
			if (sc.ch == '\\') {
				if (!IsEOLChar(sc.chNext)) {
					escSeq.resetEscapeState(sc.state, sc.chNext);
					sc.SetState(SCE_RUST_ESCAPECHAR);
					sc.Forward();
					if (escSeq.outerState == SCE_RUST_CHARACTER && sc.Match('u', '{')) {
						escSeq.brace = true;
						escSeq.digitsLeft = 7; // 24-bit code point escape
						sc.Forward();
					}
				}
			} else if (sc.ch == '\'') {
				sc.ForwardSetState(SCE_RUST_DEFAULT);
			} else if (sc.atLineEnd || sc.currentPos - charStartPos >= MaxRustCharLiteralLength - 1) {
				// prevent changing styles for remain document on typing.
				sc.SetState(SCE_RUST_DEFAULT);
			}
			break;

		case SCE_RUST_ESCAPECHAR:
			if (escSeq.atEscapeEnd(sc.ch)) {
				if (escSeq.brace && sc.ch == '}') {
					sc.Forward();
				}
				sc.SetState(escSeq.outerState);
				continue;
			}
			break;
		}

		if (sc.state == SCE_RUST_DEFAULT) {
			if (sc.ch == '/' && (sc.chNext == '/' || sc.chNext == '*')) {
				visibleCharsBefore = visibleChars;
				const int chNext = sc.chNext;
				sc.SetState((chNext == '/') ? SCE_RUST_COMMENTLINE : SCE_RUST_COMMENTBLOCK);
				sc.Forward(2);
				if (sc.ch == '!' || (sc.ch == chNext && sc.chNext != chNext)) {
					sc.ChangeState((chNext == '/') ? SCE_RUST_COMMENTLINEDOC : SCE_RUST_COMMENTBLOCKDOC);
				}
				if (chNext == '/') {
					if (visibleChars == 0) {
						lineStateLineType = RustLineStateMaskLineComment;
					}
				} else {
					commentLevel = 1;
				}
				continue;
			}
			if (sc.ch == '#') {
				if (sc.chNext == '[' || ((sc.chNext == '!' || isspacechar(sc.chNext)) && LexGetNextChar(styler, sc.currentPos + 2) == '[')) {
					// only support `#...[attr]` or `#!...[attr]`, not `#...!...[attr]`
					sc.SetState(SCE_RUST_ATTRIBUTE);
					if (sc.chNext == '!') {
						sc.Forward();
					}
					lineStateAttribute = RustLineStateMaskAttribute;
				}
			} else if (sc.ch == '\"') {
				sc.SetState(SCE_RUST_STRING);
			} else if (sc.ch == '\'') {
				if (IsIdentifierStartEx(sc.chNext) && sc.GetRelative(2) != '\'') {
					sc.SetState(SCE_RUST_LIFETIME);
				} else {
					charStartPos = sc.currentPos;
					sc.SetState(SCE_RUST_CHARACTER);
				}
			} else if (sc.Match('r', '#')) {
				if (IsRustRawString(styler, sc.currentPos + 2, true, hashCount)) {
					hashCount += 1;
					sc.SetState(SCE_RUST_RAW_STRING);
					sc.Advance(hashCount + 1);
				} else {
					if (sc.chPrev != '.') {
						chBeforeIdentifier = sc.chPrev;
					}
					sc.SetState(SCE_RUST_IDENTIFIER);
					const int chNext = sc.GetRelative(2);
					if (IsIdentifierStart(chNext)) {
						// raw identifier: r# + keyword
						sc.Forward();
					}
				}
			} else if (sc.Match('r', '\"')) {
				hashCount = 0;
				sc.SetState(SCE_RUST_RAW_STRING);
				sc.Forward();
			} else if (sc.Match('b', '\"')) {
				sc.SetState(SCE_RUST_BYTESTRING);
				sc.Forward();
			} else if (sc.Match('b', '\'')) {
				charStartPos = sc.currentPos;
				sc.SetState(SCE_RUST_BYTE_CHARACTER);
				sc.Forward();
			} else if (sc.Match('b', 'r')) {
				if (IsRustRawString(styler, sc.currentPos + 2, true, hashCount)) {
					sc.SetState(SCE_RUST_RAW_BYTESTRING);
					sc.Advance(hashCount + 2);
				} else {
					if (sc.chPrev != '.') {
						chBeforeIdentifier = sc.chPrev;
					}
					sc.SetState(SCE_RUST_IDENTIFIER);
				}
			} else if (sc.ch == '$' && IsIdentifierStartEx(sc.chNext)) {
				sc.SetState(SCE_RUST_VARIABLE);
			} else if (IsADigit(sc.ch)) {
				sc.SetState(SCE_RUST_NUMBER);
			} else if (IsIdentifierStartEx(sc.ch)) {
				if (sc.chPrev != '.') {
					chBeforeIdentifier = sc.chPrev;
				}
				sc.SetState(SCE_RUST_IDENTIFIER);
			} else if (isoperator(sc.ch) || sc.ch == '$' || sc.ch == '@') {
				sc.SetState(SCE_RUST_OPERATOR);
				if (lineStateAttribute) {
					if (sc.ch == '[') {
						++squareBracket;
					} else if (sc.ch == ']') {
						--squareBracket;
						if (squareBracket == 0) {
							lineStateAttribute = 0;
						}
					}
				}
			}
		}

		if (!isspacechar(sc.ch)) {
			visibleChars++;
		}
		if (sc.atLineEnd) {
			const int lineState = (squareBracket << 3) | (commentLevel << 11) | (hashCount << 19)
				| lineStateAttribute | lineStateLineType;
			styler.SetLineState(sc.currentLine, lineState);
			lineStateLineType = 0;
			visibleChars = 0;
			visibleCharsBefore = 0;
			kwType = KeywordType::None;
		}
		sc.Forward();
	}

	sc.Complete();
}

struct FoldLineState {
	int lineComment;
	int pubUse;
	constexpr explicit FoldLineState(int lineState) noexcept:
		lineComment(lineState & RustLineStateMaskLineComment),
		pubUse((lineState >> 1) & 1) {
	}
};

constexpr bool IsMultilineStringStyle(int style) noexcept {
	return style == SCE_RUST_STRING
		|| style == SCE_RUST_BYTESTRING
		|| style == SCE_RUST_RAW_STRING
		|| style == SCE_RUST_RAW_BYTESTRING
		|| style == SCE_RUST_ESCAPECHAR
		|| style == SCE_RUST_FORMAT_SPECIFIER
		|| style == SCE_RUST_PLACEHOLDER
		|| style == SCE_RUST_LINE_CONTINUATION;
}

void FoldRustDoc(Sci_PositionU startPos, Sci_Position lengthDoc, int initStyle, LexerWordList, Accessor &styler) {
	const Sci_PositionU endPos = startPos + lengthDoc;
	Sci_Line lineCurrent = styler.GetLine(startPos);
	FoldLineState foldPrev(0);
	int levelCurrent = SC_FOLDLEVELBASE;
	if (lineCurrent > 0) {
		levelCurrent = styler.LevelAt(lineCurrent - 1) >> 16;
		foldPrev = FoldLineState(styler.GetLineState(lineCurrent - 1));
		const Sci_PositionU bracePos = CheckBraceOnNextLine(styler, lineCurrent - 1, SCE_RUST_OPERATOR, SCE_RUST_TASKMARKER);
		if (bracePos) {
			startPos = bracePos + 1; // skip the brace
		}
	}

	int levelNext = levelCurrent;
	FoldLineState foldCurrent(styler.GetLineState(lineCurrent));
	Sci_PositionU lineStartNext = styler.LineStart(lineCurrent + 1);
	lineStartNext = sci::min(lineStartNext, endPos);

	char chNext = styler[startPos];
	int styleNext = styler.StyleAt(startPos);
	int style = initStyle;
	int visibleChars = 0;

	while (startPos < endPos) {
		const char ch = chNext;
		const int stylePrev = style;
		style = styleNext;
		chNext = styler[++startPos];
		styleNext = styler.StyleAt(startPos);

		switch (style) {
		case SCE_RUST_COMMENTBLOCK:
		case SCE_RUST_COMMENTBLOCKDOC: {
			const int level = (ch == '/' && chNext == '*') ? 1 : ((ch == '*' && chNext == '/') ? -1 : 0);
			if (level != 0) {
				levelNext += level;
				startPos++;
				style = styleNext;
				chNext = styler[startPos];
				styleNext = styler.StyleAt(startPos);
			}
		} break;

		case SCE_RUST_STRING:
		case SCE_RUST_BYTESTRING:
		case SCE_RUST_RAW_STRING:
		case SCE_RUST_RAW_BYTESTRING:
			if (!IsMultilineStringStyle(stylePrev)) {
				levelNext++;
			} else if (!IsMultilineStringStyle(styleNext)) {
				levelNext--;
			}
			break;

		case SCE_RUST_OPERATOR:
			if (ch == '{' || ch == '[' || ch == '(') {
				levelNext++;
			} else if (ch == '}' || ch == ']' || ch == ')') {
				levelNext--;
			}
			break;
		}

		if (visibleChars == 0 && !IsSpaceEquiv(style)) {
			++visibleChars;
		}
		if (startPos == lineStartNext) {
			const FoldLineState foldNext(styler.GetLineState(lineCurrent + 1));
			if (foldCurrent.lineComment) {
				levelNext += foldNext.lineComment - foldPrev.lineComment;
			} else if (foldCurrent.pubUse) {
				levelNext += foldNext.pubUse - foldPrev.pubUse;
			} else if (visibleChars) {
				const Sci_PositionU bracePos = CheckBraceOnNextLine(styler, lineCurrent, SCE_RUST_OPERATOR, SCE_RUST_TASKMARKER);
				if (bracePos) {
					levelNext++;
					startPos = bracePos + 1; // skip the brace
					style = SCE_RUST_OPERATOR;
					chNext = styler[startPos];
					styleNext = styler.StyleAt(startPos);
				}
			}

			const int levelUse = levelCurrent;
			int lev = levelUse | levelNext << 16;
			if (levelUse < levelNext) {
				lev |= SC_FOLDLEVELHEADERFLAG;
			}
			if (lev != styler.LevelAt(lineCurrent)) {
				styler.SetLevel(lineCurrent, lev);
			}

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

LexerModule lmRust(SCLEX_RUST, ColouriseRustDoc, "rust", FoldRustDoc);
