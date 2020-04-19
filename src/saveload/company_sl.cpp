/* $Id$ */

/** @file company_sl.cpp Code handling saving and loading of company data */

#include "../stdafx.h"
#include "../company_base.h"
#include "../company_func.h"
#include "../company_manager_face.h"

#include "saveload.h"

/**
 * Converts an old company manager's face format to the new company manager's face format
 *
 * Meaning of the bits in the old face (some bits are used in several times):
 * - 4 and 5: chin
 * - 6 to 9: eyebrows
 * - 10 to 13: nose
 * - 13 to 15: lips (also moustache for males)
 * - 16 to 19: hair
 * - 20 to 22: eye colour
 * - 20 to 27: tie, ear rings etc.
 * - 28 to 30: glasses
 * - 19, 26 and 27: race (bit 27 set and bit 19 equal to bit 26 = black, otherwise white)
 * - 31: gender (0 = male, 1 = female)
 *
 * @param face the face in the old format
 * @return the face in the new format
 */
CompanyManagerFace ConvertFromOldCompanyManagerFace(uint32 face)
{
	CompanyManagerFace cmf = 0;
	GenderEthnicity ge = GE_WM;

	if (HasBit(face, 31)) SetBit(ge, GENDER_FEMALE);
	if (HasBit(face, 27) && (HasBit(face, 26) == HasBit(face, 19))) SetBit(ge, ETHNICITY_BLACK);

	SetCompanyManagerFaceBits(cmf, CMFV_GEN_ETHN,    ge, ge);
	SetCompanyManagerFaceBits(cmf, CMFV_HAS_GLASSES, ge, GB(face, 28, 3) <= 1);
	SetCompanyManagerFaceBits(cmf, CMFV_EYE_COLOUR,  ge, HasBit(ge, ETHNICITY_BLACK) ? 0 : ClampU(GB(face, 20, 3), 5, 7) - 5);
	SetCompanyManagerFaceBits(cmf, CMFV_CHIN,        ge, ScaleCompanyManagerFaceValue(CMFV_CHIN,     ge, GB(face,  4, 2)));
	SetCompanyManagerFaceBits(cmf, CMFV_EYEBROWS,    ge, ScaleCompanyManagerFaceValue(CMFV_EYEBROWS, ge, GB(face,  6, 4)));
	SetCompanyManagerFaceBits(cmf, CMFV_HAIR,        ge, ScaleCompanyManagerFaceValue(CMFV_HAIR,     ge, GB(face, 16, 4)));
	SetCompanyManagerFaceBits(cmf, CMFV_JACKET,      ge, ScaleCompanyManagerFaceValue(CMFV_JACKET,   ge, GB(face, 20, 2)));
	SetCompanyManagerFaceBits(cmf, CMFV_COLLAR,      ge, ScaleCompanyManagerFaceValue(CMFV_COLLAR,   ge, GB(face, 22, 2)));
	SetCompanyManagerFaceBits(cmf, CMFV_GLASSES,     ge, GB(face, 28, 1));

	uint lips = GB(face, 10, 4);
	if (!HasBit(ge, GENDER_FEMALE) && lips < 4) {
		SetCompanyManagerFaceBits(cmf, CMFV_HAS_MOUSTACHE, ge, true);
		SetCompanyManagerFaceBits(cmf, CMFV_MOUSTACHE,     ge, max(lips, 1U) - 1);
	} else {
		if (!HasBit(ge, GENDER_FEMALE)) {
			lips = lips * 15 / 16;
			lips -= 3;
			if (HasBit(ge, ETHNICITY_BLACK) && lips > 8) lips = 0;
		} else {
			lips = ScaleCompanyManagerFaceValue(CMFV_LIPS, ge, lips);
		}
		SetCompanyManagerFaceBits(cmf, CMFV_LIPS, ge, lips);

		uint nose = GB(face, 13, 3);
		if (ge == GE_WF) {
			nose = (nose * 3 >> 3) * 3 >> 2; // There is 'hole' in the nose sprites for females
		} else {
			nose = ScaleCompanyManagerFaceValue(CMFV_NOSE, ge, nose);
		}
		SetCompanyManagerFaceBits(cmf, CMFV_NOSE, ge, nose);
	}

	uint tie_earring = GB(face, 24, 4);
	if (!HasBit(ge, GENDER_FEMALE) || tie_earring < 3) { // Not all females have an earring
		if (HasBit(ge, GENDER_FEMALE)) SetCompanyManagerFaceBits(cmf, CMFV_HAS_TIE_EARRING, ge, true);
		SetCompanyManagerFaceBits(cmf, CMFV_TIE_EARRING, ge, HasBit(ge, GENDER_FEMALE) ? tie_earring : ScaleCompanyManagerFaceValue(CMFV_TIE_EARRING, ge, tie_earring / 2));
	}

	return cmf;
}



