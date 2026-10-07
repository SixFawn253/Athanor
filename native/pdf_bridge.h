#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C"
{
#endif
    typedef void (*athanor_pdf_record)(void *, const char *, int, const char *, size_t);
    typedef int (*athanor_pdf_encode)(void *, const unsigned char *, int, int, int, double, double, unsigned char **,
                                      size_t *, int *, int *);
    typedef void (*athanor_pdf_progress)(void *, int, int);
    int athanor_pdf_snapshot(const char *, athanor_pdf_record, void *, char *, size_t);
    int athanor_pdf_compress(const char *, const char *, int, int, athanor_pdf_encode, athanor_pdf_progress, void *,
                             char *, size_t);
    typedef int (*athanor_pdf_page_image)(void *, const unsigned char *, int, int, int, int, int);
    int athanor_pdf_render(const char *, int, athanor_pdf_page_image, void *, char *, size_t);
    int athanor_pdf_from_images(const char *const *, int, const char *, athanor_pdf_progress, void *, char *, size_t);
#ifdef __cplusplus
}
#endif
