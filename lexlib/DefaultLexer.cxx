// Scintilla source code edit control
/** @file DefaultLexer.cxx
 ** A lexer base class that provides reasonable default behaviour.
 **/
// Copyright 2017 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#include "ILexer.h"
#include "Scintilla.h"

#include "DefaultLexer.h"

using namespace Lexilla;

DefaultLexer::DefaultLexer(const char *languageName_, int language_) noexcept :
	languageName(languageName_),
	language(language_) {
}

DefaultLexer::~DefaultLexer() = default;

void SCI_METHOD DefaultLexer::Release() noexcept {
	delete this;
}

int SCI_METHOD DefaultLexer::Version() const noexcept {
	return Scintilla::lvRelease5;
}

const char * SCI_METHOD DefaultLexer::PropertyNames() const noexcept {
	return "";
}

int SCI_METHOD DefaultLexer::PropertyType(const char *) const {
	return SC_TYPE_BOOLEAN;
}

const char * SCI_METHOD DefaultLexer::DescribeProperty(const char *) const {
	return "";
}

Sci_Position SCI_METHOD DefaultLexer::PropertySet(const char *, const char *) {
	return -1;
}

const char *SCI_METHOD DefaultLexer::PropertyGet([[maybe_unused]] const char *key) const {
	return "";
}

const char * SCI_METHOD DefaultLexer::DescribeWordListSets() const noexcept {
	return "";
}

Sci_Position SCI_METHOD DefaultLexer::WordListSet(int, int, const char *) {
	return -1;
}

void SCI_METHOD DefaultLexer::Fold(Sci_PositionU, Sci_Position, int, Scintilla::IDocument *) {
}

void * SCI_METHOD DefaultLexer::PrivateCall(int, void *) {
	return nullptr;
}

int SCI_METHOD DefaultLexer::LineEndTypesSupported() const noexcept {
	return SC_LINE_END_TYPE_DEFAULT;
}

int SCI_METHOD DefaultLexer::AllocateSubStyles(int, int) {
	return -1;
}

int SCI_METHOD DefaultLexer::SubStylesStart(int) const noexcept {
	return -1;
}

int SCI_METHOD DefaultLexer::SubStylesLength(int) const noexcept {
	return 0;
}

int SCI_METHOD DefaultLexer::StyleFromSubStyle(int subStyle) const noexcept {
	return subStyle;
}

int SCI_METHOD DefaultLexer::PrimaryStyleFromStyle(int style) const noexcept {
	return style;
}

void SCI_METHOD DefaultLexer::FreeSubStyles() noexcept {
}

void SCI_METHOD DefaultLexer::SetIdentifiers(int, const char *) {
}

int SCI_METHOD DefaultLexer::DistanceToSecondaryStyles() const noexcept {
	return 0;
}

const char * SCI_METHOD DefaultLexer::GetSubStyleBases() const noexcept {
	return "";
}

int SCI_METHOD DefaultLexer::NamedStyles() const noexcept {
	return 0;
}

const char * SCI_METHOD DefaultLexer::NameOfStyle([[maybe_unused]] int style) const noexcept {
	return "";
}

const char * SCI_METHOD DefaultLexer::TagsOfStyle([[maybe_unused]] int style) const noexcept {
	return "";
}

const char * SCI_METHOD DefaultLexer::DescriptionOfStyle([[maybe_unused]] int style) const noexcept {
	return "";
}

// ILexer5 methods
const char * SCI_METHOD DefaultLexer::GetName() const noexcept {
	return languageName;
}

int SCI_METHOD DefaultLexer::GetIdentifier() const noexcept {
	return language;
}
