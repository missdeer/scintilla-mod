// Scintilla source code edit control
/** @file ScintillaBase.h
 ** Defines an enhanced subclass of Editor with calltips, autocomplete and context menu.
 **/
// Copyright 1998-2002 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.
#pragma once

namespace Scintilla::Internal {

#define SCI_EnablePopupMenu	0

class LexState;

/**
 */
class ScintillaBase : public Editor, IListBoxDelegate {
protected:
	/** Enumeration of commands and child windows. */
	enum {
		idCallTip = 1,
		idAutoComplete = 2,

#if SCI_EnablePopupMenu
		idcmdUndo = 10,
		idcmdRedo = 11,
		idcmdCut = 12,
		idcmdCopy = 13,
		idcmdPaste = 14,
		idcmdDelete = 15,
		idcmdSelectAll = 16
#endif
	};

	enum class NotificationPosition {
		None,
		BottomRight,
		Center,
	};

#if SCI_EnablePopupMenu
	Scintilla::PopUp displayPopupMenu;
	Menu popup;
#endif
	Scintilla::Internal::AutoComplete ac;

	CallTip ct;

	int listType;			///< 0 is an autocomplete list
	int maxListWidth;		/// Maximum width of list, in average character widths
	Scintilla::MultiAutoComplete multiAutoCMode; /// Mode for autocompleting when multiple selections are present

	LexState *DocumentLexState();

	ScintillaBase() noexcept;
	// ~ScintillaBase() in public section
	void Initialise() noexcept override {}
	void Finalise() noexcept override;

	void InsertCharacter(std::string_view sv, Scintilla::CharacterSource charSource) override;
#if SCI_EnablePopupMenu
	void Command(int cmdId);
#endif
	void CancelModes() noexcept override;
	int KeyCommand(Scintilla::Message iMessage) override;

	void AutoCompleteInsert(Sci::Position startPos, Sci::Position removeLen, std::string_view text);
	void AutoCompleteStart(Sci::Position lenEntered, const char *list);
	void AutoCompleteCancel() noexcept;
	void AutoCompleteMove(int delta);
	int AutoCompleteGetCurrent() const noexcept;
	int AutoCompleteGetCurrentText(char *buffer) const;
	void AutoCompleteCharacterAdded(char ch);
	void AutoCompleteCharacterDeleted();
	void AutoCompleteNotifyCompleted(char ch, CompletionMethods completionMethod, Sci::Position firstPos, const char *text);
	void AutoCompleteCompleted(char ch, Scintilla::CompletionMethods completionMethod);
	void AutoCompleteMoveToCurrentWord();
	void AutoCompleteSelection();
	void ListNotify(ListBoxEvent *plbe) override;

	void CallTipClick() noexcept;
	void SCICALL CallTipShow(Point pt, NotificationPosition notifyPos, const char *defn);
	virtual void SCICALL CreateCallTipWindow(PRectangle rc) noexcept = 0;

#if SCI_EnablePopupMenu
	virtual void AddToPopUp(const char *label, int cmd = 0, bool enabled = true) noexcept = 0;
	bool ShouldDisplayPopup(Point ptInWindowCoordinates) const noexcept;
	void ContextMenu(Point pt) noexcept;
#endif

	void ButtonDownWithModifiers(Point pt, unsigned int curTime, Scintilla::KeyMod modifiers) override;
	void RightButtonDownWithModifiers(Point pt, unsigned int curTime, Scintilla::KeyMod modifiers) override;

	void NotifyStyleToNeeded(Sci::Position endStyleNeeded) override;

public:
	~ScintillaBase() override;

	// Deleted so ScintillaBase objects can not be copied.
	ScintillaBase(const ScintillaBase &) = delete;
	ScintillaBase(ScintillaBase &&) = delete;
	ScintillaBase &operator=(const ScintillaBase &) = delete;
	ScintillaBase &operator=(ScintillaBase &&) = delete;
	// Public so scintilla_send_message can use it
	Scintilla::sptr_t WndProc(Scintilla::Message iMessage, Scintilla::uptr_t wParam, Scintilla::sptr_t lParam) override;
};

}
