// Scintilla source code edit control
/** @file Accessor.h
 ** Interfaces between Scintilla and lexers.
 **/
// Copyright 1998-2010 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.
#pragma once

namespace Lexilla {

enum {
	wsSpace = 1, wsTab = 2, wsSpaceTab = 4, wsInconsistent = 8
};

class Accessor;
class PropSetSimple;

typedef bool (*PFNIsCommentLeader)(Accessor &styler, Sci_Position pos, Sci_Position len);

class Accessor final : public LexAccessor {
	const PropSetSimple &props;
public:
	Accessor(Scintilla::IDocument *pAccess_, const PropSetSimple &props_) noexcept;
	const char *GetProperty(const char *key, size_t keyLen) const;
	int GetPropertyInt(const char *key, size_t keyLen, int defaultValue = 0) const;

	template <size_t N>
	const char *GetProperty(const char (&key)[N]) const {
		return GetProperty(key, N - 1);
	}

	template <size_t N>
	int GetPropertyInt(const char (&key)[N], int defaultValue = 0) const {
		return GetPropertyInt(key, N - 1, defaultValue);
	}

	template <size_t N>
	bool GetPropertyBool(const char (&key)[N], bool defaultValue = false) const {
		return GetPropertyInt(key, N - 1, defaultValue) & true;
	}

	int IndentAmount(Sci_Line line) noexcept;

	[[deprecated]]
	int IndentAmount(Sci_Line line, [[maybe_unused]] int *flags, [[maybe_unused]] PFNIsCommentLeader pfnIsCommentLeader = nullptr) noexcept {
		return IndentAmount(line);
	}
};

}
