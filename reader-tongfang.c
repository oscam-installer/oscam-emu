#include "globals.h"
#ifdef READER_TONGFANG
#include "reader-common.h"
#include "cscrypt/des.h"
#include <time.h>

// returns 1 if cw_is_valid, returns 0 if cw is all zeros
static int32_t cw_is_valid(uint8_t *cw)
{
	int32_t i;

	for(i = 0; i < 8; i++)
	{
		if(cw[i] != 0) // test if cw = 00
		{
			return OK;
		}
	}
	return ERROR;
}

static int32_t tongfang_read_data(struct s_reader *reader, uint8_t size, uint8_t *cta_res, uint16_t *status)
{
	uint8_t read_data_cmd[] = {0x00, 0xc0, 0x00, 0x00, 0xff};
	uint16_t cta_lr;

	read_data_cmd[4] = size;
	write_cmd(read_data_cmd, NULL);

	*status = (cta_res[cta_lr - 2] << 8) | cta_res[cta_lr - 1];

	return (cta_lr - 2);
}

static int32_t tongfang_card_init(struct s_reader *reader, ATR *newatr)
{
	const uint8_t get_ppua_cmdv1[] = {0x00, 0xa4, 0x04, 0x00, 0x05, 0xf9, 0x5a, 0x54, 0x00, 0x06};
	const uint8_t get_ppua_cmdv3[] = {0x80, 0x46, 0x00, 0x00, 0x04, 0x07, 0x00, 0x00, 0x08};
	uint8_t get_serial_cmdv1[] = {0x80, 0x32, 0x00, 0x00, 0x58};
	uint8_t get_serial_cmdv2[] = {0x80, 0x46, 0x00, 0x00, 0x04, 0x01, 0x00, 0x00, 0x04};
	uint8_t get_serial_cmdv3[] = {0x80, 0x46, 0x00, 0x00, 0x04, 0x01, 0x00, 0x00, 0x14};
	uint8_t get_commkey_cmd[17] = {0x80, 0x56, 0x00, 0x00, 0x0c};
	uint8_t confirm_commkey_cmd[21] = {0x80, 0x4c, 0x00, 0x00, 0x10};
	uint8_t pairing_cmd[9] = {0x80, 0x4c, 0x00, 0x00, 0x04, 0xFF, 0xFF, 0xFF, 0xFF};

	uint8_t data[257];
	uint8_t card_id[20];
	uint16_t status = 0;
	uint8_t boxID[] = {0xFF, 0xFF, 0xFF, 0xFF};
	uint8_t zero[8] = {0};
	uint8_t deskey[8];
	int32_t i;
	uint32_t calibsn = 0;
	int8_t readsize = 0;

	get_atr;
	def_resp;

	if(atr_size == 8 && atr[0] == 0x3B && atr[1] == 0x64)
	{
		reader->tongfang_version = 1;
	}
	else if(atr_size > 9 && atr[0] == 0x3B && (atr[1] & 0xF0) == 0x60 && 0 == memcmp(atr + 4, "NTIC", 4))
	{
		reader->tongfang_version = atr[8] - 0x30 + 1;
	}
	else if((atr_size == ((uint32_t)(atr[1] & 0x0F) + 4)) && (atr[0] == 0x3B) && ((atr[1] & 0xF0) == 0x60) && (atr[2] == 0x00) && ((atr[3] & 0xF0) == 0x00))
	{
		reader->tongfang_version = 2;
	}
	else
	{
		return ERROR; //not yxsb/yxtf
	}

	uint32_t cas_version = reader->tongfang_version & 0x00FFFFL;

	reader->caid = 0x4A02;
	reader->nprov = 1;
	memset(reader->prid, 0x00, sizeof(reader->prid));
	memset(card_id, 0, sizeof(card_id));
	memset(reader->hexserial, 0, 8);

	if(reader->tongfang_boxid > 0)
	{
		for(i = 0; (size_t)i < sizeof(boxID); i++)
		{
			boxID[i] = (reader->tongfang_boxid >> (8 * (3 - i))) % 0x100;
		}
	}

	if(cas_version <= 2)
	{	//tongfang 1-2
		write_cmd(get_ppua_cmdv1, get_ppua_cmdv1 + 5);
		if((cta_res[cta_lr - 2] != 0x90) || (cta_res[cta_lr - 1] != 0x00))
		{
			return ERROR;
		}
		rdr_log(reader, "Tongfang %d card detected", cas_version);

		//get card serial
		if(atr[8] == 0x31)
		{	//degrade card from version 3
			write_cmd(get_serial_cmdv2, get_serial_cmdv2 + 5);
			if((cta_res[cta_lr - 2] & 0xf0) != 0x60)
			{
				rdr_log(reader, "error: get card serial failed.");
				return ERROR;
			}
			readsize = cta_res[cta_lr - 1];
			if(readsize != tongfang_read_data(reader, readsize, data, &status) || status != 0x9000)
			{
				rdr_log(reader, "error: card get serial data failed.");
				return ERROR;
			}
			memcpy(reader->hexserial + 2, data, 4);
		}
		else
		{
			write_cmd(get_serial_cmdv1, get_serial_cmdv1 + 5);
			if((cta_res[cta_lr - 2] & 0xf0) != 0x60)
			{
				rdr_log(reader, "error: get card serial failed.");
				return ERROR;
			}
			readsize = cta_res[cta_lr - 1];
			if(readsize != tongfang_read_data(reader, readsize, data, &status) || status != 0x9000)
			{
				rdr_log(reader, "error: card get serial data failed.");
				return ERROR;
			}
			memcpy(reader->hexserial + 2, cta_res, 4);
		}

		// check pairing
		write_cmd(pairing_cmd, pairing_cmd + 5);
		if((cta_res[cta_lr - 2] == 0x94) && (cta_res[cta_lr - 1] == 0xB1))
		{
			rdr_log_dbg(reader, D_READER, "the card needlessly pairing with any box.");
		}
		else if((cta_res[cta_lr - 2] == 0x94) && (cta_res[cta_lr - 1] == 0xB2))
		{
			if(reader->tongfang_boxid > 0)
			{
				memcpy(pairing_cmd + 5, boxID, 4);
				write_cmd(pairing_cmd, pairing_cmd + 5);

				if((cta_res[cta_lr - 2] != 0x90) || (cta_res[cta_lr - 1] != 0x00))
				{
					rdr_log(reader, "error: this card pairing failed with the box, please check your boxid setting.");
					//return ERROR;
				}
			}
			else
			{
				rdr_log(reader, "warning: the card pairing with some box.");
				//return ERROR;
			}
		}
		else
		{
			rdr_log(reader, "error: this card pairing failed with the box(return code:0x%02X%02X).", cta_res[cta_lr - 2], cta_res[cta_lr - 1]);
		}

	}
	else if(cas_version == 3)
	{	//tongfang 3
		write_cmd(get_ppua_cmdv3, get_ppua_cmdv3 + 5);
		if((cta_res[cta_lr - 2] & 0xf0) != 0x60)
		{
			return ERROR;
		}
		readsize = cta_res[cta_lr - 1];
		if(readsize != tongfang_read_data(reader, readsize, data, &status))
		{
			rdr_log(reader, "error: get ppua v3 failed.");
			return ERROR;
		}

		rdr_log(reader, "Tongfang3 card detected");

		// get commkey
		if(!reader->tongfang3_deskey_length)
		{
			rdr_log(reader, "error: tongfang3_deskey must be configured.");
			return ERROR;
		}
		else
		{
			memcpy(deskey, reader->tongfang3_deskey, sizeof(reader->tongfang3_deskey));
		}
		memcpy(data, zero, sizeof(zero));
		des_ecb_encrypt(data, deskey, 8);
		memcpy(get_commkey_cmd + 5, data, 8);
		if(reader->tongfang3_calibsn > 0)
		{
			calibsn = reader->tongfang3_calibsn;
		}
		else
		{
			rdr_log(reader, "error: tongfang3_calibsn must be configured.");
			return ERROR;
		}
		get_commkey_cmd[5 + 8] = (calibsn >> 24) & 0xFF;
		get_commkey_cmd[5 + 8 + 1] = (calibsn >> 16) & 0xFF;
		get_commkey_cmd[5 + 8 + 2] = (calibsn >> 8) & 0xFF;
		get_commkey_cmd[5 + 8 + 3] = (calibsn) & 0xFF;
		write_cmd(get_commkey_cmd, get_commkey_cmd + 5);
		if((cta_res[cta_lr - 2] & 0xf0) != 0x60)
		{
			rdr_log(reader,"error: get card commkey failed.");
			return ERROR;
		}
		readsize = cta_res[cta_lr - 1];
		if(readsize != tongfang_read_data(reader, readsize, data, &status))
		{
			rdr_log(reader, "error: get card seed failed.");
			return ERROR;
		}
		//rdr_log(reader, "card seed got.");
		memcpy(reader->tongfang3_commkey, data, 8);
		des_ecb_encrypt(reader->tongfang3_commkey, deskey, 8);

		rdr_log_dbg(reader, D_READER, "card commkey got(%llX)",(unsigned long long)b2ll(8,reader->tongfang3_commkey));

		//get card serial
		write_cmd(get_serial_cmdv3, get_serial_cmdv3 + 5);
		if((cta_res[cta_lr - 2] & 0xf0) != 0x60)
		{
			rdr_log(reader, "error: get card serial failed.");
			return ERROR;
		}
		readsize = cta_res[cta_lr - 1];
		if(readsize != tongfang_read_data(reader, readsize, data, &status) || status != 0x9000)
		{
			rdr_log(reader, "error: card get serial failed.");
			return ERROR;
		}
		//rdr_log(reader, "card serial got.");

		memset(reader->hexserial, 0, 8);
		memcpy(reader->hexserial + 2, data, 4); // might be incorrect offset

		memcpy(card_id, data + 4, (readsize - 4) > ((int32_t)sizeof(card_id) - 1) ? (int32_t)sizeof(card_id) - 1 : readsize - 5);
		card_id[sizeof(card_id) - 1] = '\0';

		//confirm commkey and pairing
		memcpy(data, reader->stbid, sizeof(reader->stbid));
		des_ecb_encrypt(data, reader->tongfang3_commkey, 8);

		if(reader->tongfang_boxid > 0)
		{
			memcpy(zero + 2, boxID, 4);
		}
		memcpy(data + 8, zero, 8);
		des_ecb_encrypt(data + 8, reader->tongfang3_commkey, 8);

		memcpy(confirm_commkey_cmd + 5, data, 16);
		write_cmd(confirm_commkey_cmd, confirm_commkey_cmd + 5);

		if((cta_res[cta_lr - 2] & 0xf0) == 0x60)
		{
			readsize = cta_res[cta_lr - 1];
			if(readsize != tongfang_read_data(reader, readsize, data, &status))
			{
				rdr_log(reader, "error: confirm commkey failed(read response data failed).");
			}

			if(data[0] == 0x90 && data[1] == 0x00)
			{
				rdr_log_dbg(reader, D_READER, "the card pairing with any box succeed.");
			}
			else if(data[0] == 0x94 && data[1] == 0xB1)
			{
				rdr_log_dbg(reader, D_READER, "the card needlessly pairing with any box");
			}
			else if (data[0] == 0x94 && data[1] == 0xB2)
			{
				rdr_log(reader, "error: this card pairing failed with the box, please check your boxid setting.");
			}
		}
		else
		{
			if(cta_res[cta_lr - 2] == 0x90 && cta_res[cta_lr - 1] == 0x00)
			{
				rdr_log_dbg(reader, D_READER, "the card pairing with any box succeed.");
			}
			else if(cta_res[cta_lr - 2] == 0x94 && cta_res[cta_lr - 1] == 0xB1)
			{
				rdr_log_dbg(reader, D_READER, "the card needlessly pairing with any box");
			}
			else if(cta_res[cta_lr - 2] == 0x94 && cta_res[cta_lr - 1] == 0xB2)
			{
				rdr_log(reader, "error: this card pairing failed with the box, please check your boxid setting.");
			}
			else if(cta_res[cta_lr - 2] == 0x94 && cta_res[cta_lr - 1] == 0xB4)
			{
				rdr_log(reader, "error: this card initializing failed, please check your deskey setting, calibsn setting, and verify that stbid starts with 0000 or 0100.");
			}
			else
			{
				rdr_log(reader, "error: confirm commkey failed(return code:0x%02X%02X).", cta_res[cta_lr - 2], cta_res[cta_lr - 1]);
			}
		}
	}
	else
	{
		rdr_log(reader, "error: NTIC%c card not support yet!", atr[8]);
		return ERROR;
	}

	rdr_log_sensitive(reader, "type: Tongfang, caid: %04X, serial: {%llu}, hex serial: {%llX}, Card ID: {%s}, BoxID: {%08X}", reader->caid, (unsigned long long) b2ll(6, reader->hexserial), (unsigned long long) b2ll(4, reader->hexserial + 2), card_id, b2i(4, boxID));

	return OK;
}

