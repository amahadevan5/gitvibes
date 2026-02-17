#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include "libvibes/vibes.h"
#include <sys/time.h>

/*
 * ULID (Universally Unique Lexicographically Sortable Identifier)
 *
 * Format: 10 chars timestamp (48-bit ms) + 16 chars random (80-bit)
 * Encoding: Crockford's Base32
 * Total: 26 characters
 */
static const char CROCKFORD_BASE32[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

int64_t vibes_timestamp_ms(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void vibes_ulid_generate(char out[VIBES_ULID_LEN])
{
	int64_t ts = vibes_timestamp_ms();
	unsigned char rand_bytes[10];
	int i;

	/* Read random bytes from /dev/urandom */
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd >= 0) {
		xread(fd, rand_bytes, sizeof(rand_bytes));
		close(fd);
	} else {
		/* Fallback: seed from timestamp + pid */
		srand((unsigned)(ts ^ getpid()));
		for (i = 0; i < 10; i++)
			rand_bytes[i] = (unsigned char)(rand() & 0xff);
	}

	/* Encode timestamp (48 bits = 10 base32 chars, big-endian) */
	out[0] = CROCKFORD_BASE32[(ts >> 45) & 0x1f];
	out[1] = CROCKFORD_BASE32[(ts >> 40) & 0x1f];
	out[2] = CROCKFORD_BASE32[(ts >> 35) & 0x1f];
	out[3] = CROCKFORD_BASE32[(ts >> 30) & 0x1f];
	out[4] = CROCKFORD_BASE32[(ts >> 25) & 0x1f];
	out[5] = CROCKFORD_BASE32[(ts >> 20) & 0x1f];
	out[6] = CROCKFORD_BASE32[(ts >> 15) & 0x1f];
	out[7] = CROCKFORD_BASE32[(ts >> 10) & 0x1f];
	out[8] = CROCKFORD_BASE32[(ts >> 5) & 0x1f];
	out[9] = CROCKFORD_BASE32[ts & 0x1f];

	/* Encode 80 bits of randomness (16 base32 chars) */
	out[10] = CROCKFORD_BASE32[(rand_bytes[0] >> 3) & 0x1f];
	out[11] = CROCKFORD_BASE32[((rand_bytes[0] << 2) | (rand_bytes[1] >> 6)) & 0x1f];
	out[12] = CROCKFORD_BASE32[(rand_bytes[1] >> 1) & 0x1f];
	out[13] = CROCKFORD_BASE32[((rand_bytes[1] << 4) | (rand_bytes[2] >> 4)) & 0x1f];
	out[14] = CROCKFORD_BASE32[((rand_bytes[2] << 1) | (rand_bytes[3] >> 7)) & 0x1f];
	out[15] = CROCKFORD_BASE32[(rand_bytes[3] >> 2) & 0x1f];
	out[16] = CROCKFORD_BASE32[((rand_bytes[3] << 3) | (rand_bytes[4] >> 5)) & 0x1f];
	out[17] = CROCKFORD_BASE32[rand_bytes[4] & 0x1f];
	out[18] = CROCKFORD_BASE32[(rand_bytes[5] >> 3) & 0x1f];
	out[19] = CROCKFORD_BASE32[((rand_bytes[5] << 2) | (rand_bytes[6] >> 6)) & 0x1f];
	out[20] = CROCKFORD_BASE32[(rand_bytes[6] >> 1) & 0x1f];
	out[21] = CROCKFORD_BASE32[((rand_bytes[6] << 4) | (rand_bytes[7] >> 4)) & 0x1f];
	out[22] = CROCKFORD_BASE32[((rand_bytes[7] << 1) | (rand_bytes[8] >> 7)) & 0x1f];
	out[23] = CROCKFORD_BASE32[(rand_bytes[8] >> 2) & 0x1f];
	out[24] = CROCKFORD_BASE32[((rand_bytes[8] << 3) | (rand_bytes[9] >> 5)) & 0x1f];
	out[25] = CROCKFORD_BASE32[rand_bytes[9] & 0x1f];
	out[26] = '\0';
}

#endif /* VIBES_ENABLED */
