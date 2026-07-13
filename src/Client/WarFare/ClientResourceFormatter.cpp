#include "StdAfx.h"
#include "ClientResourceFormatter.h"
#include "GameBase.h"

// Owns the text-resource table (Data\Texts_us.tbl). Defined here rather than in
// GameBase.cpp so the IDS_* formatting path links on POSIX without pulling in
// the rest of CGameBase (docs/PORT_POSIX_PLAN.md, T6.4).
CN3TableBase<__TABLE_TEXTS> CGameBase::s_pTbl_Texts;

bool fmt::resource_helper::get_from_texts(uint32_t resourceId, std::string& fmtStr)
{
	__TABLE_TEXTS* text = CGameBase::s_pTbl_Texts.Find(resourceId);
	if (text == nullptr)
	{
#if defined(_N3GAME)
		CLogWriter::Write("get_from_texts({}) failed - resource missing in Texts TBL.", resourceId);
#endif

		return false;
	}

	fmtStr = text->szText;
	return true;
}
