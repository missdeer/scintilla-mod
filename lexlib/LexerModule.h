// Scintilla source code edit control
/** @file LexerModule.h
 ** Colourise for particular languages.
 **/
// Copyright 1998-2001 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.
#pragma once

namespace Lexilla {

class Accessor;
class WordList;

typedef const WordList (&LexerWordList)[KEYWORDSET_MAX];

typedef void (*LexerFunction)(Sci_PositionU startPos, Sci_Position lengthDoc, int initStyle, LexerWordList keywordLists, Accessor &styler);
typedef Scintilla::ILexer5 *(*LexerFactoryFunction)();

/**
 * A LexerModule is responsible for lexing and folding a particular language.
 */
class LexerModule final {
public:
	const int language;
	LexerFunction const fnLexer;
	LexerFunction const fnFolder;
	LexerFactoryFunction const fnFactory;
	const char *const languageName;

	constexpr LexerModule(
		int language_,
		LexerFunction fnLexer_,
		const char *languageName_ = nullptr,
		LexerFunction fnFolder_ = nullptr) noexcept:
		language(language_),
		fnLexer(fnLexer_),
		fnFolder(fnFolder_),
		fnFactory(nullptr),
		languageName(languageName_) {
	}

	constexpr LexerModule(
		int language_,
		LexerFactoryFunction fnFactory_,
		const char *languageName_ = nullptr) noexcept:
		language(language_),
		fnLexer(nullptr),
		fnFolder(nullptr),
		fnFactory(fnFactory_),
		languageName(languageName_) {
	}

	constexpr int GetLanguage() const noexcept {
		return language;
	}

	Scintilla::ILexer5 *Create() const;

	static const LexerModule *Find(int language_) noexcept;
};

constexpr int SCE_SIMPLE_OPERATOR = 5;
constexpr int SimpleLineStateMaskLineComment = 1;

// code folding for braces, brackets, parentheses and consecutive line comments.
void FoldSimpleDoc(Sci_PositionU startPos, Sci_Position lengthDoc, int /*initStyle*/, LexerWordList /*keywordLists*/, Accessor &styler);

enum {
	PyLineStateMaskEmptyLine = 1 << 0,
	PyLineStateMaskCommentLine = 1 << 1,
	PyLineStateMaskTripleQuote = 1 << 2,
	PyLineStateMaskCloseBrace = 1 << 3,
	PyLineStateLineContinuation = 1 << 4,
	PyLineStateStringInterpolation = 1 << 5,
};
// Python like indentation based folding.
void FoldPyDoc(Sci_PositionU startPos, Sci_Position lengthDoc, int /*initStyle*/, LexerWordList /*keywordLists*/, Accessor &styler);

}