/* Save/load of companies */
static const SaveLoad _company_desc[] = {
	    SLE_VAR(Company, name_2,          SLE_UINT32),
	    SLE_VAR(Company, name_1,          SLE_STRINGID),
	SLE_CONDSTR(Company, name,            SLE_STR, 0,                       84, SL_MAX_VERSION),

	    SLE_VAR(Company, president_name_1, SLE_UINT16),
	    SLE_VAR(Company, president_name_2, SLE_UINT32),
	SLE_CONDSTR(Company, president_name,  SLE_STR, 0,                       84, SL_MAX_VERSION),

	    SLE_VAR(Company, face,            SLE_UINT32),

	/* money was changed to a 64 bit field in savegame version 1. */
	SLE_CONDVAR(Company, money,                 SLE_VAR_I64 | SLE_FILE_I32,  0, 0),
	SLE_CONDVAR(Company, money,                 SLE_INT64,                   1, SL_MAX_VERSION),

	SLE_CONDVAR(Company, current_loan,          SLE_VAR_I64 | SLE_FILE_I32,  0, 64),
	SLE_CONDVAR(Company, current_loan,          SLE_INT64,                  65, SL_MAX_VERSION),

	    SLE_VAR(Company, colour,                SLE_UINT8),
	    SLE_VAR(Company, money_fraction,        SLE_UINT8),
	SLE_CONDVAR(Company, avail_railtypes,       SLE_UINT8,                   0, 57),
	    SLE_VAR(Company, block_preview,         SLE_UINT8),

	SLE_CONDVAR(Company, cargo_types,           SLE_FILE_U16 | SLE_VAR_U32,  0, 93),
	SLE_CONDVAR(Company, cargo_types,           SLE_UINT32,                 94, SL_MAX_VERSION),
	SLE_CONDVAR(Company, location_of_HQ,        SLE_FILE_U16 | SLE_VAR_U32,  0,  5),
	SLE_CONDVAR(Company, location_of_HQ,        SLE_UINT32,                  6, SL_MAX_VERSION),
	SLE_CONDVAR(Company, last_build_coordinate, SLE_FILE_U16 | SLE_VAR_U32,  0,  5),
	SLE_CONDVAR(Company, last_build_coordinate, SLE_UINT32,                  6, SL_MAX_VERSION),
	SLE_CONDVAR(Company, inaugurated_year,      SLE_FILE_U8  | SLE_VAR_I32,  0, 30),
	SLE_CONDVAR(Company, inaugurated_year,      SLE_INT32,                  31, SL_MAX_VERSION),

	    SLE_ARR(Company, share_owners,          SLE_UINT8, 4),

	    SLE_VAR(Company, num_valid_stat_ent,    SLE_UINT8),

	    SLE_VAR(Company, quarters_of_bankrupcy, SLE_UINT8),
	SLE_CONDVAR(Company, bankrupt_asked,        SLE_FILE_U8  | SLE_VAR_U16,  0, 103),
	SLE_CONDVAR(Company, bankrupt_asked,        SLE_UINT16,                104, SL_MAX_VERSION),
	    SLE_VAR(Company, bankrupt_timeout,      SLE_INT16),
	SLE_CONDVAR(Company, bankrupt_value,        SLE_VAR_I64 | SLE_FILE_I32,  0, 64),
	SLE_CONDVAR(Company, bankrupt_value,        SLE_INT64,                  65, SL_MAX_VERSION),

	/* yearly expenses was changed to 64-bit in savegame version 2. */
	SLE_CONDARR(Company, yearly_expenses,       SLE_FILE_I32 | SLE_VAR_I64, 3 * 13, 0, 1),
	SLE_CONDARR(Company, yearly_expenses,       SLE_INT64, 3 * 13,                  2, SL_MAX_VERSION),

	SLE_CONDVAR(Company, is_ai,                 SLE_BOOL,                    2, SL_MAX_VERSION),
	SLE_CONDNULL(1, 107, 111), ///< is_noai
	SLE_CONDNULL(1, 4, 99),

	/* Engine renewal settings */
	SLE_CONDNULL(512, 16, 18),
	SLE_CONDREF(Company, engine_renew_list,     REF_ENGINE_RENEWS,          19, SL_MAX_VERSION),
	SLE_CONDVAR(Company, engine_renew,          SLE_BOOL,                   16, SL_MAX_VERSION),
	SLE_CONDVAR(Company, engine_renew_months,   SLE_INT16,                  16, SL_MAX_VERSION),
	SLE_CONDVAR(Company, engine_renew_money,    SLE_UINT32,                 16, SL_MAX_VERSION),
	SLE_CONDVAR(Company, renew_keep_length,     SLE_BOOL,                    2, SL_MAX_VERSION), // added with 16.1, but was blank since 2

	/* Reserve extra space in savegame here. (currently 63 bytes) */
	SLE_CONDNULL(63, 2, SL_MAX_VERSION),

	SLE_END()
};

