#include <epk2.h>

EVP_PKEY *_gpPubKey;
AES_KEY _gdKeyImage, _geKeyImage;
const int MAX_PAK_CHUNK_SIZE = 0x400000;
const char PEM_FILE[] = "general_pub.pem";
const int PAK_ID_LENGTH = 5;

const char* pak_type_names[] = { stringify( BOOT ), stringify( MTDI ),
		stringify( CRC3 ), stringify( ROOT ), stringify( LGIN ),
		stringify( MODE ), stringify( KERN ), stringify( LGAP ),
		stringify( LGRE ), stringify( LGFO ), stringify( ADDO ),
		stringify( ECZA ), stringify( RECD ), stringify( MICO ),
		stringify( SPIB ), stringify( SYST ), stringify( USER ),
		stringify( NETF ), stringify( IDFI ), stringify( LOGO ),
		stringify( OPEN ), stringify( YWED ), stringify( CMND ),
		stringify( NVRA ), stringify( PREL ), stringify( KIDS ),
		stringify( STOR ), stringify( CERT ), stringify( AUTH ),
		stringify( ESTR ), stringify( GAME ), stringify( BROW ),
		stringify( CE_F ), stringify( ASIG ), stringify( RESE ),
		stringify( EPAK ), stringify( UNKNOWN ) };

struct pak_header_t* getPakHeader(unsigned char *buff) {
	return (struct pak_header_t *) buff;
}
;

void SWU_CryptoInit() {
	OpenSSL_add_all_digests();

	ERR_load_CRYPTO_strings();

	FILE *pubKeyFile = fopen(PEM_FILE, "r");

	if (pubKeyFile == NULL) {
		printf("error: can't open PEM file %s from current directory.\n",
				PEM_FILE);
		exit(1);
	}

	_gpPubKey = PEM_read_PUBKEY(pubKeyFile, NULL, NULL, NULL);

	fclose(pubKeyFile);

	ERR_clear_error();

	unsigned char AES_KEY[16] = { 0x2f, 0x2e, 0x2d, 0x2c, 0x2b, 0x2a, 0x29,
			0x28, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11, 0x10 };

	int size = 0x80;

	AES_set_decrypt_key(AES_KEY, size, &_gdKeyImage);

	AES_set_encrypt_key(AES_KEY, size, &_geKeyImage);
}

int _verifyImage(unsigned char *signature, unsigned int sig_len,
		unsigned char *image, unsigned int image_len) {

	return verifyImage(_gpPubKey, signature, sig_len, image, image_len);
}

/**
 * returns 1 on success and 0 otherwise
 */
int API_SWU_VerifyImage(unsigned char* buffer, unsigned int buflen) {

	return verifyImage(_gpPubKey, buffer, SIGNATURE_SIZE, buffer
			+ SIGNATURE_SIZE, buflen - SIGNATURE_SIZE);
}

void decryptImage(unsigned char* srcaddr, unsigned int len,
		unsigned char* dstaddr) {

	unsigned int remaining = len;

	unsigned int decrypted = 0;
	while (remaining >= AES_BLOCK_SIZE) {
		AES_decrypt(srcaddr, dstaddr, &_gdKeyImage);
		srcaddr += AES_BLOCK_SIZE;
		dstaddr += AES_BLOCK_SIZE;
		remaining -= AES_BLOCK_SIZE;
		decrypted++;
	}

	if (remaining != 0) {
		decrypted = decrypted * AES_BLOCK_SIZE;
		memcpy(dstaddr, srcaddr, remaining);
	}
}

void encryptImage(unsigned char* srcaddr, unsigned int len,
		unsigned char* dstaddr) {

	unsigned int remaining = len;

	while (remaining >= AES_BLOCK_SIZE) {

		AES_encrypt(srcaddr, dstaddr, &_gdKeyImage);
		srcaddr += AES_BLOCK_SIZE;
		dstaddr += AES_BLOCK_SIZE;
		remaining -= AES_BLOCK_SIZE;
	}
}

