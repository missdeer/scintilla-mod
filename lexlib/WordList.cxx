// Scintilla source code edit control
/** @file WordList.cxx
 ** Hold a list of words.
 **/
// Copyright 1998-2002 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#include <cassert>
#include <cstring>

#include <algorithm>
#include <iterator>

#include "WordList.h"

using namespace Lexilla;
using range_t = WordList::range_t;

namespace {

/**
 * Creates an array that points into each word in the string and puts \0 terminators
 * after each word.
 */
inline char **ArrayFromWordList(char *wordlist, size_t slen, range_t *len) {
	unsigned char prev = 1;
	range_t words = 0;
	// treat space and C0 control characters as word separators.

	char * const end = wordlist + slen;
	char *s = wordlist;
	while (s < end) {
		const unsigned char ch = *s++;
		const bool curr = ch <= ' ';
		if (!curr && prev) {
			words++;
		}
		prev = curr;
	}

	char **keywords = new char *[words + 1];
	range_t wordsStore = 0;
	if (words) {
		prev = '\0';
		s = wordlist;
		while (s < end) {
			unsigned char ch = *s;
			if (ch > ' ') {
				if (!prev) {
					keywords[wordsStore] = s;
					wordsStore++;
				}
			} else {
				*s = '\0';
				ch = '\0';
			}
			prev = ch;
			++s;
		}
	}

	assert(wordsStore < (words + 1));
	keywords[wordsStore] = end;
	*len = wordsStore;
	return keywords;
}

/** Threshold for linear search.
 * Because of cache locality and other metrics, linear search is faster than binary search
 * when word list contains few words.
 */
constexpr range_t WordListLinearSearchThreshold = 5;

// words in [start, end) starts with same character, maximum word count limited to 0xffff.
struct Range {
	range_t start;
	const range_t end;
	explicit constexpr Range(range_t range) noexcept : start(range & 0xffff), end(range >> 16) {}
	constexpr range_t Length() const noexcept {
		return end - start;
	}
	bool Next() noexcept {
		++start;
		return start < end;
	}
};

}

WordList::~WordList() {
	Clear();
}

#if 0
bool WordList::operator!=(const WordList &other) const noexcept {
	if (len != other.len) {
		return true;
	}
	for (range_t i = 0; i < len; i++) {
		if (strcmp(words[i], other.words[i]) != 0) {
			return true;
		}
	}
	return false;
}
#endif

void WordList::Clear() noexcept {
	if (words) {
		delete[]words;
		delete[]list;
		words = nullptr;
		list = nullptr;
		//len = 0;
	}
}

bool WordList::Set(const char *s, KeywordAttr attribute) {
	// omitted comparison for Notepad2, we don't care whether the list is same as before or not.
	// 1. when we call SciCall_SetKeywords(), the document or lexer already changed.
	// 2. the comparison is expensive than rebuild the list, especially for a long list.

	Clear();
	const size_t lenS = strlen(s) + 1;
	list = new char[lenS];
	memcpy(list, s, lenS);
	if (attribute & KeywordAttr_MakeLower) {
		char *p = list;
		while (*p) {
			if (*p >= 'A' && *p <= 'Z') {
				*p |= 'a' - 'A';
			}
			++p;
		}
	}

	range_t len = 0;
	words = ArrayFromWordList(list, lenS - 1, &len);
	if (!(attribute & KeywordAttr_PreSorted)) {
		std::sort(words, words + len, [](const char *a, const char *b) noexcept {
			return strcmp(a, b) < 0;
		});
	}

	memset(ranges, 0, sizeof(ranges));
	for (range_t i = 0; i < len;) {
		const unsigned char indexChar = *words[i];
		const range_t start = i++;
		while (static_cast<unsigned char>(*words[i]) == indexChar) {
			++i;
		}
		assert(static_cast<unsigned>(indexChar - MinIndexChar) < std::size(ranges));
		ranges[indexChar - MinIndexChar] = start | (i << 16);
	}
	return true;
}

/** Check whether a string is in the list.
 * List elements are either exact matches or prefixes.
 * Prefix elements start with '^' and match all strings that start with the rest of the element
 * so '^GTK_' matches 'GTK_X', 'GTK_MAJOR_VERSION', and 'GTK_'.
 */
bool WordList::InList(const char *s) const noexcept {
	if (nullptr == words) {
		return false;
	}
	const unsigned index = static_cast<unsigned char>(s[0]) - MinIndexChar;
	if (index > std::size(ranges) - 1) {
		return false;
	}
	range_t end = ranges[index];
	if (end) {
		Range range(end);
		range_t count = range.Length();
		if (count < WordListLinearSearchThreshold) {
			do {
				const char *a = words[range.start] + 1;
				const char *b = s + 1;
				while (*a && *a == *b) {
					a++;
					b++;
				}
				if (!*a && !*b) {
					return true;
				}
			} while (range.Next());
		} else {
			do {
				const range_t step = count >> 1;
				const range_t mid = range.start + step;
				const char *a = words[mid] + 1;
				const char *b = s + 1;
				while (*a && *a == *b) {
					a++;
					b++;
				}
				const int diff = static_cast<unsigned char>(*a) - static_cast<unsigned char>(*b);
				if (diff == 0) {
					return true;
				}
				if (diff < 0) {
					range.start = mid + 1;
					count -= step + 1;
				} else {
					count = step;
				}
			} while (count != 0);
		}
	}

	end = ranges[static_cast<unsigned char>('^') - MinIndexChar];
	if (end) {
		Range range(end);
		do {
			const char *a = words[range.start] + 1;
			const char *b = s;
			while (*a && *a == *b) {
				a++;
				b++;
			}
			if (!*a) {
				return true;
			}
		} while (range.Next());
	}
	return false;
}