static const SaveLoad _company_economy_desc[] = {
	/* these were changed to 64-bit in savegame format 2 */
	SLE_CONDVAR(CompanyEconomyEntry, income,              SLE_FILE_I32 | SLE_VAR_I64, 0, 1),
	SLE_CONDVAR(CompanyEconomyEntry, income,              SLE_INT64,                  2, SL_MAX_VERSION),
	SLE_CONDVAR(CompanyEconomyEntry, expenses,            SLE_FILE_I32 | SLE_VAR_I64, 0, 1),
	SLE_CONDVAR(CompanyEconomyEntry, expenses,            SLE_INT64,                  2, SL_MAX_VERSION),
	SLE_CONDVAR(CompanyEconomyEntry, company_value,       SLE_FILE_I32 | SLE_VAR_I64, 0, 1),
	SLE_CONDVAR(CompanyEconomyEntry, company_value,       SLE_INT64,                  2, SL_MAX_VERSION),

	    SLE_VAR(CompanyEconomyEntry, delivered_cargo,     SLE_INT32),
	    SLE_VAR(CompanyEconomyEntry, performance_history, SLE_INT32),

	SLE_END()
};

/* We do need to read this single value, as the bigger it gets, the more data is stored */
struct CompanyOldAI {
	uint8 num_build_rec;
};

static const SaveLoad _company_ai_desc[] = {
	SLE_CONDNULL(2,  0, 106),
	SLE_CONDNULL(2,  0, 12),
	SLE_CONDNULL(4, 13, 106),
	SLE_CONDNULL(8,  0, 106),
	 SLE_CONDVAR(CompanyOldAI, num_build_rec, SLE_UINT8, 0, 106),
	SLE_CONDNULL(3,  0, 106),

	SLE_CONDNULL(2,  0,  5),
	SLE_CONDNULL(4,  6, 106),
	SLE_CONDNULL(2,  0,  5),
	SLE_CONDNULL(4,  6, 106),
	SLE_CONDNULL(2,  0, 106),

	SLE_CONDNULL(2,  0,  5),
	SLE_CONDNULL(4,  6, 106),
	SLE_CONDNULL(2,  0,  5),
	SLE_CONDNULL(4,  6, 106),
	SLE_CONDNULL(2,  0, 106),

	SLE_CONDNULL(2,  0, 68),
	SLE_CONDNULL(4,  69, 106),

	SLE_CONDNULL(18, 0, 106),
	SLE_CONDNULL(20, 0, 106),
	SLE_CONDNULL(32, 0, 106),

	SLE_CONDNULL(64, 2, 106),
	SLE_END()
};

