/* test_port - unit tests for cdaudio + datapath. Usage: test_port INSTALLDIR [GBL-CD.iso]
 * cdaudio: against a two-track tone rip the test writes itself (Track02.wav 440 Hz, Track03.wav 660 Hz, 3 s each);
 * datapath: against a Ghost Bear's Legacy install (MW2.PRJ, SND\DIG.INI) and the original GBL disc's image. */
#include "cdaudio.h"
#include "datapath.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static int fails;
#define ISO 2
#define CHECK(c, msg) do { if (c) printf("  ok   %s\n", msg); else { printf("  FAIL %s\n", msg); fails++; } } while (0)

/* dominant frequency of the left channel via zero crossings */
static double freq(const float *b, size_t frames)
{
    size_t i, z = 0;
    for (i = 1; i < frames; i++) if ((b[(i - 1) * 2] < 0) != (b[i * 2] < 0)) z++;
    return (double)z * CDA_SAMPLE_RATE / (2.0 * (double)frames);
}
static int mkdir_p(const char *d) { return mkdir(d, 0755) == 0 ? 0 : -1; }
static double peak(const float *b, size_t frames)
{
    double m = 0; size_t i;
    for (i = 0; i < frames * 2; i++) if (fabs(b[i]) > m) m = fabs(b[i]);
    return m;
}