void API_SWU_DecryptImage(unsigned char* source, unsigned int len,
		unsigned char* destination) {

	unsigned char *srcaddr = source + SIGNATURE_SIZE;

	unsigned char *dstaddr = destination;

	unsigned int remaining = len - SIGNATURE_SIZE;

	decryptImage(srcaddr, remaining, dstaddr);
}

const char* getPakName(unsigned int pakType) {
	const char *pak_type_name = pak_type_names[pakType];

	char *result = malloc(PAK_ID_LENGTH);

	result[0] = tolower(pak_type_name[0]);
	result[1] = tolower(pak_type_name[1]);
	result[2] = tolower(pak_type_name[2]);
	result[3] = tolower(pak_type_name[3]);
	result[4] = 0;

	return result;
}



int verifyImage(EVP_PKEY *key, unsigned char *signature, unsigned int sig_len,
		unsigned char *image, unsigned int image_len) {

	unsigned char *md_value;
	unsigned int md_len = 0;

	const EVP_MD *sha1Digest = EVP_get_digestbyname("sha1");

	md_value = malloc(0x40);

	EVP_MD_CTX ctx1, ctx2;

	EVP_DigestInit(&ctx1, sha1Digest);

	EVP_DigestUpdate(&ctx1, image, image_len);

	EVP_DigestFinal(&ctx1, md_value, &md_len);

	EVP_DigestInit(&ctx2, EVP_sha1());

	EVP_DigestUpdate(&ctx2, md_value, md_len);

	int result = EVP_VerifyFinal(&ctx2, signature, sig_len, key);

	EVP_MD_CTX_cleanup(&ctx1);

	EVP_MD_CTX_cleanup(&ctx2);

	free(md_value);

	return result;

}

pak_type_t SWU_UTIL_GetPakType(unsigned char* buffer) {

	return convertToPakType(buffer);
}

int SWU_Util_GetFileType(unsigned char* buffer) {
	int pakType = SWU_UTIL_GetPakType(buffer);

	pakType = pakType ^ 0x42;

	return pakType;
}



struct epk2_header_t *getEPakHeader(unsigned char *buffer) {
	return (struct epk2_header_t*) (buffer);
}

void printEPakHeader(struct epk2_header_t *epakHeader) {
	printf("firmware format: %.*s\n", 4, epakHeader->_04_fw_format);
	printf("firmware type: %s\n", epakHeader->_06_fw_type);
	printf("firmware version: %02x.%02x.%02x.%02x\n",
			epakHeader->_05_fw_version[3], epakHeader->_05_fw_version[2],
			epakHeader->_05_fw_version[1], epakHeader->_05_fw_version[0]);
	printf("contained mtd images: %d\n", epakHeader->_03_pak_count);
	printf("images size: %d\n\n", epakHeader->_02_file_size);
}

void printPakInfo(struct pak_t* pak) {
	printf("pak '%s' contains %d chunk(s).\n", getPakName(pak->type),
			pak->chunk_count);

	int pak_chunk_index = 0;
	for (pak_chunk_index = 0; pak_chunk_index < pak->chunk_count; pak_chunk_index++) {
		struct pak_chunk_t *pak_chunk = pak->chunks[pak_chunk_index];

		unsigned char *decrypted = malloc(AES_BLOCK_SIZE);

		decryptImage(pak_chunk->header->_01_type_code, AES_BLOCK_SIZE,
				decrypted);

		pak_type_t pak_type = convertToPakType(decrypted);

		printf("  chunk #%u ('%.*s') contains %u bytes\n", pak_chunk_index + 1,
				4, getPakName(pak_type), pak_chunk->content_len);

		free(decrypted);
	}
}


