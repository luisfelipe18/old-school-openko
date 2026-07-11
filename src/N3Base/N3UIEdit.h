// N3UIEdit.h: interface for the CN3UIEdit class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_N3UIEDIT_H__91BCC181_3AA5_4CD4_8D33_06D5D96F4F26__INCLUDED_)
#define AFX_N3UIEDIT_H__91BCC181_3AA5_4CD4_8D33_06D5D96F4F26__INCLUDED_

#pragma once

#include "N3UIStatic.h"

class CN3UIEdit : public CN3UIStatic
{
	static constexpr uint32_t DEFAULT_MAX_LENGTH = 0x7fffffff;

	//	friend class CN3IME;
public:
	CN3UIEdit();
	~CN3UIEdit() override;

	// class
protected:
	class CN3Caret
	{
	public:
		CN3Caret();
		virtual ~CN3Caret();
		void SetPos(int x, int y);
		void MoveOffset(int iOffsetX, int iOffsetY);
		void SetSize(int iSize);
		void SetColor(D3DCOLOR color);
		void Render(LPDIRECT3DDEVICE9 lpD3DDev);
		void InitFlckering();     // 깜박임 초기화..
		BOOL m_bVisible;          // 보이나

	protected:
		int m_iSize;              // caret의 pixel 크기
		float m_fFlickerTimePrev; // 깜박이기 위한 시간..
		bool m_bFlickerStatus;
		//		POINT	m_ptPos;				// caret의 pixel 좌표
		__VertexTransformedColor m_pVB[2]; // vertex 버퍼
	};

										   // Attributes
public:
	static char s_szBuffTmp[512];

	// The native Win32 EDIT control + IME plumbing is Windows-only. On POSIX
	// the edit owns its text buffer directly and is fed by the SDL entry
	// point (docs/PORT_POSIX_PLAN.md, T7.2): committed/composed UTF-8 comes
	// in through OnTextInput/OnTextEditing, editing keys through OnKeyDown,
	// and focus changes notify the entry point through the registered hooks
	// so it can start/stop the OS text input (IME) over the control's area.
#ifdef _WIN32
	static HWND s_hWndEdit, s_hWndParent;
	static WNDPROC s_lpfnEditProc;

	static void SetImeStatus(POINT ptPos, bool bOpen);
	static BOOL CreateEditWindow(HWND hParent, RECT rect);
	static LRESULT APIENTRY EditWndProc(HWND hWnd, uint16_t Message, WPARAM wParam, LPARAM lParam);
	static void UpdateTextFromEditCtrl();
	static void UpdateCaretPosFromEditCtrl();
#else
	struct TextInputHooks
	{
		void (*pOnFocusGained)(const RECT& rcEdit) = nullptr; // start text input over the edit
		void (*pOnFocusLost)()                     = nullptr; // stop text input
	};
	static void SetTextInputHooks(const TextInputHooks& hooks);

	/// True while an edit control has keyboard focus (text input is active).
	static bool TextInputActive();

	/// Committed text from the OS (SDL_EVENT_TEXT_INPUT), UTF-8.
	static void OnTextInput(const char* szUtf8);

	/// IME composition preview (SDL_EVENT_TEXT_EDITING), UTF-8; empty cancels.
	static void OnTextEditing(const char* szUtf8);

	/// Editing keys (DIK_* codes); returns true when the key was consumed.
	static bool OnKeyDown(int iDik);
#endif

protected:
	static CN3Caret s_Caret;
	size_t m_nCaretPos;       // 글자 단위위치(byte단위)
	int m_iCompLength;        // 현재 조합중인 글자의 byte수 0이면 조합중이 아니다.
	size_t m_iMaxStrLen;      // 쓸수 있는 글씨의 최대 숫자
	std::string m_szPassword; // password buffer

	CN3SndObj* m_pSnd_Typing; // 타이핑 할 때 나는 소리
							  // Operations
public:
	const std::string& GetString() override;
	void SetString(const std::string& szString) override;

	bool Load(File& file) override;
	void Render() override;
	void Release() override;
	void SetVisible(bool bVisible) override;
	uint32_t MouseProc(uint32_t dwFlags, const POINT& ptCur, const POINT& ptOld) override;

	// 위치 지정(chilren의 위치도 같이 바꾸어준다. caret위치도 같이 바꾸어줌.)
	BOOL MoveOffset(int iOffsetX, int iOffsetY) override;

	void KillFocus(); // 포커스를 없앤다.
	bool SetFocus();  // 포커스를 준다.

	bool HaveFocus() const
	{
		return (this == s_pFocusedEdit);
	}

	void SetCaretPos(size_t nPos);                          //몇번째 바이트에 있는지 설정한다.
	void SetMaxString(size_t nMax);                         // 최대 글씨 수를 정해준다.

protected:
	BOOL IsHangulMiddleByte(const char* lpszStr, int iPos); // 한글의 2번째 바이트 글자인가?

#ifndef _WIN32
	// POSIX text editing over the logical (unmasked) buffer, DBCS-aware.
	void InsertBytesAtCaret(const std::string& szCp949);
	void ReplaceComposition(const std::string& szCp949);
	void ClearComposition();
#endif

#ifdef _N3TOOL
public:
	CN3UIEdit& operator=(const CN3UIEdit& other);
	bool Save(File& file) override;
	void SetSndTyping(const std::string& strFileName);
	std::string GetSndFName_Typing() const;
#endif
};

#endif // !defined(AFX_N3UIEDIT_H__91BCC181_3AA5_4CD4_8D33_06D5D96F4F26__INCLUDED_)