/*
Example ecm:
03 85 80 70 61 8E 2A 16 4F 00 12 0F 21 5A E5 6A
8F 4D C1 57 4E 24 2A 38 3C 26 8A 4C C2 74 A1 23
9F 12 43 80 3A 16 4F 3E 8E 2A C0 40 0F 22 94 E4
6A 89 F1 09 38 8F DF 3D 08 A6 29 1A 61 98 31 82
7F 34 55 74 0E A3 54 38 01 09 00 01 00 01 D9 31
A5 1B 8B CA A8 95 E0 D1 24 7D 36 8C F6 89 4A F7
B2 3A 74 3D D1 D4

Example ecm:
81 70 76 22 91 14 96 01 0C 17 C4 00 12 09 5A 00
98 80 B0 D8 65 32 1B 26 03 F0 21 3B 8C 07 15 12
58 80 3A 14 96 53 22 91 C0 04 17 C5 61 C0 FF 3A
D9 3C EE 51 CD 6E 70 A2 EC 71 FF 0F D6 E8 52 D6
69 C2 7F 07 0F 83 02 09 00 01 00 01 B5 AC C0 8D
7A B0 65
*/
static int32_t tongfang_do_ecm(struct s_reader *reader, const ECM_REQUEST *er, struct s_ecm_answer *ea)
{
	uint8_t ecm_buf[512];	//{0x80,0x3a,0x00,0x01,0x53};
	uint8_t *ecm_cmd = ecm_buf;
	int32_t ecm_len = 0;
	uint8_t data[256] = {0};
	char *tmp;
	int32_t i = 0;
	size_t write_len = 0;
	size_t read_size = 0;
	size_t data_len = 0;
	uint16_t status = 0;

	uint32_t cas_version = reader->tongfang_version & 0x00FFFFL;

	def_resp;

	if(cs_malloc(&tmp, er->ecmlen * 3 + 1))
	{
		rdr_log_dbg(reader, D_IFD, "ECM: %s", cs_hexdump(1, er->ecm, er->ecmlen, tmp, er->ecmlen * 3 + 1));
		NULLFREE(tmp);
	}

	if((ecm_len = check_sct_len(er->ecm, 3)) < 0)
	{
		rdr_log(reader, "error: check_sct_len failed, smartcard section too long %d > %zd", SCT_LEN(er->ecm), sizeof(er->ecm) - 3);
		return ERROR;
	}

	for(i = 0; i < ecm_len; i++)
	{
		if ((i < (ecm_len - 1)) && (er->ecm[i] == 0x80) && (er->ecm[i + 1] == 0x3a) && (er->ecm[i + 2] == er->ecm[5]) && (er->ecm[i + 3] == er->ecm[6]))
		{
			break;
		}
	}
	if(i == ecm_len)
	{
		rdr_log(reader, "error: invalid ecm data...");
		return ERROR;
	}

	write_len = er->ecm[i + 4] + 5;
	if(write_len > (sizeof(ecm_buf)))
	{
		if(write_len > MAX_ECM_SIZE || !cs_malloc(&ecm_cmd,write_len))
		{
			rdr_log(reader, "error: ecm data too long,longer than sizeof ecm_buf(%zd > %zd).", write_len, sizeof(ecm_cmd));
			return ERROR;
		}
	}

	memcpy(ecm_cmd, er->ecm + i, write_len);
	write_cmd(ecm_cmd, ecm_cmd + 5);
	if((cta_lr - 2) >= 2)
	{
		read_size = cta_res[1];
	}
	else
	{
		if((cta_res[cta_lr - 2] & 0xf0) == 0x60)
		{
			read_size = cta_res[cta_lr - 1];
		}
		else
		{
			char ecm_cmd_string[150];
			rdr_log(reader, "error: card send parsing ecm command failed!(%s)", cs_hexdump(1, ecm_cmd, write_len, ecm_cmd_string, sizeof(ecm_cmd_string)));
			if(ecm_cmd != ecm_buf)
			{
				NULLFREE(ecm_cmd);
			}
			return ERROR;
		}
	}

	if(ecm_cmd != ecm_buf)
	{
		NULLFREE(ecm_cmd);
	}

	if(read_size > sizeof(data))
	{
		rdr_log(reader, "error: read_size is bigger than sizeof data.(%zd>%zd)", read_size, sizeof(data));
		return ERROR;
	}

	data_len = tongfang_read_data(reader, read_size, data, &status);
	if(data_len < 23)
	{
		char ecm_string[256 * 3 + 1];
		rdr_log(reader, "error: card return cw data failed, return data len=%zd(ECM:%s).", data_len, cs_hexdump(1, er->ecm, er->ecmlen, ecm_string, sizeof(ecm_string)));
		return ERROR;
	}

	if(!(er->ecm[0] & 0x01))
	{
		memcpy(ea->cw, data + 8, 16);
	}
	else
	{
		memcpy(ea->cw, data + 16, 8);
		memcpy(ea->cw + 8, data + 8, 8);
	}

	// All zeroes is no valid CW, can be a result of wrong boxid
	if(!cw_is_valid(ea->cw) || !cw_is_valid(ea->cw + 8))
	{
		rdr_log(reader,"error: cw is invalid.");
		return ERROR;
	}

	if(cas_version == 3)
	{
		des_ecb_encrypt(ea->cw, reader->tongfang3_commkey, 8);
		des_ecb_encrypt(ea->cw + 8, reader->tongfang3_commkey, 8);
	}

	return OK;
}

