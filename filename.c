#define _GNU_SOURCE  // strcasestr
#include "filename.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifndef MAX_STR_LEN
#define MAX_STR_LEN 512  // same as opds_app.h
#endif

// Make a server-supplied filename safe for the FAT/exFAT user storage (/mnt/ext1).
// Calibre-Web, for example, sends names like "Polostan - Stephenson| Neal.epub"
// ('|' stands in for the comma in "Last, First" authors), and '|' is not allowed
// on FAT, so rename() fails. Map '|' back to ',', replace the other reserved
// characters and control bytes with '-', and trim trailing spaces/dots.
void SanitizeFatFilename(char *name) {
    for (size_t j = 0; name[j]; j++) {
        unsigned char c = (unsigned char)name[j];
        if (c == '|') name[j] = ',';
        else if (c < 0x20 || c == 0x7f || strchr("/\\:*?\"<>", c)) name[j] = '-';
    }
    size_t len = strlen(name);
    while (len > 0 && (name[len - 1] == ' ' || name[len - 1] == '.')) name[--len] = '\0';
    if (len == 0) strcpy(name, "book");
}

// Determines if a parsed link points to a supported eBook format
// Map an acquisition link to a file extension: MIME type first (exact match on
// the part before any ';'), then the extension at the end of the href path.
// The link title can't be used for this: servers put free text there
// (Project Gutenberg: "EPUB (no images, older E-readers)").
void InferExtension(const char *type, const char *href, char *out, size_t out_len) {
    static const struct { const char *mime; const char *ext; } mime_map[] = {
        {"application/epub+zip", "epub"},
        {"application/kepub+zip", "kepub.epub"},
        {"application/x-kobo-epub+zip", "kepub.epub"},
        {"application/pdf", "pdf"},
        {"application/x-mobipocket-ebook", "mobi"},
        {"application/x-mobi8-ebook", "azw3"},
        {"application/vnd.amazon.ebook", "azw"},
        {"application/x-fictionbook+xml", "fb2"},
        {"application/x-fictionbook", "fb2"},
        {"application/fb2", "fb2"},
        {"application/x-zip-compressed-fb2", "fb2.zip"},
        {"application/fb2+zip", "fb2.zip"},
        {"image/vnd.djvu", "djvu"},
        {"image/x-djvu", "djvu"},
        {"text/plain", "txt"},
        {"application/rtf", "rtf"},
        {"text/rtf", "rtf"},
        {"application/msword", "doc"},
        {"application/vnd.openxmlformats-officedocument.wordprocessingml.document", "docx"},
        {"application/vnd.ms-htmlhelp", "chm"},
        {"application/x-chm", "chm"},
        {"application/x-cbz", "cbz"},
        {"application/vnd.comicbook+zip", "cbz"},
        {"application/x-cbr", "cbr"},
        {"application/vnd.comicbook-rar", "cbr"},
        {"application/vnd.adobe.adept+xml", "acsm"},
        {NULL, NULL}
    };
    out[0] = '\0';
    if (type) {
        char t[128] = {0};
        size_t n = 0;
        while (type[n] && type[n] != ';' && n < sizeof(t) - 1) { t[n] = tolower((unsigned char)type[n]); n++; }
        while (n > 0 && t[n - 1] == ' ') t[--n] = '\0';
        for (int i = 0; mime_map[i].mime; i++) {
            if (strcmp(t, mime_map[i].mime) == 0) { snprintf(out, out_len, "%s", mime_map[i].ext); return; }
        }
    }
    if (href) {
        // Path only: stop at '?' or '#', then look at the last segment.
        char path[MAX_STR_LEN] = {0};
        size_t n = 0;
        while (href[n] && href[n] != '?' && href[n] != '#' && n < sizeof(path) - 1) { path[n] = tolower((unsigned char)href[n]); n++; }
        const char *seg = strrchr(path, '/');
        seg = seg ? seg + 1 : path;
        static const char *known[] = {"kepub.epub", "fb2.zip", "epub", "pdf", "mobi", "azw3", "azw", "fb2",
                                      "djvu", "txt", "rtf", "docx", "doc", "chm", "prc", "tcr", "cbz", "cbr", "acsm", NULL};
        size_t sl = strlen(seg);
        for (int i = 0; known[i]; i++) {
            size_t kl = strlen(known[i]);
            if (sl > kl + 1 && seg[sl - kl - 1] == '.' && strcmp(seg + sl - kl, known[i]) == 0) {
                snprintf(out, out_len, "%s", known[i]);
                return;
            }
        }
    }
}

