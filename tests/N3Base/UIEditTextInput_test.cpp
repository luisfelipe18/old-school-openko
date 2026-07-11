// POSIX CN3UIEdit text input (docs/PORT_POSIX_PLAN.md, T7.2): the SDL entry
// point feeds UTF-8 text and editing keys into the focused edit through the
// static CN3UIEdit entry points. These tests drive that surface headlessly:
// insertion, DBCS-aware caret movement and deletion, password masking, the
// max-length clamp, IME composition replace/commit, and the focus hooks that
// start/stop the OS text input.

#include <gtest/gtest.h>

#include <N3Base/N3UIEdit.h>
#include <N3Base/N3UIString.h>
#include <N3Base/RHI/RHIDeviceNull.h>

#include <Platform/DInputKeyCodes.h>
#include <Platform/PlatformEncoding.h>

namespace
{
// Captures UIMSG_* notifications the edit sends to its parent (enter key).
class MessageSink : public CN3UIBase
{
public:
	uint32_t m_dwLastMsg = 0;
	int m_iMsgCount      = 0;

	bool ReceiveMessage(CN3UIBase* /*pSender*/, uint32_t dwMsg) override
	{
		m_dwLastMsg = dwMsg;
		++m_iMsgCount;
		return true;
	}
};

// CN3UIEdit wires m_pBuffOutRef from its .uif children on Load; build the
// same wiring manually for a headless test.
class TestEdit : public CN3UIEdit
{
public:
	CN3UIString* m_pDisplay = nullptr;

	void BuildTextBuffer()
	{
		const RECT rc = { 10, 10, 210, 30 };
		SetRegion(rc);

		m_pDisplay = new CN3UIString(); // owned by the child list
		m_pDisplay->Init(this);
		m_pDisplay->SetRegion(rc);
		m_pDisplay->SetStyle(UISTYLE_STRING_SINGLELINE);
		m_pBuffOutRef = m_pDisplay;
	}

	size_t CaretPos() const
	{
		return m_nCaretPos;
	}
};

int s_iFocusGained = 0;
int s_iFocusLost   = 0;

class UIEditTextInputTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		CN3Base::RHIDeviceSet(&m_Device);

		CN3UIEdit::TextInputHooks hooks;
		hooks.pOnFocusGained = [](const RECT& /*rcEdit*/) { ++s_iFocusGained; };
		hooks.pOnFocusLost   = []() { ++s_iFocusLost; };
		CN3UIEdit::SetTextInputHooks(hooks);

		s_iFocusGained = 0;
		s_iFocusLost   = 0;
	}

	void TearDown() override
	{
		CN3UIEdit::SetTextInputHooks({});
		CN3Base::RHIDeviceSet(nullptr);
	}

	RHIDeviceNull m_Device;
};
} // namespace

TEST_F(UIEditTextInputTest, TypingInsertsAndEditsAscii)
{
	TestEdit edit;
	edit.Init(nullptr);
	edit.BuildTextBuffer();

	EXPECT_FALSE(CN3UIEdit::TextInputActive());
	EXPECT_TRUE(edit.SetFocus());
	EXPECT_TRUE(CN3UIEdit::TextInputActive());

	CN3UIEdit::OnTextInput("abc");
	EXPECT_EQ(edit.GetString(), "abc");
	EXPECT_EQ(edit.CaretPos(), 3u);

	// Backspace removes the last character.
	EXPECT_TRUE(CN3UIEdit::OnKeyDown(DIK_BACK));
	EXPECT_EQ(edit.GetString(), "ab");
	EXPECT_EQ(edit.CaretPos(), 2u);

	// Arrow left + insertion in the middle.
	EXPECT_TRUE(CN3UIEdit::OnKeyDown(DIK_LEFT));
	CN3UIEdit::OnTextInput("X");
	EXPECT_EQ(edit.GetString(), "aXb");
	EXPECT_EQ(edit.CaretPos(), 2u);

	// Delete removes the character at the caret.
	EXPECT_TRUE(CN3UIEdit::OnKeyDown(DIK_DELETE));
	EXPECT_EQ(edit.GetString(), "aX");

	// Home / End.
	EXPECT_TRUE(CN3UIEdit::OnKeyDown(DIK_HOME));
	EXPECT_EQ(edit.CaretPos(), 0u);
	EXPECT_TRUE(CN3UIEdit::OnKeyDown(DIK_END));
	EXPECT_EQ(edit.CaretPos(), 2u);

	// Unhandled keys are not consumed (e.g. tab circulates focus upstream).
	EXPECT_FALSE(CN3UIEdit::OnKeyDown(DIK_TAB));

	edit.KillFocus();
	EXPECT_FALSE(CN3UIEdit::TextInputActive());
}

