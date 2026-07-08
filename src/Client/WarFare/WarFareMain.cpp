#include "StdAfx.h"
#include "UIChat.h"
#include "GameOptions.h"
#include "GameEng.h"
#include "resource.h"
#include "text_resources.h"
#include "PacketDef.h"
#include "APISocket.h"
#include "PlayerMySelf.h"
#include "GameProcMain.h"
#include "N3WorldManager.h"

#include <ctime>

#include "UIManager.h"
#include "UIMessageBoxManager.h"

#include <N3Base/DFont.h>
#include <N3Base/N3SndMgr.h>
#include <N3Base/N3UIEdit.h>

#include <windowsx.h>

HWND CreateMainWindow(HINSTANCE hInstance);
LRESULT CALLBACK WndProcMain(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

int APIENTRY WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE /*hPrevInstance*/, _In_ LPSTR /*lpCmdLine*/, _In_ int nShowCmd)
{
	LoadGameOptions();

	srand((uint32_t) time(nullptr));

	// 메인 윈도우를 만들고..
	HWND hWndMain = CreateMainWindow(hInstance);
	if (hWndMain == nullptr)
	{
		CLogWriter::Write("Cannot create window.");
		exit(-1);
	}

	::ShowWindow(hWndMain, nShowCmd); // 보여준다..
	::SetActiveWindow(hWndMain);

	CGameProcedure::s_bWindowed = true;

	// allocate the static members
	CGameProcedure::StaticMemberInit(hInstance, hWndMain);

	// set the game's current procedure to s_pProcLogIn
	CGameProcedure::ProcActiveSet((CGameProcedure*) CGameProcedure::s_pProcLogIn);

#if _DEBUG
	HACCEL hAccel = LoadAccelerators(nullptr, MAKEINTRESOURCE(IDR_MAIN_ACCELATOR));
	HDC hDC       = GetDC(hWndMain);
#endif // #if _DEBUG

	MSG msg {};
	BOOL bGotMsg = FALSE;

	while (WM_QUIT != msg.message)
	{
		// Use PeekMessage() if the app is active, so we can use idle time to
		// render the scene. Else, use GetMessage() to avoid eating CPU time.
		bGotMsg = PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE);
		if (bGotMsg)
		{
#if _DEBUG
			if (0 == TranslateAccelerator(hWndMain, hAccel, &msg))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
#else
			TranslateMessage(&msg);
			DispatchMessage(&msg);
#endif // #if _DEBUG
		}
		else
		{
			// Render a frame during idle time (no messages are waiting)
			CGameProcedure::TickActive();
			CGameProcedure::RenderActive();
		}
	}

#if _DEBUG
	ReleaseDC(hWndMain, hDC);
	DestroyAcceleratorTable(hAccel);
#endif // #if _DEBUG

	CGameProcedure::StaticMemberRelease();

	return static_cast<int>(msg.wParam);
}

HWND CreateMainWindow(HINSTANCE hInstance)
{
	WNDCLASSEXA wc;

	//  only register the window class once - use hInstance as a flag.
	wc.cbSize        = sizeof(WNDCLASSEXA);
	wc.style         = 0;
	wc.lpfnWndProc   = (WNDPROC) WndProcMain;
	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = 0;
	wc.hInstance     = hInstance;
	wc.hIcon         = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MAIN));
	wc.hCursor       = nullptr;
	wc.hbrBackground = (HBRUSH) GetStockObject(NULL_BRUSH);
	wc.lpszMenuName  = nullptr;
	wc.lpszClassName = "Knight OnLine Client";
	wc.hIconSm       = nullptr;

	if (0 == ::RegisterClassExA(&wc))
	{
		CLogWriter::Write("Cannot register window class.");
		exit(-1);
	}

	DWORD style    = 0;
	int iViewWidth = 0, iViewHeight = 0;
	if (CN3Base::s_Options.bWindowMode)
	{
		style = WS_CLIPCHILDREN | WS_CAPTION | WS_SYSMENU | WS_GROUP;

		RECT rc;
		rc.left   = 0;
		rc.right  = CN3Base::s_Options.iViewWidth;
		rc.top    = 0;
		rc.bottom = CN3Base::s_Options.iViewHeight;

		AdjustWindowRect(&rc, style, FALSE);

		iViewWidth  = rc.right - rc.left;
		iViewHeight = rc.bottom - rc.top;
	}
	else
	{
		style       = WS_POPUP | WS_CLIPCHILDREN;
		iViewWidth  = CN3Base::s_Options.iViewWidth;
		iViewHeight = CN3Base::s_Options.iViewHeight;
	}

	return ::CreateWindowExA(
		0, wc.lpszClassName, "Knight OnLine Client", style, 0, 0, iViewWidth, iViewHeight, nullptr, nullptr, hInstance, nullptr);
}