// "<title>.<ext>" for when the server gave no usable filename. Uses the
// extension inferred from the link's MIME type/href, falling back to the
// label only when it is a plain token like "EPUB" (labels can be free text).
void BuildFallbackName(char *out, size_t out_len, const char *title, const char *ext_in, const char *label) {
    char ext[64];
    if (ext_in && ext_in[0]) snprintf(ext, sizeof(ext), "%s", ext_in);
    else {
        int plain = label && label[0] != '\0';
        for (const char *c = plain ? label : ""; *c; c++) if (!isalnum((unsigned char)*c)) { plain = 0; break; }
        snprintf(ext, sizeof(ext), "%s", plain ? label : "epub");
        for (char *c = ext; *c; c++) *c = tolower((unsigned char)*c);
    }
    char base[200];
    snprintf(base, sizeof(base), "%s", (title && title[0]) ? title : "book");  // keep well under FAT's 255
    snprintf(out, out_len, "%s.%s", base, ext);
    SanitizeFatFilename(out);
}

// Append ".<ext>" if the server's filename has no extension at all.
void EnsureExtension(char *name, size_t name_len, const char *ext) {
    const char *dot = strrchr(name, '.');
    if ((dot && dot != name && dot[1]) || !ext || !ext[0]) return;
    size_t len = strlen(name);
    if (dot && !dot[1]) name[--len] = '\0';
    if (len + 1 + strlen(ext) < name_len) snprintf(name + len, name_len - len, ".%s", ext);
}

// Extract the filename from one "Content-Disposition:" header line (already
// NUL-terminated, CR/LF stripped). Prefers RFC 6266/5987 filename*=, then
// filename= (quoted or bare). Returns 1 if out was set, 0 otherwise; out is
// left untouched when the line is some other header.
int ParseContentDisposition(const char *line, char *out, size_t out_len) {
    if (!line || !out || out_len < 2) return 0;
    if (strncasecmp(line, "Content-Disposition:", 20) != 0) return 0;
    {
        // Prefer filename*=UTF-8''<percent-encoded> (RFC 6266/5987): some servers
        // send only this form, and it carries the exact name.
        const char *ext = strcasestr(line, "filename*=");
        if (ext) {
            const char *p = ext + 10;
            // Skip charset'language' (language may be empty: UTF-8''name).
            const char *q1 = strchr(p, '\'');
            const char *q = q1 ? strchr(q1 + 1, '\'') : NULL;
            if (q) {
                p = q + 1;
                int i = 0;
                while (*p && *p != ';' && *p != ' ' && i < (int)out_len - 1) {
                    if (*p == '%' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) {
                        char hex[3] = { p[1], p[2], 0 };
                        out[i++] = (char)strtol(hex, NULL, 16);
                        p += 3;
                    } else {
                        out[i++] = *p++;
                    }
                }
                out[i] = '\0';
                if (i > 0) return 1;
            }
        }
        const char *ptr = strcasestr(line, "filename=");
        if (ptr) {
            ptr += 9; 
            if (*ptr == '"') {
                ptr++; 
                const char *end = strchr(ptr, '"');
                if (end) {
                    int len = end - ptr;
                    if (len > (int)out_len - 1) len = (int)out_len - 1;
                    strncpy(out, ptr, len);
                    out[len] = '\0';
                }
            } 
            else {
                int i = 0;
                while (ptr[i] && ptr[i] != ' ' && ptr[i] != ';' && ptr[i] != '\r' && ptr[i] != '\n' && i < (int)out_len - 1) {
                    out[i] = ptr[i]; 
                    i++;
                }
                out[i] = '\0';
            }
        }
    }
    return out[0] != '\0';
}
