// Host-side tests for filename.c (no SDK, device or network needed).
//   make test                         plain
//   make test HOST_CC="cc -fsanitize=address,undefined -fno-sanitize-recover=all -g"
#include "../filename.h"
#include <stdio.h>
#include <string.h>

static int failures = 0, checks = 0;

#define EXPECT_STR(got, want, what) do { \
    checks++; \
    if (strcmp((got), (want)) != 0) { \
        failures++; \
        fprintf(stderr, "FAIL %s:%d %s\n  got:  [%s]\n  want: [%s]\n", __FILE__, __LINE__, (what), (got), (want)); \
    } \
} while (0)

#define EXPECT_INT(got, want, what) do { \
    checks++; \
    if ((got) != (want)) { \
        failures++; \
        fprintf(stderr, "FAIL %s:%d %s: got %d, want %d\n", __FILE__, __LINE__, (what), (int)(got), (int)(want)); \
    } \
} while (0)

static void sanitize(const char *in, const char *want) {
    char buf[256];
    snprintf(buf, sizeof buf, "%s", in);
    SanitizeFatFilename(buf);
    EXPECT_STR(buf, want, in);
}

static void test_sanitize(void) {
    // Calibre-Web writes "Last, First" authors with '|' for the comma.
    sanitize("Polostan - Stephenson| Neal.epub", "Polostan - Stephenson, Neal.epub");
    sanitize("a:b*c?d\"e<f>g/h\\i.epub", "a-b-c-d-e-f-g-h-i.epub");
    sanitize("tab\there\x01.epub", "tab-here-.epub");
    sanitize("trailing. . ", "trailing");
    sanitize("...", "book");
    sanitize("", "book");
    sanitize("Café — Ünïcode.epub", "Café — Ünïcode.epub");   // UTF-8 bytes pass through
    sanitize("Frankenstein; Or, The Modern Prometheus.epub", "Frankenstein; Or, The Modern Prometheus.epub");
}

static void infer(const char *type, const char *href, const char *want) {
    char ext[16];
    InferExtension(type, href, ext, sizeof ext);
    char what[300];
    snprintf(what, sizeof what, "InferExtension(%s, %s)", type ? type : "NULL", href ? href : "NULL");
    EXPECT_STR(ext, want, what);
}

static void test_infer(void) {
    // MIME type wins, whatever the href looks like (Gutenberg hrefs end ".epub3.images").
    infer("application/epub+zip", "https://www.gutenberg.org/ebooks/84.epub3.images", "epub");
    infer("application/x-mobipocket-ebook", "/ebooks/84.kf8.images", "mobi");
    infer("application/epub+zip; charset=utf-8", "/x", "epub");
    infer("APPLICATION/PDF", "/x", "pdf");
    infer("application/kepub+zip", "/x", "kepub.epub");
    infer("application/vnd.comicbook+zip", "/api/v1/books/abc/file", "cbz");   // Komga
    infer("application/x-cbr", "/x", "cbr");
    infer("application/vnd.adobe.adept+xml", "/x", "acsm");
    infer("application/x-fictionbook+xml", "/x", "fb2");
    infer("image/vnd.djvu", "/x", "djvu");
    infer("text/plain", "/x", "txt");
    // Unknown/generic type: fall back to the href's extension.
    infer(NULL, "/dl/book.kepub.epub?token=1", "kepub.epub");
    infer("application/octet-stream", "/dl/thing.CBZ", "cbz");
    infer("application/octet-stream", "/dl/thing.fb2.zip#frag", "fb2.zip");
    infer("application/octet-stream", "/dl/thing", "");
    infer("application/octet-stream", "/dl.epub/thing", "");      // extension must be in the last segment
    infer(NULL, NULL, "");
    // Output buffer is respected.
    char tiny[4];
    InferExtension("application/kepub+zip", "/x", tiny, sizeof tiny);
    EXPECT_STR(tiny, "kep", "InferExtension truncates to out_len");
}

static void fallback(const char *title, const char *ext, const char *label, const char *want) {
    char out[256];
    BuildFallbackName(out, sizeof out, title, ext, label);
    EXPECT_STR(out, want, title ? title : "NULL title");
}

static void test_fallback(void) {
    // Gutenberg: no Content-Disposition, label is free text.
    fallback("Frankenstein; Or, The Modern Prometheus", "epub", "EPUB3 (E-readers incl. Send-to-Kindle)",
             "Frankenstein; Or, The Modern Prometheus.epub");
    fallback("Batman: Year One", "cbz", "Download", "Batman- Year One.cbz");
    // No inferred extension: plain-token label is used, lower-cased; free text falls back to epub.
    fallback("Mystery", "", "FB2", "Mystery.fb2");
    fallback("Mystery", "", "EPUB (with images)", "Mystery.epub");
    fallback("Mystery", NULL, NULL, "Mystery.epub");
    fallback("", "pdf", "PDF", "book.pdf");
    fallback(NULL, "pdf", "PDF", "book.pdf");
    // Long titles are capped well under FAT's 255 and keep their extension.
    char longt[400];
    memset(longt, 'x', sizeof longt - 1);
    longt[sizeof longt - 1] = '\0';
    char out[256];
    BuildFallbackName(out, sizeof out, longt, "epub", "EPUB");
    checks++;
    if (strlen(out) > 210 || strcmp(out + strlen(out) - 5, ".epub") != 0) {
        failures++;
        fprintf(stderr, "FAIL long title: len %zu, tail [%s]\n", strlen(out), out + (strlen(out) > 8 ? strlen(out) - 8 : 0));
    }
}