/*
	WndProcMain processes the messages for the main window
*/
LRESULT CALLBACK WndProcMain(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
		case WM_COMMAND:
		{
			uint16_t wNotifyCode = HIWORD(wParam); // notification code
			CN3UIEdit* pEdit     = CN3UIEdit::GetFocusedEdit();

			if (wNotifyCode == EN_CHANGE && pEdit)
			{
				// NOLINTNEXTLINE(performance-no-int-to-ptr)
				HWND hwndCtl = (HWND) lParam;

				if (CN3UIEdit::s_hWndEdit == hwndCtl)
				{
					pEdit->UpdateTextFromEditCtrl();
					pEdit->UpdateCaretPosFromEditCtrl();
					CGameProcedure::SetGameCursor(CGameProcedure::s_hCursorNormal);
				}
			}
		}
		break;

		case WM_ACTIVATE:
		{
			int iActive = LOWORD(wParam); // activation flag
			switch (iActive)
			{
				case WA_CLICKACTIVE:
				case WA_ACTIVE:
				{
					SetFocus(hWnd);

					CN3UIEdit* pUIFocused = CN3UIBase::GetFocusedEdit();
					if (pUIFocused != nullptr)
					{
						pUIFocused->KillFocus();
						pUIFocused->SetFocus();
					}

					CGameProcedure::s_bIsWindowInFocus = true;
				}
					return 1;

				case WA_INACTIVE:
					CGameProcedure::s_bIsWindowInFocus = false;

					if (!CGameProcedure::s_bWindowed)
					{
						CLogWriter::Write("WA_INACTIVE.");
						PostQuitMessage(0);
					}
					break;

				default:
					break;
			}
		}
		break;

		case WM_CLOSE:
		case WM_DESTROY:
		case WM_QUIT:
		{
			if (CGameProcedure::s_pProcActive != nullptr && CGameProcedure::s_pProcActive == CGameProcedure::s_pProcMain)
			{
				if (!_IsKeyDown(VK_MENU))
				{
					CGameProcedure::s_pProcMain->RequestExit();
					return 1;
				}

				if (CGameProcedure::s_pProcMain->m_fExitTimer != -1.0f)
				{
					if (CGameProcedure::s_pProcMain->m_pUIChatDlg != nullptr)
					{
						std::string szMsg = fmt::format_text_resource(IDS_CANNOT_EXIT_DURING_A_BATTLE);
						CGameProcedure::s_pProcMain->m_pUIChatDlg->AddChatMsg(N3_CHAT_NORMAL, szMsg, 0xFFFF0000);
						CGameProcedure::s_pProcMain->m_eExitType = EXIT_TYPE_QUIT;
					}

					return 1;
				}
			}

			CGameProcedure::s_pSocket->Disconnect();
			CGameProcedure::s_pSocketSub->Disconnect();

			PostQuitMessage(0);
		}
		break;

		case WM_MOUSEWHEEL:
		{
			CN3UIBase* pUI = nullptr;
			if (CGameProcedure::s_pMsgBoxMgr != nullptr)
				pUI = CGameProcedure::s_pMsgBoxMgr->GetFocusMsgBox();

			short delta = GET_WHEEL_DELTA_WPARAM(wParam);

			if (pUI != nullptr && pUI->IsVisible() && pUI->OnMouseWheelEvent(delta))
				break;

			if (CGameProcedure::s_pUIMgr != nullptr)
				pUI = CGameProcedure::s_pUIMgr->GetFocusedUI();

			if (pUI != nullptr && pUI->IsVisible() && pUI->OnMouseWheelEvent(delta))
				break;

			if (CGameProcedure::s_pProcActive == CGameProcedure::s_pProcMain)
				CGameProcedure::s_pEng->CameraZoom(delta * 0.05f);
		}
		break;

		default:
			break;
	}

	return DefWindowProc(hWnd, message, wParam, lParam);
}
