// This file is part of Notepad2.
// See License.txt for details about distribution and modification.
//! Lexer for JavaScript, JScript, TypeScript, ActionScript.

#include <cassert>
#include <cstring>

#include <string>
#include <string_view>
#include <vector>

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
#include "LexerUtils.h"
#include "DocUtils.h"

using namespace Lexilla;

namespace {

// https://tc39.es/ecma262/#prod-StringLiteral
struct EscapeSequence {
	int outerState = SCE_JS_DEFAULT;
	int digitsLeft = 0;
	bool brace = false;

	// highlight any character as escape sequence.
	void resetEscapeState(int state, int chNext) noexcept {
		outerState = state;
		digitsLeft = (chNext == 'x')? 3 : ((chNext == 'u') ? 5 : 1);
	}
	bool atEscapeEnd(int ch) noexcept {
		--digitsLeft;
		return digitsLeft <= 0 || !IsHexDigit(ch);
	}
};

enum {
	JsLineStateMaskLineComment = 1,		// line comment
	JsLineStateMaskImport = (1 << 1),	// import

	JsLineStateInsideJsxExpression = 1 << 3,
	JsLineStateLineContinuation = 1 << 4,
};

//KeywordIndex++Autogenerated -- start of section automatically generated
enum {
	KeywordIndex_Keyword = 0,
	KeywordIndex_FutureReservedWord = 1,
	KeywordIndex_Type = 1,
	KeywordIndex_Directive = 2,
	KeywordIndex_Class = 3,
	KeywordIndex_Interface = 4,
	KeywordIndex_Enumeration = 5,
	KeywordIndex_Constant = 6,
	KeywordIndex_Decorator = 7,
	KeywordIndex_Metadata = 7,
};
//KeywordIndex--Autogenerated -- end of section automatically generated

enum class KeywordType {
	None = SCE_JS_DEFAULT,
	Class = SCE_JS_CLASS,
	Interface = SCE_JS_INTERFACE,
	Enum = SCE_JS_ENUM,
	Function = SCE_JS_FUNCTION_DEFINITION,
	Label = SCE_JS_LABEL,
};

enum class DocTagState {
	None,
	At,				/// @param x
	InlineAt,		/// {@link https://tsdoc.org/}
	XmlOpen,		/// <reference path="" />
	XmlClose,		/// </param>, No this (C# like) style
};

static_assert(DefaultNestedStateBaseStyle + 1 == SCE_JSX_OTHER);
static_assert(DefaultNestedStateBaseStyle + 2 == SCE_JSX_TEXT);
static_assert(DefaultNestedStateBaseStyle + 3 == SCE_JS_STRING_BT);

inline bool IsJsIdentifierStartNext(const StyleContext &sc) noexcept {
	return IsJsIdentifierStart(sc.chNext) || sc.MatchNext('\\', 'u');
}

constexpr bool IsSpaceEquiv(int state) noexcept {
	return state <= SCE_JS_TASKMARKER;
}

constexpr int GetStringQuote(int state) noexcept {
	if (state == SCE_JS_STRING_BT) {
		return '`';
	}
	if constexpr (SCE_JS_STRING_SQ & 1) {
		return (state & 1) ? '\'' : '\"';
	} else {
		return (state & 1) ? '\"' : '\'';
	}
}

constexpr bool FollowExpression(int chPrevNonWhite, int stylePrevNonWhite) noexcept {
	return chPrevNonWhite == ')' || chPrevNonWhite == ']'
		|| stylePrevNonWhite == SCE_JS_OPERATOR_PF
		|| IsJsIdentifierChar(chPrevNonWhite);
}

constexpr bool IsRegexStart(int chPrevNonWhite, int stylePrevNonWhite) noexcept {
	return stylePrevNonWhite == SCE_JS_WORD || !FollowExpression(chPrevNonWhite, stylePrevNonWhite);
}

inline bool IsJsxTagStart(const StyleContext &sc, int chPrevNonWhite, int stylePrevNonWhite) noexcept {
	// https://facebook.github.io/jsx/
	// https://reactjs.org/docs/jsx-in-depth.html
	return (stylePrevNonWhite == SCE_JSX_TAG || IsRegexStart(chPrevNonWhite, stylePrevNonWhite))
		&& (IsJsIdentifierStartNext(sc) || sc.chNext == '>' || sc.chNext == '{');
}

void ColouriseJsDoc(Sci_PositionU startPos, Sci_Position lengthDoc, int initStyle, LexerWordList keywordLists, Accessor &styler) {
	int lineStateLineType = 0;
	int lineContinuation = 0;
	bool insideRegexRange = false; // inside regex character range []

	KeywordType kwType = KeywordType::None;
	int chBeforeIdentifier = 0;

	std::vector<int> nestedState; // string interpolation "${}"
	int jsxTagLevel = 0;
	std::vector<int> jsxTagLevels;// nested JSX tag in expression

	// JSX syntax conflicts with TypeScript type assert.
	// https://www.typescriptlang.org/docs/handbook/jsx.html
	const bool enableJsx = styler.GetPropertyBool("lexer.lang", false);

	int visibleChars = 0;
	int visibleCharsBefore = 0;
	int chBefore = 0;
	int chPrevNonWhite = 0;
	int stylePrevNonWhite = SCE_JS_DEFAULT;
	DocTagState docTagState = DocTagState::None;
	EscapeSequence escSeq;

	if (enableJsx && startPos != 0) {
		// backtrack to the line starts JSX for better coloring on typing.
		BacktrackToStart(styler, JsLineStateInsideJsxExpression, startPos, lengthDoc, initStyle);
	}

	StyleContext sc(startPos, lengthDoc, initStyle, styler);
	if (sc.currentLine > 0) {
		int lineState = styler.GetLineState(sc.currentLine - 1);
		/*
		2: lineStateLineType
		1: JsLineStateInsideJsxExpression
		1: lineContinuation
		3: nestedState count
		3*4: nestedState
		*/
		lineContinuation = lineState & JsLineStateLineContinuation;
		lineState >>= 8;
		if (lineState) {
			UnpackLineState(lineState, nestedState);
		}
	}
	if (startPos == 0) {
		if (sc.Match('#', '!')) {
			// Shell Shebang at beginning of file
			sc.SetState(SCE_JS_COMMENTLINE);
			sc.Forward();
			lineStateLineType = JsLineStateMaskLineComment;
		}
	} else if (IsSpaceEquiv(initStyle)) {
		// look back for better regex colouring
		LookbackNonWhite(styler, startPos, SCE_JS_TASKMARKER, chPrevNonWhite, stylePrevNonWhite);
	}

	while (sc.More()) {
		switch (sc.state) {
		case SCE_JS_OPERATOR:
		case SCE_JS_OPERATOR2:
		case SCE_JS_OPERATOR_PF:
			sc.SetState(SCE_JS_DEFAULT);
			break;

		case SCE_JS_NUMBER:
			if (!IsDecimalNumberEx(sc.chPrev, sc.ch, sc.chNext)) {
				sc.SetState(SCE_JS_DEFAULT);
			}
			break;

		case SCE_JS_IDENTIFIER:
		case SCE_JSX_TAG:
		case SCE_JSX_ATTRIBUTE:
		case SCE_JSX_ATTRIBUTE_AT:
		case SCE_JS_DECORATOR:
			if ((sc.ch == '.' && !(sc.state == SCE_JS_IDENTIFIER || sc.state == SCE_JSX_ATTRIBUTE_AT))
				|| (sc.ch == ':' && (sc.state == SCE_JSX_TAG || sc.state == SCE_JSX_ATTRIBUTE))) {
				const int state = sc.state;
				sc.SetState(SCE_JS_OPERATOR2);
				sc.ForwardSetState(state);
			}
			if (!IsJsIdentifierChar(sc.ch) && !sc.Match('\\', 'u') && !(sc.ch == '-' && (sc.state == SCE_JSX_TAG || sc.state == SCE_JSX_ATTRIBUTE))) {
				if (sc.state == SCE_JS_IDENTIFIER) {
					char s[128];
					sc.GetCurrent(s, sizeof(s));
					if (keywordLists[KeywordIndex_Directive].InList(s)) {
						sc.ChangeState(SCE_JS_DIRECTIVE);
						if (StrEqualsAny(s, "import", "require")) {
							lineStateLineType = JsLineStateMaskImport;
						}
					} else if (keywordLists[KeywordIndex_Keyword].InList(s)) {
						sc.ChangeState(SCE_JS_WORD);
						if (StrEqualsAny(s, "class", "extends","new", "type", "as", "is")) {
							kwType = KeywordType::Class;
						} else if (StrEqual(s, "function")) {
							kwType = KeywordType::Function;
						} else if (StrEqualsAny(s, "interface", "implements")) {
							kwType = KeywordType::Interface;
						} else if (StrEqual(s, "enum")) {
							kwType = KeywordType::Enum;
						} else if (StrEqualsAny(s, "break", "continue")) {
							kwType = KeywordType::Label;
						}
						if (kwType != KeywordType::None) {
							const int chNext = sc.GetLineNextChar();
							if (!(IsJsIdentifierStart(chNext) || chNext == '\\')) {
								kwType = KeywordType::None;
							}
						}
					} else if (keywordLists[KeywordIndex_FutureReservedWord].InList(s)) {
						sc.ChangeState(SCE_JS_WORD2);
					} else if (keywordLists[KeywordIndex_Class].InList(s)) {
						sc.ChangeState(SCE_JS_CLASS);
					} else if (keywordLists[KeywordIndex_Interface].InList(s)) {
						sc.ChangeState(SCE_JS_INTERFACE);
					} else if (keywordLists[KeywordIndex_Enumeration].InList(s)) {
						sc.ChangeState(SCE_JS_ENUM);
					} else if (keywordLists[KeywordIndex_Constant].InList(s)) {
						sc.ChangeState(SCE_JS_CONSTANT);
					} else if (sc.ch == ':') {
						if (chBefore == ',' || chBefore == '{') {
							sc.ChangeState(SCE_JS_KEY);
						} else if (IsJumpLabelPrevASI(chBefore)) {
							sc.ChangeState(SCE_JS_LABEL);
						}
					} else if (sc.ch != '.') {
						if (kwType != KeywordType::None) {
							sc.ChangeState(static_cast<int>(kwType));
						} else {
							const int chNext = sc.GetDocNextChar(sc.ch == '?');
							if (chNext == '(') {
								sc.ChangeState(SCE_JS_FUNCTION);
							} else if (sc.Match('[', ']')
								|| (chBeforeIdentifier == '<' && (chNext == '>' || chNext == '<'))) {
								// type[]
								// type<type>
								// type<type?>
								// type<type<type>>
								sc.ChangeState(SCE_JS_CLASS);
							}
						}
					}
					stylePrevNonWhite = sc.state;
					if (sc.state != SCE_JS_WORD && sc.ch != '.') {
						kwType = KeywordType::None;
					}
				}
				sc.SetState((sc.state == SCE_JSX_TAG || sc.state == SCE_JSX_ATTRIBUTE) ? SCE_JSX_OTHER : SCE_JS_DEFAULT);
				continue;
			}
			break;

		case SCE_JS_STRING_SQ:
		case SCE_JS_STRING_DQ:
		case SCE_JSX_STRING_SQ:
		case SCE_JSX_STRING_DQ:
		case SCE_JS_STRING_BT:
			if (sc.atLineStart && sc.state != SCE_JS_STRING_BT) {
				if (lineContinuation) {
					lineContinuation = 0;
				} else {
					sc.SetState((sc.state == SCE_JSX_STRING_SQ || sc.state == SCE_JSX_STRING_DQ) ? SCE_JSX_OTHER : SCE_JS_DEFAULT);
					continue;
				}
			}
			if (sc.ch == '\\') {
				if (IsEOLChar(sc.chNext)) {
					lineContinuation = JsLineStateLineContinuation;
				} else {
					escSeq.resetEscapeState(sc.state, sc.chNext);
					sc.SetState(SCE_JS_ESCAPECHAR);
					sc.Forward();
					if (sc.Match('u', '{')) {
						escSeq.brace = true;
						escSeq.digitsLeft = 9; // Unicode code point
						sc.Forward();
					}
				}
			} else if (sc.ch == GetStringQuote(sc.state)) {
				sc.Forward();
				if ((sc.state == SCE_JS_STRING_SQ || sc.state == SCE_JS_STRING_DQ) && (chBefore == ',' || chBefore == '{')) {
					// json key
					const int chNext = sc.GetLineNextChar();
					if (chNext == ':') {
						sc.ChangeState(SCE_JS_KEY);
					}
				}
				sc.SetState((sc.state == SCE_JSX_STRING_SQ || sc.state == SCE_JSX_STRING_DQ) ? SCE_JSX_OTHER : SCE_JS_DEFAULT);
				continue;
			} else if (sc.state == SCE_JS_STRING_BT && sc.Match('$', '{')) {
				nestedState.push_back(sc.state);
				sc.SetState(SCE_JS_OPERATOR2);
				sc.Forward();
			}
			break;

		case SCE_JS_ESCAPECHAR:
			if (escSeq.atEscapeEnd(sc.ch)) {
				if (escSeq.brace && sc.ch == '}') {
					sc.Forward();
				}
				sc.SetState(escSeq.outerState);
				continue;
			}
			break;

		case SCE_JS_REGEX:
			if (sc.atLineStart) {
				sc.SetState(SCE_JS_DEFAULT);
			} else if (sc.ch == '\\') {
				sc.Forward();
			} else if (sc.ch == '[' || sc.ch == ']') {
				insideRegexRange = sc.ch == '[';
			} else if (sc.ch == '/' && !insideRegexRange) {
				sc.Forward();
				// regex flags
				while (IsLowerCase(sc.ch)) {
					sc.Forward();
				}
				sc.SetState(SCE_JS_DEFAULT);
			}
			break;

		case SCE_JS_COMMENTLINE:
		case SCE_JS_COMMENTLINEDOC:
		case SCE_JS_COMMENTBLOCK:
		case SCE_JS_COMMENTBLOCKDOC:
			if (sc.state == SCE_JS_COMMENTLINE || sc.state == SCE_JS_COMMENTLINEDOC) {
				if (sc.atLineStart) {
					sc.SetState(SCE_JS_DEFAULT);
					break;
				}
			} else if (sc.Match('*', '/')) {
				sc.Forward();
				sc.ForwardSetState(SCE_JS_DEFAULT);
				break;
			}
			switch (docTagState) {
			case DocTagState::At:
				docTagState = DocTagState::None;
				break;
			case DocTagState::InlineAt:
				if (sc.ch == '}') {
					docTagState = DocTagState::None;
					sc.SetState(SCE_JS_COMMENTTAGAT);
					sc.ForwardSetState(SCE_JS_COMMENTBLOCKDOC);
				}
				break;
			case DocTagState::XmlOpen:
			case DocTagState::XmlClose:
				if (sc.Match('/', '>') || sc.ch == '>') {
					docTagState = DocTagState::None;
					sc.SetState(SCE_JS_COMMENTTAGXML);
					sc.Forward((sc.ch == '/') ? 2 : 1);
					sc.SetState(SCE_JS_COMMENTLINEDOC);
				}
				break;
			default:
				break;
			}
			if (docTagState == DocTagState::None) {
				if (sc.ch == '@' && IsLowerCase(sc.chNext) && IsCommentTagPrev(sc.chPrev)) {
					docTagState = DocTagState::At;
					escSeq.outerState = sc.state;
					sc.SetState(SCE_JS_COMMENTTAGAT);
				} else if (sc.state == SCE_JS_COMMENTBLOCKDOC && sc.Match('{', '@') && IsLowerCase(sc.GetRelative(2))) {
					docTagState = DocTagState::InlineAt;
					escSeq.outerState = sc.state;
					sc.SetState(SCE_JS_COMMENTTAGAT);
					sc.Forward();
				} else if (sc.state == SCE_JS_COMMENTLINEDOC && sc.ch == '<') {
					if (IsLowerCase(sc.chNext)) {
						docTagState = DocTagState::XmlOpen;
						escSeq.outerState = sc.state;
						sc.SetState(SCE_JS_COMMENTTAGXML);
					} else if (sc.chNext == '/' && IsLowerCase(sc.GetRelative(2))) {
						docTagState = DocTagState::XmlClose;
						escSeq.outerState = sc.state;
						sc.SetState(SCE_JS_COMMENTTAGXML);
						sc.Forward();
					}
				} else if (HighlightTaskMarker(sc, visibleChars, visibleCharsBefore, SCE_JS_TASKMARKER)) {
					continue;
				}
			}
			break;

		case SCE_JS_COMMENTTAGAT:
		case SCE_JS_COMMENTTAGXML:
			if (!(IsIdentifierChar(sc.ch) || sc.ch == '-')) {
				sc.SetState(escSeq.outerState);
				continue;
			}
			break;

		case SCE_JSX_TEXT:
		case SCE_JSX_OTHER:
			if (sc.ch == '>' || sc.Match('/', '>')) {
				sc.SetState(SCE_JSX_TAG);
				if (sc.ch == '/') {
					// self closing <tag />
					--jsxTagLevel;
					sc.Forward();
				}
				chPrevNonWhite = '>';
				stylePrevNonWhite = SCE_JSX_TAG;
				sc.ForwardSetState((jsxTagLevel == 0) ? SCE_JS_DEFAULT : SCE_JSX_TEXT);
				continue;
			} else if (sc.ch == '=' && (sc.state == SCE_JSX_OTHER)) {
				sc.SetState(SCE_JS_OPERATOR2);
				sc.ForwardSetState(SCE_JSX_OTHER);
				continue;
			} else if ((sc.ch == '\'' || sc.ch == '\"') && (sc.state == SCE_JSX_OTHER)) {
				chBefore = 0;
				sc.SetState((sc.ch == '\'') ? SCE_JSX_STRING_SQ : SCE_JSX_STRING_DQ);
			} else if ((sc.state == SCE_JSX_OTHER) && (IsJsIdentifierStart(sc.ch) || sc.Match('\\', 'u'))) {
				sc.SetState(SCE_JSX_ATTRIBUTE);
			} else if (sc.ch == '{') {
				jsxTagLevels.push_back(jsxTagLevel);
				nestedState.push_back(sc.state);
				sc.SetState(SCE_JS_OPERATOR2);
				jsxTagLevel = 0;
			} else if (sc.Match('<', '/')) {
				--jsxTagLevel;
				sc.SetState(SCE_JSX_TAG);
				sc.Forward();
			} else if (sc.ch == '<') {
				++jsxTagLevel;
				sc.SetState(SCE_JSX_TAG);
			}
			break;
		}

		if (sc.state == SCE_JS_DEFAULT) {
			if (sc.ch == '/') {
				if (sc.chNext == '/' || sc.chNext == '*') {
					docTagState = DocTagState::None;
					visibleCharsBefore = visibleChars;
					const int chNext = sc.chNext;
					sc.SetState((chNext == '/') ? SCE_JS_COMMENTLINE : SCE_JS_COMMENTBLOCK);
					sc.Forward(2);
					if (sc.ch == '!' || (sc.ch == chNext && sc.chNext != chNext)) {
						sc.ChangeState((chNext == '/') ? SCE_JS_COMMENTLINEDOC : SCE_JS_COMMENTBLOCKDOC);
					}
					if (chNext == '/') {
						if (visibleChars == 0) {
							lineStateLineType = JsLineStateMaskLineComment;
						}
					}
					continue;
				}
				if (!IsEOLChar(sc.chNext) && IsRegexStart(chPrevNonWhite, stylePrevNonWhite)) {
					insideRegexRange = false;
					sc.SetState(SCE_JS_REGEX);
				} else {
					sc.SetState(SCE_JS_OPERATOR);
				}
			}
			else if (sc.ch == '\'' || sc.ch == '\"') {
				chBefore = chPrevNonWhite;
				sc.SetState((sc.ch == '\'') ? SCE_JS_STRING_SQ : SCE_JS_STRING_DQ);
			} else if (sc.ch == '`') {
				sc.SetState(SCE_JS_STRING_BT);
			} else if (IsNumberStartEx(sc.chPrev, sc.ch, sc.chNext)) {
				sc.SetState(SCE_JS_NUMBER);
			} else if (sc.ch == '@' && IsJsIdentifierStartNext(sc)) {
				sc.SetState((sc.chPrev == '.') ? SCE_JSX_ATTRIBUTE_AT : SCE_JS_DECORATOR);
			} else if (IsJsIdentifierStart(sc.ch) || sc.Match('\\', 'u')) {
				chBefore = chPrevNonWhite;
				if (chPrevNonWhite != '.') {
					chBeforeIdentifier = chPrevNonWhite;
				}
				sc.SetState(SCE_JS_IDENTIFIER);
			}
			else if (sc.ch == '+' || sc.ch == '-') {
				if (sc.ch == sc.chNext) {
					// highlight ++ and -- as different style to simplify regex detection.
					sc.SetState(SCE_JS_OPERATOR_PF);
					sc.Forward();
				} else {
					sc.SetState(SCE_JS_OPERATOR);
				}
			} else if (sc.ch == '<' && enableJsx) {
				// <tag></tag>
				if (sc.chNext == '/') {
					--jsxTagLevel;
					sc.SetState(SCE_JSX_TAG);
					sc.Forward();
				} else if (IsJsxTagStart(sc, chPrevNonWhite, stylePrevNonWhite)) {
					++jsxTagLevel;
					sc.SetState(SCE_JSX_TAG);
				} else {
					sc.SetState(SCE_JS_OPERATOR);
				}
			} else if (IsAGraphic(sc.ch) && sc.ch != '\\') {
				sc.SetState(SCE_JS_OPERATOR);
				if (!nestedState.empty()) {
					if (sc.ch == '{') {
						nestedState.push_back(SCE_JS_DEFAULT);
						if (enableJsx) {
							jsxTagLevels.push_back(jsxTagLevel);
							jsxTagLevel = 0;
						}
					} else if (sc.ch == '}') {
						if (enableJsx) {
							jsxTagLevel = TryTakeAndPop(jsxTagLevels);
						}
						const int outerState = TakeAndPop(nestedState);
						if (outerState != SCE_JS_DEFAULT) {
							sc.ChangeState(SCE_JS_OPERATOR2);
						}
						sc.ForwardSetState(outerState);
						continue;
					}
				}
			}
		}

		if (!isspacechar(sc.ch)) {
			visibleChars++;
			if (!IsSpaceEquiv(sc.state)) {
				chPrevNonWhite = sc.ch;
				stylePrevNonWhite = sc.state;
			}
		}
		if (sc.atLineEnd) {
			int lineState = lineContinuation | lineStateLineType;
			if (enableJsx && !(jsxTagLevel == 0 && jsxTagLevels.empty())) {
				lineState |= JsLineStateInsideJsxExpression;
			}
			if (!nestedState.empty()) {
				lineState |= PackLineState(nestedState) << 8;
			}
			styler.SetLineState(sc.currentLine, lineState);
			lineStateLineType = 0;
			visibleChars = 0;
			visibleCharsBefore = 0;
			kwType = KeywordType::None;
			docTagState = DocTagState::None;
		}
		sc.Forward();
	}

	sc.Complete();
}

struct FoldLineState {
	int lineComment;
	int packageImport;
	int lineContinuation;
	constexpr explicit FoldLineState(int lineState) noexcept:
		lineComment(lineState & JsLineStateMaskLineComment),
		packageImport((lineState >> 1) & 1),
		lineContinuation((lineState >> 4) & 1) {
	}
};

constexpr bool IsStreamCommentStyle(int style) noexcept {
	return style == SCE_JS_COMMENTBLOCK
		|| style == SCE_JS_COMMENTBLOCKDOC
		|| style == SCE_JS_COMMENTTAGAT
		|| style == SCE_JS_COMMENTTAGXML
		|| style == SCE_JS_TASKMARKER;
}

constexpr bool IsMultilineStringStyle(int style) noexcept {
	return style == SCE_JS_STRING_BT
		|| style == SCE_JS_OPERATOR2
		|| style == SCE_JS_ESCAPECHAR;
}

void FoldJsDoc(Sci_PositionU startPos, Sci_Position lengthDoc, int initStyle, LexerWordList, Accessor &styler) {
	const Sci_PositionU endPos = startPos + lengthDoc;
	Sci_Line lineCurrent = styler.GetLine(startPos);
	FoldLineState foldPrev(0);
	int levelCurrent = SC_FOLDLEVELBASE;
	if (lineCurrent > 0) {
		levelCurrent = styler.LevelAt(lineCurrent - 1) >> 16;
		foldPrev = FoldLineState(styler.GetLineState(lineCurrent - 1));
		const Sci_PositionU bracePos = CheckBraceOnNextLine(styler, lineCurrent - 1, SCE_JS_OPERATOR, SCE_JS_TASKMARKER);
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
		case SCE_JS_COMMENTBLOCK:
		case SCE_JS_COMMENTBLOCKDOC:
			if (!IsStreamCommentStyle(stylePrev)) {
				levelNext++;
			} else if (!IsStreamCommentStyle(styleNext)) {
				levelNext--;
			}
			break;

		case SCE_JS_STRING_BT:
			if (!IsMultilineStringStyle(stylePrev)) {
				levelNext++;
			} else if (!IsMultilineStringStyle(styleNext)) {
				levelNext--;
			}
			break;

		case SCE_JS_OPERATOR:
			if (ch == '{' || ch == '[' || ch == '(') {
				levelNext++;
			} else if (ch == '}' || ch == ']' || ch == ')') {
				levelNext--;
			}
			break;

		case SCE_JSX_TAG:
			if (ch == '<') {
				if (chNext == '/') {
					levelNext--;
					startPos++;
					chNext = styler[startPos];
					styleNext = styler.StyleAt(startPos);
				} else {
					levelNext++;
				}
			} else if (ch == '/' && chNext == '>') {
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
			} else if (foldCurrent.packageImport) {
				levelNext += foldNext.packageImport - foldPrev.packageImport;
			} else if (foldCurrent.lineContinuation | foldPrev.lineContinuation) {
				levelNext += foldCurrent.lineContinuation - foldPrev.lineContinuation;
			} else if (visibleChars) {
				const Sci_PositionU bracePos = CheckBraceOnNextLine(styler, lineCurrent, SCE_JS_OPERATOR, SCE_JS_TASKMARKER);
				if (bracePos) {
					levelNext++;
					startPos = bracePos + 1; // skip the brace
					style = SCE_JS_OPERATOR;
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

LexerModule lmJavaScript(SCLEX_JAVASCRIPT, ColouriseJsDoc, "js", FoldJsDoc);
