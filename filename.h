#ifndef OPDS_FILENAME_H
#define OPDS_FILENAME_H
// Download-filename logic. Pure C (no InkView, curl or libxml2), so it builds
// and is tested on the host: see tests/test_filename.c and `make test`.
#include <stddef.h>

// Make a server-supplied filename safe for FAT/exFAT user storage, in place.
void SanitizeFatFilename(char *name);

// Map an acquisition link's MIME type / href to an extension without the dot
// ("epub", "kepub.epub", "cbz"); out = "" if unknown.
void InferExtension(const char *type, const char *href, char *out, size_t out_len);

// "<title>.<ext>" (sanitized) for when the server sent no usable filename.
void BuildFallbackName(char *out, size_t out_len, const char *title, const char *ext, const char *label);

// Append ".<ext>" when name has no extension.
void EnsureExtension(char *name, size_t name_len, const char *ext);

// Filename from one Content-Disposition header line; 1 if found.
int ParseContentDisposition(const char *line, char *out, size_t out_len);

#endif