TEST_F(UIEditTextInputTest, HangulEditsWholeCharacters)
{
	// 가 (U+AC00) in CP949 is the two-byte pair B0 A1.
	const std::string szGa = Utf8ToCp949("가");
	ASSERT_EQ(szGa.size(), 2u);

	TestEdit edit;
	edit.Init(nullptr);
	edit.BuildTextBuffer();
	edit.SetFocus();

	CN3UIEdit::OnTextInput("a");
	CN3UIEdit::OnTextInput("가");
	CN3UIEdit::OnTextInput("b");
	EXPECT_EQ(edit.GetString(), "a" + szGa + "b");
	EXPECT_EQ(edit.CaretPos(), 4u);

	// Arrows move over the DBCS pair as one character.
	CN3UIEdit::OnKeyDown(DIK_LEFT);
	EXPECT_EQ(edit.CaretPos(), 3u);
	CN3UIEdit::OnKeyDown(DIK_LEFT);
	EXPECT_EQ(edit.CaretPos(), 1u);
	CN3UIEdit::OnKeyDown(DIK_RIGHT);
	EXPECT_EQ(edit.CaretPos(), 3u);

	// Backspace removes the whole Hangul syllable.
	CN3UIEdit::OnKeyDown(DIK_BACK);
	EXPECT_EQ(edit.GetString(), "ab");
	EXPECT_EQ(edit.CaretPos(), 1u);

	edit.KillFocus();
}

TEST_F(UIEditTextInputTest, ImeCompositionReplacesAndCommits)
{
	const std::string szGa  = Utf8ToCp949("가");
	const std::string szGan = Utf8ToCp949("간");

	TestEdit edit;
	edit.Init(nullptr);
	edit.BuildTextBuffer();
	edit.SetFocus();

	// Composition preview grows in place: ㄱ→가→간 style (using full
	// syllables here; the mechanics are byte-identical).
	CN3UIEdit::OnTextEditing("가");
	EXPECT_EQ(edit.GetString(), szGa);
	CN3UIEdit::OnTextEditing("간");
	EXPECT_EQ(edit.GetString(), szGan);

	// While composing, the IME owns the editing keys.
	EXPECT_FALSE(CN3UIEdit::OnKeyDown(DIK_BACK));
	EXPECT_EQ(edit.GetString(), szGan);

	// Commit replaces the preview with the final text.
	CN3UIEdit::OnTextInput("간");
	EXPECT_EQ(edit.GetString(), szGan);
	EXPECT_EQ(edit.CaretPos(), 2u);

	// After the commit the keys work again: backspace eats the syllable.
	EXPECT_TRUE(CN3UIEdit::OnKeyDown(DIK_BACK));
	EXPECT_EQ(edit.GetString(), "");

	// A canceled composition (empty preedit) leaves the buffer untouched.
	CN3UIEdit::OnTextEditing("가");
	EXPECT_EQ(edit.GetString(), szGa);
	CN3UIEdit::OnTextEditing("");
	EXPECT_EQ(edit.GetString(), "");

	edit.KillFocus();
}

TEST_F(UIEditTextInputTest, PasswordStyleMasksDisplayOnly)
{
	TestEdit edit;
	edit.Init(nullptr);
	edit.BuildTextBuffer();
	edit.SetStyle(UISTYLE_EDIT_PASSWORD);
	edit.SetFocus();

	CN3UIEdit::OnTextInput("secret");

	EXPECT_EQ(edit.GetString(), "secret");                // logical buffer
	EXPECT_EQ(edit.m_pDisplay->GetString(), "******");    // displayed buffer

	CN3UIEdit::OnKeyDown(DIK_BACK);
	EXPECT_EQ(edit.GetString(), "secre");
	EXPECT_EQ(edit.m_pDisplay->GetString(), "*****");

	edit.KillFocus();
}

TEST_F(UIEditTextInputTest, MaxLengthClampsAtCharacterBoundary)
{
	const std::string szGa = Utf8ToCp949("가");

	TestEdit edit;
	edit.Init(nullptr);
	edit.BuildTextBuffer();
	edit.SetMaxString(3);
	edit.SetFocus();

	CN3UIEdit::OnTextInput("ab");
	CN3UIEdit::OnTextInput("cd"); // only 'c' fits
	EXPECT_EQ(edit.GetString(), "abc");

	// A DBCS pair never gets split to squeeze into the last byte.
	CN3UIEdit::OnKeyDown(DIK_BACK); // "ab", 1 byte free
	CN3UIEdit::OnTextInput("가");
	EXPECT_EQ(edit.GetString(), "ab");

	CN3UIEdit::OnKeyDown(DIK_BACK); // "a", 2 bytes free
	CN3UIEdit::OnTextInput("가");
	EXPECT_EQ(edit.GetString(), "a" + szGa);

	edit.KillFocus();
}

TEST_F(UIEditTextInputTest, EnterNotifiesParentAndHooksFollowFocus)
{
	MessageSink parent;
	parent.Init(nullptr);

	TestEdit edit;
	edit.Init(&parent);
	edit.BuildTextBuffer();

	EXPECT_EQ(s_iFocusGained, 0);
	edit.SetFocus();
	EXPECT_EQ(s_iFocusGained, 1);
	EXPECT_EQ(s_iFocusLost, 0);

	EXPECT_TRUE(CN3UIEdit::OnKeyDown(DIK_RETURN));
	EXPECT_EQ(parent.m_dwLastMsg, static_cast<uint32_t>(UIMSG_EDIT_RETURN));
	EXPECT_EQ(parent.m_iMsgCount, 1);

	edit.KillFocus();
	EXPECT_EQ(s_iFocusLost, 1);

	// No focused edit: keys and text fall through.
	EXPECT_FALSE(CN3UIEdit::OnKeyDown(DIK_BACK));
	CN3UIEdit::OnTextInput("ignored");
	EXPECT_EQ(edit.GetString(), "");
}