/**
 * similar to InList, but word s can be a prefix of keyword.
 * mainly used to test whether a function is built-in or not.
 * e.g. for keyword definition "sin(x)", InListPrefixed("sin", '(') => true
 * InList(s) == InListPrefixed(s, '\0')
 */
bool WordList::InListPrefixed(const char *s, const char marker) const noexcept {
	if (nullptr == words) {
		return false;
	}
	const unsigned index = static_cast<unsigned char>(s[0]) - MinIndexChar;
	if (index > std::size(ranges) - 1) {
		return false;
	}
	range_t end = ranges[index];
	if (end) {
		Range range(end);
		range_t count = range.Length();
		if (count < WordListLinearSearchThreshold) {
			do {
				const char *a = words[range.start] + 1;
				const char *b = s + 1;
				while (*a && *a == *b) {
					a++;
					b++;
				}
				if ((!*a || *a == marker) && !*b) {
					return true;
				}
			} while (range.Next());
		} else {
			do {
				const range_t step = count >> 1;
				const range_t mid = range.start + step;
				const char *a = words[mid] + 1;
				const char *b = s + 1;
				while (*a && *a == *b) {
					a++;
					b++;
				}
				const int diff = static_cast<unsigned char>(*a) - static_cast<unsigned char>(*b);
				if (diff == 0 || diff == static_cast<unsigned char>(marker)) {
					return true;
				}
				if (diff < 0) {
					range.start = mid + 1;
					count -= step + 1;
				} else {
					count = step;
				}
			} while (count != 0);
		}
	}

	end = ranges[static_cast<unsigned char>('^') - MinIndexChar];
	if (end) {
		Range range(end);
		do {
			const char *a = words[range.start] + 1;
			const char *b = s;
			while (*a && *a == *b) {
				a++;
				b++;
			}
			if (!*a) {
				return true;
			}
		} while (range.Next());
	}
	return false;
}

/** similar to InList, but word s can be a substring of keyword.
 * eg. the keyword define is defined as def~ine. This means the word must start
 * with def to be a keyword, but also defi, defin and define are valid.
 * The marker is ~ in this case.
 */
bool WordList::InListAbbreviated(const char *s, const char marker) const noexcept {
	if (nullptr == words) {
		return false;
	}
	const unsigned index = static_cast<unsigned char>(s[0]) - MinIndexChar;
	if (index > std::size(ranges) - 1) {
		return false;
	}
	range_t end = ranges[index];
	if (end) {
		Range range(end);
		do {
			bool isSubword = false;
			const char *a = words[range.start] + 1;
			const char *b = s + 1;
			if (*a == marker) {
				isSubword = true;
				a++;
			}
			while (*a && *a == *b) {
				a++;
				if (*a == marker) {
					isSubword = true;
					a++;
				}
				b++;
			}
			if ((!*a || isSubword) && !*b) {
				return true;
			}
		} while (range.Next());
	}

	end = ranges[static_cast<unsigned char>('^') - MinIndexChar];
	if (end) {
		Range range(end);
		do {
			const char *a = words[range.start] + 1;
			const char *b = s;
			while (*a && *a == *b) {
				a++;
				b++;
			}
			if (!*a) {
				return true;
			}
		} while (range.Next());
	}
	return false;
}

/** similar to InListAbbreviated, but word s can be a abridged version of a keyword.
* eg. the keyword is defined as "after.~:". This means the word must have a prefix (begins with) of
* "after." and suffix (ends with) of ":" to be a keyword, Hence "after.field:" , "after.form.item:" are valid.
* Similarly "~.is.valid" keyword is suffix only... hence "field.is.valid" , "form.is.valid" are valid.
* The marker is ~ in this case.
* No multiple markers check is done and wont work.
*/
bool WordList::InListAbridged(const char *s, const char marker) const noexcept {
	if (nullptr == words) {
		return false;
	}
	unsigned index = static_cast<unsigned char>(s[0]) - MinIndexChar;
	if (index > std::size(ranges) - 1) {
		return false;
	}
	range_t end = ranges[index];
	if (end) {
		Range range(end);
		do {
			const char *a = words[range.start];
			const char *b = s;
			while (*a && *a == *b) {
				a++;
				if (*a == marker) {
					a++;
					const size_t suffixLengthA = strlen(a);
					const size_t suffixLengthB = strlen(b);
					if (suffixLengthA >= suffixLengthB) {
						break;
					}
					b = b + suffixLengthB - suffixLengthA - 1;
				}
				b++;
			}
			if (!*a && !*b) {
				return true;
			}
		} while (range.Next());
	}

	index = static_cast<unsigned char>(marker) - MinIndexChar;
	if (index > std::size(ranges) - 1) {
		return false;
	}
	end = ranges[index];
	if (end) {
		Range range(end);
		do {
			const char *a = words[range.start] + 1;
			const char *b = s;
			const size_t suffixLengthA = strlen(a);
			const size_t suffixLengthB = strlen(b);
			if (suffixLengthA > suffixLengthB) {
				continue;
			}
			b = b + suffixLengthB - suffixLengthA;

			while (*a && *a == *b) {
				a++;
				b++;
			}
			if (!*a && !*b) {
				return true;
			}
		} while (range.Next());
	}

	return false;
}

const char *WordList::WordAt(range_t n) const noexcept {
	return words[n];
}