static int32_t tongfang_do_emm(struct s_reader *reader, EMM_PACKET *ep)
{
	uint8_t emm_cmd[200];
	def_resp;
	int32_t write_len;

	if(SCT_LEN(ep->emm) < 8)
	{
		return ERROR;
	}

	write_len = ep->emm[15] + 5;
	memcpy(emm_cmd, ep->emm + 11, write_len);

	write_cmd(emm_cmd, emm_cmd + 5);

	return OK;
}

static int32_t tongfang_card_info(struct s_reader *reader)
{
	static const uint8_t get_provider_cmd[] = {0x80, 0x44, 0x00, 0x00, 0x08};
	uint8_t get_subscription_cmd[] = {0x80, 0x48, 0x00, 0x01, 0x04, 0x01, 0x00, 0x00, 0x13};
	static const uint8_t get_agegrade_cmd[] = {0x80, 0x46, 0x00, 0x00, 0x04, 0x03, 0x00, 0x00, 0x09};
	def_resp;
	int32_t i;
	uint8_t data[256];
	uint16_t status = 0;

	write_cmd(get_provider_cmd, NULL);
	if((cta_res[cta_lr - 2] != 0x90) || (cta_res[cta_lr - 1] != 0x00))
	{
		return ERROR;
	}

	reader->nprov = 0;
	memset(reader->prid, 0x00, sizeof(reader->prid));

	for(i = 0; i < 4; i++)
	{
		if(((cta_res[i * 2] != 0xFF) || (cta_res[i * 2 + 1] != 0xFF)) && ((cta_res[i * 2] != 0x00) || (cta_res[i * 2 + 1] != 0x00)))
		{
			int j;
			int found = 0;
			for(j = 0; j < reader->nprov; j++)
			{
				if(reader->nprov > 0 && reader->prid[j][2] == cta_res[i * 2] && reader->prid[j][3] == cta_res[i * 2 + 1])
				{
					found = 1;
					break;
				}
			}

			if(found == 1)
			{
				continue;
			}

			memcpy(&reader->prid[reader->nprov][2], cta_res + i * 2, 2);
			rdr_log(reader, "Provider:%06X", b2i(2, cta_res + i * 2));
			reader->nprov ++;
		}
	}

	cs_clear_entitlement(reader);

	for(i = 0; i < reader->nprov; i++)
	{
		get_subscription_cmd[2] = reader->prid[i][2];
		get_subscription_cmd[3] = reader->prid[i][3];
		write_cmd(get_subscription_cmd, get_subscription_cmd + 5);
		if((cta_res[cta_lr - 2] & 0xF0) != 0x60)
		{
			continue;
		}
		if((3 > tongfang_read_data(reader, cta_res[cta_lr - 1], data, &status)) || (status != 0x9000))
		{
			continue;
		}

		uint16_t count = data[2];
		int j;
		for(j = 0; j < count; j++)
		{
			if(!data[j * 13 + 4])
			{
				continue;
			}

			time_t start_t, end_t;
			//946656000L = 2000-01-01 00:00:00
			start_t = 946656000L + b2i(2, data + j * 13 + 8) * 24 * 3600L;
			end_t = 946656000L + b2i(2, data + j * 13 + 12) * 24 * 3600L;
			uint64_t product_id = b2i(2, data + j * 13 + 5);

			struct tm tm_start, tm_end;
			char start_day[11], end_day[11];

			localtime_r(&start_t, &tm_start);
			localtime_r(&end_t, &tm_end);
			strftime(start_day, sizeof(start_day), "%Y/%m/%d", &tm_start);
			strftime(end_day, sizeof(end_day), "%Y/%m/%d", &tm_end);

			if(!j)
			{
				rdr_log(reader, "entitlements for provider: %d (%04X:%06X)", i, reader->caid, b2i(2, &reader->prid[i][2]));
			}

			rdr_log(reader, "    chid: %04"PRIX64"  date: %s - %s", product_id, start_day, end_day);

			cs_add_entitlement(reader, reader->caid, b2i(2, &reader->prid[i][2]), product_id, 0, start_t, end_t, 4, 1);
		}
	}

	write_cmd(get_agegrade_cmd, get_agegrade_cmd + 5);
	if((cta_res[cta_lr - 2] & 0xF0) != 0x60)
	{
		return OK;
	}

	tongfang_read_data(reader, cta_res[cta_lr - 1], data, &status);
	if(status != 0x9000)
	{
		return OK;
	}

	rdr_log(reader, "AgeGrade:%d", (data[0] + 3));

	return OK;
}