static void ensure(const char *name, const char *ext, const char *want) {
    char buf[256];
    snprintf(buf, sizeof buf, "%s", name);
    EnsureExtension(buf, sizeof buf, ext);
    EXPECT_STR(buf, want, name);
}

static void test_ensure(void) {
    ensure("84", "epub", "84.epub");
    ensure("book.", "epub", "book.epub");
    ensure("book.epub", "epub", "book.epub");
    ensure("book.mobi", "epub", "book.mobi");     // server's extension is trusted
    ensure(".hidden", "epub", ".hidden.epub");
    ensure("noext", "", "noext");
    ensure("noext", NULL, "noext");
    char small[8] = "abcdef";
    EnsureExtension(small, sizeof small, "epub");  // no room: left alone, no overflow
    EXPECT_STR(small, "abcdef", "EnsureExtension without room");
}

static void cd(const char *line, int want_rc, const char *want) {
    char out[256] = {0};
    int rc = ParseContentDisposition(line, out, sizeof out);
    EXPECT_INT(rc, want_rc, line);
    EXPECT_STR(out, want, line);
}

static void test_content_disposition(void) {
    // Calibre-Web sends both forms.
    cd("Content-Disposition: attachment; filename=\"Polostan - Stephenson| Neal.epub\"; "
       "filename*=UTF-8''Polostan%20-%20Stephenson%7C%20Neal.epub",
       1, "Polostan - Stephenson| Neal.epub");
    cd("Content-Disposition: attachment; filename*=UTF-8''Caf%C3%A9%20Book.cbz", 1, "Café Book.cbz");
    cd("Content-Disposition: attachment; filename*=utf-8'en'Book.epub", 1, "Book.epub");
    cd("content-disposition: attachment; filename=plain.epub", 1, "plain.epub");
    cd("Content-Disposition: attachment; filename=plain.epub; size=10", 1, "plain.epub");
    cd("Content-Disposition: attachment; filename=\"quoted name.pdf\"", 1, "quoted name.pdf");
    cd("Content-Disposition: attachment; FILENAME=\"Upper.pdf\"", 1, "Upper.pdf");
    cd("Content-Disposition: inline", 0, "");
    cd("Content-Type: application/epub+zip", 0, "");
    cd("Content-Disposition: attachment; filename=\"unterminated.epub", 0, "");
    cd("", 0, "");
    // Malformed percent escapes are copied literally rather than read past.
    cd("Content-Disposition: attachment; filename*=UTF-8''bad%2", 1, "bad%2");
    cd("Content-Disposition: attachment; filename*=UTF-8''bad%zz.epub", 1, "bad%zz.epub");
    // Output is bounded by out_len.
    char out[8] = {0};
    ParseContentDisposition("Content-Disposition: attachment; filename=\"0123456789.epub\"", out, sizeof out);
    EXPECT_STR(out, "0123456", "quoted name truncated to out_len");
    char out2[8] = {0};
    ParseContentDisposition("Content-Disposition: attachment; filename*=UTF-8''0123456789.epub", out2, sizeof out2);
    EXPECT_STR(out2, "0123456", "encoded name truncated to out_len");
}

// End to end, as ui.c composes them: header -> name (or fallback) -> extension -> sanitize.
static void pipeline(const char *header, const char *title, const char *type, const char *href,
                     const char *label, const char *want) {
    char name[256] = {0}, ext[16];
    if (header) ParseContentDisposition(header, name, sizeof name);
    InferExtension(type, href, ext, sizeof ext);
    if (!name[0]) BuildFallbackName(name, sizeof name, title, ext, label);
    else EnsureExtension(name, sizeof name, ext);
    SanitizeFatFilename(name);
    EXPECT_STR(name, want, title);
}

static void test_pipeline(void) {
    pipeline("Content-Disposition: attachment; filename=\"Polostan - Stephenson| Neal.epub\"; "
             "filename*=UTF-8''Polostan%20-%20Stephenson%7C%20Neal.epub",
             "Polostan", "application/epub+zip", "/opds/download/475/epub/", "EPUB",
             "Polostan - Stephenson, Neal.epub");
    pipeline(NULL, "Frankenstein; Or, The Modern Prometheus", "application/epub+zip",
             "https://www.gutenberg.org/ebooks/84.epub3.images", "EPUB3 (E-readers incl. Send-to-Kindle)",
             "Frankenstein; Or, The Modern Prometheus.epub");
    pipeline("Content-Disposition: attachment; filename=84", "Frankenstein", "application/epub+zip",
             "/ebooks/84.epub3.images", "EPUB", "84.epub");
}

int main(void) {
    test_sanitize();
    test_infer();
    test_fallback();
    test_ensure();
    test_content_disposition();
    test_pipeline();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