void scanPAKs(struct epk2_header_t *epak_header, struct pak_t **pak_array) {

	unsigned char *epak_offset = epak_header->_00_signature;

	unsigned char *pak_header_offset = epak_offset
			+ sizeof(struct epk2_header_t);

	struct pak_chunk_header_t *pak_chunk_header =
			(struct pak_chunk_header_t*) ((epak_header->_01_type_code)
					+ (epak_header->_07_header_length));

	// it contains the added lengths of signature data
	unsigned int signature_sum = sizeof(epak_header->_00_signature)
			+ sizeof(pak_chunk_header->_00_signature);

	unsigned int pak_chunk_signature_length =
			sizeof(pak_chunk_header->_00_signature);

	int count = 0;

	int current_pak_length = -1;
	while (count < epak_header->_03_pak_count) {
		struct pak_header_t *pak_header = getPakHeader(pak_header_offset);

		pak_type_t pak_type = convertToPakType(pak_header->_00_type_code);

		struct pak_t *pak = malloc(sizeof(struct pak_t));

		pak_array[count] = pak;

		pak->type = pak_type;
		pak->header = pak_header;
		pak->chunk_count = 0;
		pak->chunks = NULL;

		int verified = 0;

		struct pak_chunk_header_t *next_pak_offset =
				(struct pak_chunk_header_t*) (epak_offset
						+ pak_header->_03_next_pak_file_offset + signature_sum);

		unsigned int distance_between_paks =
				((int) next_pak_offset->_01_type_code)
						- ((int) pak_chunk_header->_01_type_code);

		// last contained pak...
		if ((count == (epak_header->_03_pak_count - 1))) {
			distance_between_paks = current_pak_length
					+ pak_chunk_signature_length;
		}

		unsigned int max_distance = MAX_PAK_CHUNK_SIZE
				+ sizeof(struct pak_chunk_header_t);

		while (verified != 1) {

			unsigned int pak_chunk_length = distance_between_paks;

			bool is_next_chunk_needed = FALSE;

			if (pak_chunk_length > max_distance) {
				pak_chunk_length = max_distance;
				is_next_chunk_needed = TRUE;
			}

			unsigned int signed_length = current_pak_length;

			if (signed_length > max_distance) {
				signed_length = max_distance;
			}

			if (current_pak_length < 0) {
				signed_length = pak_chunk_length;
			}

			if ((verified = API_SWU_VerifyImage(
					pak_chunk_header->_00_signature, signed_length)) != 1) {
				printf(
						"verify pak chunk #%u of %s failed (size=0x%x). trying fallback...\n",
						pak->chunk_count + 1, getPakName(pak->type),
						signed_length);

				//hexdump(pak_chunk_header->_01_type_code, 0x80);

				while (((verified = API_SWU_VerifyImage(
						pak_chunk_header->_00_signature, signed_length)) != 1)
						&& (signed_length > 0)) {
					signed_length--;
					//printf(	"probe with size: 0x%x\n", signed_length);
				}

				if (verified) {
					printf("successfull verified with size: 0x%x\n",
							signed_length);
				} else {
					printf("fallback failed. sorry, aborting now.\n");
					exit(1);
				}
			}

			// sum signature lengths
			signature_sum += pak_chunk_signature_length;

			unsigned int pak_chunk_content_length = (pak_chunk_length
					- pak_chunk_signature_length);

			if (is_next_chunk_needed) {
				distance_between_paks -= pak_chunk_content_length;
				current_pak_length -= pak_chunk_content_length;
				verified = 0;

			} else {
				current_pak_length = pak_header->_04_next_pak_length
						+ pak_chunk_signature_length;
			}

			pak->chunk_count++;

			pak->chunks = realloc(pak->chunks, pak->chunk_count
					* sizeof(struct pak_chunk_t*));

			struct pak_chunk_t *pak_chunk = malloc(sizeof(struct pak_chunk_t));

			pak_chunk->header = pak_chunk_header;
			pak_chunk->content = pak_chunk_header->_04_unknown3
					+ sizeof(pak_chunk_header->_04_unknown3);

			pak_chunk->content_len = signed_length
					- sizeof(struct pak_chunk_header_t);

			pak->chunks[pak->chunk_count - 1] = pak_chunk;

			// move pointer to the next pak chunk offset
			pak_chunk_header
					= (struct pak_chunk_header_t *) (pak_chunk_header->_00_signature
							+ pak_chunk_length);
		}

		pak_header_offset += sizeof(struct pak_header_t);

		count++;
	}
}