static int32_t tongfang_get_emm_type(EMM_PACKET *ep, struct s_reader *rdr)
{
	switch(ep->emm[0])
	{
		case 0x82:
			ep->type = SHARED;
			memset(ep->hexserial, 0, 8);
			memcpy(ep->hexserial, ep->emm + 5, 3);
			return (!memcmp(rdr->hexserial + 2, ep->hexserial, 3));

		default:
			ep->type = UNKNOWN;
			return 1;
	}
}

static int32_t tongfang_get_emm_filter(struct s_reader *rdr, struct s_csystem_emm_filter **emm_filters, unsigned int *filter_count)
{
	if(*emm_filters == NULL)
	{
		const unsigned int max_filter_count = 1;
		if(!cs_malloc(emm_filters, max_filter_count * sizeof(struct s_csystem_emm_filter)))
		{
			return ERROR;
		}

		struct s_csystem_emm_filter *filters = *emm_filters;
		*filter_count = 0;

		int32_t idx = 0;

		filters[idx].type = EMM_SHARED;
		filters[idx].enabled = 1;
		filters[idx].filter[0] = 0x82;
		filters[idx].mask[0] = 0xFF;
		memcpy(&filters[idx].filter[3], rdr->hexserial + 2, 3);
		memset(&filters[idx].mask[3], 0xFF, 3);
		idx++;

		*filter_count = idx;
	}

	return OK;
}

const struct s_cardsystem reader_tongfang =
{
	.desc           = "tongfang",
	.caids          = (uint16_t[]){ 0x4A02, 0 },
	.do_emm         = tongfang_do_emm,
	.do_ecm         = tongfang_do_ecm,
	.card_info      = tongfang_card_info,
	.card_init      = tongfang_card_init,
	.get_emm_type   = tongfang_get_emm_type,
	.get_emm_filter = tongfang_get_emm_filter,
};

#endif