/* a 3 s 16-bit stereo 44.1 kHz WAV of a sine at hz */
static int write_tone(const char *path, double hz)
{
    const uint32_t frames = CDA_SAMPLE_RATE * 3, data = frames * 4;
    uint8_t h[44];
    uint32_t i;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    memcpy(h, "RIFF", 4); h[4] = (uint8_t)(36 + data); h[5] = (uint8_t)((36 + data) >> 8); h[6] = (uint8_t)((36 + data) >> 16); h[7] = (uint8_t)((36 + data) >> 24);
    memcpy(h + 8, "WAVEfmt ", 8);
    h[16] = 16; h[17] = h[18] = h[19] = 0; h[20] = 1; h[21] = 0; h[22] = 2; h[23] = 0;
    h[24] = (uint8_t)CDA_SAMPLE_RATE; h[25] = (uint8_t)(CDA_SAMPLE_RATE >> 8); h[26] = (uint8_t)(CDA_SAMPLE_RATE >> 16); h[27] = 0;
    h[28] = (uint8_t)(CDA_SAMPLE_RATE * 4); h[29] = (uint8_t)((CDA_SAMPLE_RATE * 4) >> 8); h[30] = (uint8_t)((CDA_SAMPLE_RATE * 4) >> 16); h[31] = 0;
    h[32] = 4; h[33] = 0; h[34] = 16; h[35] = 0;
    memcpy(h + 36, "data", 4); h[40] = (uint8_t)data; h[41] = (uint8_t)(data >> 8); h[42] = (uint8_t)(data >> 16); h[43] = (uint8_t)(data >> 24);
    fwrite(h, 1, 44, f);
    for (i = 0; i < frames; i++) {
        int16_t v = (int16_t)(sin(2.0 * 3.14159265358979 * hz * (double)i / CDA_SAMPLE_RATE) * 16000.0);
        uint8_t b[4] = {(uint8_t)v, (uint8_t)((uint16_t)v >> 8), (uint8_t)v, (uint8_t)((uint16_t)v >> 8)};
        fwrite(b, 1, 4, f);
    }
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    char rip[256], t2[300], t3[300];
    static float buf[CDA_SAMPLE_RATE * 2];
    cdaudio_options o = {0, 0, 0};
    cdaudio *cd;
    cdaudio_status st;
    uint32_t s2, s3, s1;
    char path[2048];
    FILE *f;
    size_t got;

    if (argc < 2) { fprintf(stderr, "usage: %s INSTALLDIR [GBL-CD.iso]\n", argv[0]); return 2; }
    snprintf(rip, sizeof rip, "%s/test_port_rip_%d", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", (int)getpid());
    snprintf(t2, sizeof t2, "%s/Track02.wav", rip);
    snprintf(t3, sizeof t3, "%s/Track03.wav", rip);
    printf("cdaudio\n");
    if (mkdir_p(rip) != 0 || write_tone(t2, 440.0) != 0 || write_tone(t3, 660.0) != 0) { printf("  FAIL cannot write the tone rip in %s\n", rip); return 1; }
    CHECK(cdaudio_open(&cd, rip, &o) == 0, "open rip folder");
    cdaudio_track_info(cd, 1, &s1, NULL);
    cdaudio_track_info(cd, 2, &s2, NULL);
    cdaudio_track_info(cd, 3, &s3, NULL);
    CHECK(cda_msf_to_hsg(cda_hsg_to_msf(s3)) == s3, "MSF<->HSG round trip");

    /* play the last second of track 2 straight into track 3, as a CD would */
    cdaudio_play(cd, s3 - 75, 150);
    got = cdaudio_render(cd, buf, CDA_SAMPLE_RATE / 2);
    CHECK(fabs(freq(buf, got) - 440) < 5, "range starts in track 2 (440 Hz)");
    cdaudio_render(cd, buf, CDA_SAMPLE_RATE / 2);
    got = cdaudio_render(cd, buf, CDA_SAMPLE_RATE / 2);
    CHECK(fabs(freq(buf, got) - 660) < 5, "playback continues across the boundary into track 3 (660 Hz)");
    cdaudio_get_status(cd, &st);
    CHECK(st.track == 3, "status reports track 3");

    /* stop = pause, resume continues from the same sector */
    cdaudio_play(cd, s2, 525);
    cdaudio_render(cd, buf, CDA_SAMPLE_RATE / 2);
    cdaudio_stop(cd);
    cdaudio_get_status(cd, &st);
    CHECK(!st.busy && st.paused && st.position == s2 + 37, "first stop pauses at the current sector");
    got = cdaudio_render(cd, buf, 1000);
    CHECK(got == 0 && peak(buf, 1000) == 0, "paused drive outputs silence");
    CHECK(cdaudio_resume(cd) == 0, "resume accepted");
    cdaudio_get_status(cd, &st);
    CHECK(st.busy && st.position == s2 + 37, "resumes from the paused sector");
    cdaudio_stop(cd); cdaudio_stop(cd);
    CHECK(cdaudio_resume(cd) != 0, "second stop resets; resume refused");

    /* busy clears exactly at the end of the requested range */
    cdaudio_play(cd, s2, 75);
    got = cdaudio_render(cd, buf, CDA_SAMPLE_RATE);
    cdaudio_get_status(cd, &st);
    CHECK(got == 75 * CDA_FRAMES_PER_SECTOR && !st.busy, "1-second request renders 1 s, then busy clears");

    /* play request starting on the data track skips to the first audio */
    cdaudio_play(cd, s1 + 100, (s3 - s1));
    got = cdaudio_render(cd, buf, CDA_SAMPLE_RATE / 4);
    CHECK(fabs(freq(buf, got) - 440) < 5, "request starting on data track plays from track 2");

    /* volume */
    cdaudio_play(cd, s2, 75);
    got = cdaudio_render(cd, buf, CDA_SAMPLE_RATE / 4);
    { double full = peak(buf, got);
      cdaudio_set_volume(cd, 128, 0);
      got = cdaudio_render(cd, buf, CDA_SAMPLE_RATE / 4);
      { double l = 0, r = 0; size_t i;
        for (i = 0; i < got; i++) { if (fabs(buf[i*2]) > l) l = fabs(buf[i*2]); if (fabs(buf[i*2+1]) > r) r = fabs(buf[i*2+1]); }
        CHECK(fabs(l / full - 128.0 / 255.0) < 0.02 && r == 0, "per-channel volume (L=128, R=0)"); } }
    cdaudio_close(cd);
    remove(t2); remove(t3); rmdir(rip);

    printf("datapath (against the real install)\n");
    CHECK(dp_add_root(argv[1]) == 0, "add install folder as root");
    CHECK(dp_resolve("MW2.PRJ", path, sizeof path) == 0, "resolve MW2.PRJ");
    CHECK(dp_resolve("d:\\snd\\dig.ini", path, sizeof path) == 0 && strstr(path, "SND/DIG.INI"),
          "lowercase DOS path with drive letter finds SND/DIG.INI");
    CHECK(dp_resolve("..\\etc\\passwd", path, sizeof path) != 0, "refuses to escape the root");
    CHECK(dp_resolve("MEK\\NOPE.MEK", path, sizeof path) != 0, "missing file reports not found");
    f = dp_fopen("SAVES\\..", "rb");
    CHECK(f == NULL, "no directory traversal through fopen");
    f = dp_fopen("PORTTEST.TMP", "wb");
    CHECK(f != NULL, "write creates a new file in the first root");
    if (f) { fclose(f); dp_resolve("porttest.tmp", path, sizeof path); remove(path); }

    if (argc > 2) {
        unsigned char *a = NULL, *b = NULL;
        size_t la = 0, lb = 0;
        char inst[2048];
        printf("ISO 9660 (against the disc image)\n");
        dp_clear_roots();
        CHECK(dp_add_iso_root(argv[ISO], "GBL") == 0, "open image, GBL folder as root");
        CHECK(dp_add_iso_root(argv[ISO], NULL) == 0, "open image, disc root");
        CHECK(dp_add_iso_root(argv[ISO], "NOPE") != 0, "missing subfolder rejected");
        CHECK(dp_read_file("mw2.prj", &a, &la) == 0 && la == 30815361, "MW2.PRJ read from image (lowercase name)");
        snprintf(inst, sizeof inst, "%s/MW2.PRJ", argv[1]);
        f = fopen(inst, "rb");
        if (f) { b = malloc(la); lb = fread(b, 1, la, f); fclose(f); }
        CHECK(b && lb == la && memcmp(a, b, la) == 0, "image copy byte-identical to install copy");
        free(a); free(b); a = b = NULL;
        CHECK(dp_read_file("d:\\smk\\abflam01.smk", &a, &la) == 0 && la > 0, "SMK video from disc root, via drive-letter path");
        free(a); a = NULL;
        CHECK(dp_read_file("GBLINFO.TXT", &a, &la) == 0 && la == 107920, "GBLINFO.TXT from the disc root");
        free(a); a = NULL;
        CHECK(dp_read_file("GBL\\..\\..\\etc", &a, &la) != 0, "no traversal out of the image");
        CHECK(dp_read_file("SMK", &a, &la) != 0, "directories are not read as files");
        dp_clear_roots();
        dp_add_root(argv[1]);
        dp_add_iso_root(argv[ISO], NULL);
        CHECK(dp_resolve("MW2.PRJ", path, sizeof path) == 0 && strstr(path, argv[1]) == path,
              "install folder takes priority over the image");
    }

    printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails != 0;
}