static const SaveLoad _company_ai_build_rec_desc[] = {
	SLE_CONDNULL(2, 0, 5),
	SLE_CONDNULL(4, 6, 106),
	SLE_CONDNULL(2, 0, 5),
	SLE_CONDNULL(4, 6, 106),
	SLE_CONDNULL(8, 0, 106),
	SLE_END()
};

static const SaveLoad _company_livery_desc[] = {
	SLE_CONDVAR(Livery, in_use,  SLE_BOOL,  34, SL_MAX_VERSION),
	SLE_CONDVAR(Livery, colour1, SLE_UINT8, 34, SL_MAX_VERSION),
	SLE_CONDVAR(Livery, colour2, SLE_UINT8, 34, SL_MAX_VERSION),
	SLE_END()
};

static void SaveLoad_PLYR(Company *c)
{
	int i;

	SlObject(c, _company_desc);

	/* Keep backwards compatible for savegames, so load the old AI block */
	if (CheckSavegameVersion(107) && !IsHumanCompany(c->index)) {
		CompanyOldAI old_ai;
		char nothing;

		SlObject(&old_ai, _company_ai_desc);
		for (i = 0; i != old_ai.num_build_rec; i++) {
			SlObject(&nothing, _company_ai_build_rec_desc);
		}
	}

	/* Write economy */
	SlObject(&c->cur_economy, _company_economy_desc);

	/* Write old economy entries. */
	for (i = 0; i < c->num_valid_stat_ent; i++) {
		SlObject(&c->old_economy[i], _company_economy_desc);
	}

	/* Write each livery entry. */
	int num_liveries = CheckSavegameVersion(63) ? LS_END - 4 : (CheckSavegameVersion(85) ? LS_END - 2: LS_END);
	for (i = 0; i < num_liveries; i++) {
		SlObject(&c->livery[i], _company_livery_desc);
	}

	if (num_liveries < LS_END) {
		/* We want to insert some liveries somewhere in between. This means some have to be moved. */
		memmove(&c->livery[LS_FREIGHT_WAGON], &c->livery[LS_PASSENGER_WAGON_MONORAIL], (LS_END - LS_FREIGHT_WAGON) * sizeof(c->livery[0]));
		c->livery[LS_PASSENGER_WAGON_MONORAIL] = c->livery[LS_MONORAIL];
		c->livery[LS_PASSENGER_WAGON_MAGLEV]   = c->livery[LS_MAGLEV];
	}

	if (num_liveries == LS_END - 4) {
		/* Copy bus/truck liveries over to trams */
		c->livery[LS_PASSENGER_TRAM] = c->livery[LS_BUS];
		c->livery[LS_FREIGHT_TRAM]   = c->livery[LS_TRUCK];
	}
}

static void Save_PLYR()
{
	Company *c;
	FOR_ALL_COMPANIES(c) {
		SlSetArrayIndex(c->index);
		SlAutolength((AutolengthProc*)SaveLoad_PLYR, c);
	}
}

static void Load_PLYR()
{
	int index;
	while ((index = SlIterateArray()) != -1) {
		Company *c = new (index) Company();
		SaveLoad_PLYR(c);
		_company_colours[index] = (Colours)c->colour;
	}
}

extern const ChunkHandler _company_chunk_handlers[] = {
	{ 'PLYR', Save_PLYR, Load_PLYR, CH_ARRAY | CH_LAST},
};
