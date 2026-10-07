#include "pdf_bridge.h"
#include <math.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void record_string(athanor_pdf_record record, void *user, const char *type, int page, const char *text)
{
    record(user, type, page, text ? text : "", text ? strlen(text) : 0);
}
static void record_outline(athanor_pdf_record record, void *user, fz_outline *item, int depth)
{
    for (; item; item = item->next)
    {
        record_string(record, user, "outline-title", depth, item->title);
        record_string(record, user, "outline-uri", depth, item->uri);
        if (item->down)
            record_outline(record, user, item->down, depth + 1);
    }
}
int athanor_pdf_snapshot(const char *path, athanor_pdf_record record, void *user, char *error, size_t capacity)
{
    fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    pdf_document *doc = NULL;
    fz_page *page = NULL;
    fz_stext_page *text = NULL;
    fz_buffer *buffer = NULL;
    fz_outline *outline = NULL;
    pdf_obj *names = NULL;
    int count = -1, i = 0;
    char number[128];
    if (!ctx)
    {
        snprintf(error, capacity, "Cannot allocate PDF context");
        return -1;
    }
    fz_var(doc);
    fz_var(page);
    fz_var(text);
    fz_var(buffer);
    fz_var(outline);
    fz_var(names);
    fz_var(count);
    fz_try(ctx)
    {
        fz_register_document_handlers(ctx);
        doc = pdf_open_document(ctx, path);
        if (pdf_needs_password(ctx, doc))
            fz_throw(ctx, FZ_ERROR_ARGUMENT, "PDF requires a password");
        count = pdf_count_pages(ctx, doc);
        for (i = 0; i < count; i++)
        {
            pdf_annot *annot;
            pdf_annot *widget;
            fz_stext_options options = {0};
            fz_rect rect;
            unsigned char *data;
            size_t size;
            page = (fz_page *)pdf_load_page(ctx, doc, i);
            rect = fz_bound_page(ctx, page);
            snprintf(number, sizeof(number), "%.3f,%.3f,%.3f,%.3f", rect.x0, rect.y0, rect.x1, rect.y1);
            record_string(record, user, "page", i, number);
            text = fz_new_stext_page_from_page(ctx, page, &options);
            buffer = fz_new_buffer_from_stext_page(ctx, text);
            size = fz_buffer_storage(ctx, buffer, &data);
            record(user, "text", i, (const char *)data, size);
            fz_drop_buffer(ctx, buffer);
            buffer = NULL;
            fz_drop_stext_page(ctx, text);
            text = NULL;
            for (widget = pdf_first_widget(ctx, (pdf_page *)page); widget; widget = pdf_next_widget(ctx, widget))
            {
                pdf_obj *field = pdf_annot_obj(ctx, widget);
                char *name = pdf_load_field_name(ctx, field);
                record_string(record, user, "field-name", i, name);
                fz_free(ctx, name);
                record_string(record, user, "field-type", i, pdf_field_type_string(ctx, field));
                record_string(record, user, "field-value", i, pdf_field_value(ctx, field));
            }
            for (annot = pdf_first_annot(ctx, (pdf_page *)page); annot; annot = pdf_next_annot(ctx, annot))
            {
                rect = pdf_bound_annot(ctx, annot);
                snprintf(number, sizeof(number), "%d:%.3f,%.3f,%.3f,%.3f", pdf_annot_type(ctx, annot), rect.x0, rect.y0,
                         rect.x1, rect.y1);
                record_string(record, user, "annotation", i, number);
                record_string(record, user, "annotation-text", i, pdf_annot_contents(ctx, annot));
            }
            fz_drop_page(ctx, page);
            page = NULL;
        }
        outline = fz_load_outline(ctx, (fz_document *)doc);
        record_outline(record, user, outline, 0);
        names = pdf_load_name_tree(ctx, doc, PDF_NAME(EmbeddedFiles));
        for (i = 0; names && i < pdf_dict_len(ctx, names); i++)
        {
            unsigned char *data;
            size_t size;
            record_string(record, user, "attachment-name", 0,
                          pdf_is_name(ctx, pdf_dict_get_key(ctx, names, i))
                              ? pdf_to_name(ctx, pdf_dict_get_key(ctx, names, i))
                              : pdf_to_text_string(ctx, pdf_dict_get_key(ctx, names, i)));
            buffer = pdf_load_embedded_file_contents(ctx, pdf_dict_get_val(ctx, names, i));
            size = fz_buffer_storage(ctx, buffer, &data);
            record(user, "attachment", 0, (const char *)data, size);
            fz_drop_buffer(ctx, buffer);
            buffer = NULL;
        }
    }
    fz_always(ctx)
    {
        pdf_drop_obj(ctx, names);
        fz_drop_outline(ctx, outline);
        fz_drop_buffer(ctx, buffer);
        fz_drop_stext_page(ctx, text);
        fz_drop_page(ctx, page);
        pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx)
    {
        snprintf(error, capacity, "%s", fz_caught_message(ctx));
        count = -1;
    }
    fz_drop_context(ctx);
    return count;
}

static void resize_soft_mask(fz_context *ctx, pdf_document *doc, pdf_obj *object, fz_image *image, int width,
                             int height)
{
    fz_pixmap *mask = NULL, *gray = NULL, *scaled = NULL;
    fz_buffer *buffer = NULL;
    pdf_obj *dictionary = NULL, *reference = NULL;
    fz_var(mask);
    fz_var(gray);
    fz_var(scaled);
    fz_var(buffer);
    fz_var(dictionary);
    fz_var(reference);
    if (!image->mask || !pdf_dict_get(ctx, object, PDF_NAME(SMask)))
        return;
    fz_try(ctx)
    {
        fz_color_params colors = {0, 1, 0, 0};
        mask = fz_get_unscaled_pixmap_from_image(ctx, image->mask);
        gray = fz_convert_pixmap(ctx, mask, fz_device_gray(ctx), NULL, NULL, colors, 0);
        scaled = fz_scale_pixmap(ctx, gray, 0, 0, (float)width, (float)height, NULL);
        buffer = fz_new_buffer(ctx, (size_t)width * height);
        for (int row = 0; row < height; row++)
            fz_append_data(ctx, buffer, fz_pixmap_samples(ctx, scaled) + row * fz_pixmap_stride(ctx, scaled), width);
        dictionary = pdf_new_dict(ctx, doc, 8);
        pdf_dict_put(ctx, dictionary, PDF_NAME(Type), PDF_NAME(XObject));
        pdf_dict_put(ctx, dictionary, PDF_NAME(Subtype), PDF_NAME(Image));
        pdf_dict_put_int(ctx, dictionary, PDF_NAME(Width), width);
        pdf_dict_put_int(ctx, dictionary, PDF_NAME(Height), height);
        pdf_dict_put_int(ctx, dictionary, PDF_NAME(BitsPerComponent), 8);
        pdf_dict_put(ctx, dictionary, PDF_NAME(ColorSpace), PDF_NAME(DeviceGray));
        reference = pdf_add_stream(ctx, doc, buffer, dictionary, 0);
        pdf_dict_put(ctx, object, PDF_NAME(SMask), reference);
    }
    fz_always(ctx)
    {
        pdf_drop_obj(ctx, reference);
        pdf_drop_obj(ctx, dictionary);
        fz_drop_buffer(ctx, buffer);
        fz_drop_pixmap(ctx, scaled);
        fz_drop_pixmap(ctx, gray);
        fz_drop_pixmap(ctx, mask);
    }
    fz_catch(ctx)
    {
        fz_rethrow(ctx);
    }
}

int athanor_pdf_compress(const char *input, const char *output, int dpi, int quality, athanor_pdf_encode encode,
                         athanor_pdf_progress progress, void *user, char *error, size_t capacity)
{
    fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    pdf_document *doc = NULL;
    fz_image **images = NULL;
    double *widths = NULL, *heights = NULL;
    pdf_obj *object = NULL, *reference = NULL;
    fz_page *page = NULL;
    fz_stext_page *text = NULL;
    fz_pixmap *pix = NULL, *rgb = NULL;
    fz_buffer *buffer = NULL, *raw = NULL;
    unsigned char *encoded = NULL;
    int result = -1, count = 0, pages = 0, i = 0, j = 0, has_forms = 0;
    if (!ctx)
    {
        snprintf(error, capacity, "Cannot allocate PDF context");
        return -1;
    }
    fz_var(doc);
    fz_var(images);
    fz_var(widths);
    fz_var(heights);
    fz_var(object);
    fz_var(reference);
    fz_var(page);
    fz_var(text);
    fz_var(pix);
    fz_var(rgb);
    fz_var(buffer);
    fz_var(raw);
    fz_var(encoded);
    fz_var(count);
    fz_var(result);
    fz_try(ctx)
    {
        fz_register_document_handlers(ctx);
        doc = pdf_open_document(ctx, input);
        if (pdf_needs_password(ctx, doc))
            fz_throw(ctx, FZ_ERROR_ARGUMENT, "PDF requires a password");
        pages = pdf_count_pages(ctx, doc);
        count = pdf_count_objects(ctx, doc);
        images = fz_calloc(ctx, count, sizeof(fz_image *));
        widths = fz_calloc(ctx, count, sizeof(double));
        heights = fz_calloc(ctx, count, sizeof(double));
        if (dpi > 0)
        {
            for (i = 1; i < count; i++)
            {
                object = pdf_load_object(ctx, doc, i);
                if (pdf_name_eq(ctx, pdf_dict_get(ctx, object, PDF_NAME(Subtype)), PDF_NAME(Image)) &&
                    !pdf_to_bool(ctx, pdf_dict_get(ctx, object, PDF_NAME(ImageMask))) &&
                    !pdf_dict_get(ctx, object, PDF_NAME(Mask)))
                {
                    reference = pdf_new_indirect(ctx, doc, i, 0);
                    images[i] = pdf_load_image(ctx, doc, reference);
                    pdf_drop_obj(ctx, reference);
                    reference = NULL;
                }
                pdf_drop_obj(ctx, object);
                object = NULL;
            }
        }
        for (i = 0; i < pages; i++)
        {
            fz_stext_options options = {0};
            fz_stext_block *block;
            page = (fz_page *)pdf_load_page(ctx, doc, i);
            if (pdf_first_widget(ctx, (pdf_page *)page))
                has_forms = 1;
            if (dpi > 0)
            {
                options.flags = FZ_STEXT_PRESERVE_IMAGES;
                text = fz_new_stext_page_from_page(ctx, page, &options);
                for (block = text->first_block; block; block = block->next)
                    if (block->type == FZ_STEXT_BLOCK_IMAGE)
                    {
                        for (j = 1; j < count; j++)
                            if (images[j] == block->u.i.image)
                            {
                                widths[j] = fmax(widths[j], fabs(block->bbox.x1 - block->bbox.x0));
                                heights[j] = fmax(heights[j], fabs(block->bbox.y1 - block->bbox.y0));
                            }
                    }
                fz_drop_stext_page(ctx, text);
                text = NULL;
            }
            fz_drop_page(ctx, page);
            page = NULL;
            if (progress)
                progress(user, i + 1, pages * 2);
        }
        if (!has_forms)
            pdf_subset_fonts(ctx, doc, 0, NULL);
        for (i = 1; i < count; i++)
        {
            object = pdf_load_object(ctx, doc, i);
            if (pdf_name_eq(ctx, pdf_dict_get(ctx, object, PDF_NAME(Subtype)), PDF_NAME(Image)))
            {
                pdf_dict_dels(ctx, object, "PieceInfo");
                pdf_dict_dels(ctx, object, "Metadata");
                if (images[i] && widths[i] > 0 && heights[i] > 0)
                {
                    int w = images[i]->w, h = images[i]->h, nw = 0, nh = 0;
                    size_t length = 0;
                    fz_color_params colors = {0, 1, 0, 0};
                    pix = fz_get_unscaled_pixmap_from_image(ctx, images[i]);
                    rgb = fz_convert_pixmap(ctx, pix, fz_device_rgb(ctx), NULL, NULL, colors, 0);
                    if (encode(user, fz_pixmap_samples(ctx, rgb), w, h, fz_pixmap_stride(ctx, rgb), widths[i],
                               heights[i], &encoded, &length, &nw, &nh))
                    {
                        raw = pdf_load_raw_stream_number(ctx, doc, i);
                        if (nw != w || nh != h || length < fz_buffer_storage(ctx, raw, NULL))
                        {
                            pdf_obj *ref = pdf_new_indirect(ctx, doc, i, 0);
                            buffer = fz_new_buffer_from_copied_data(ctx, encoded, length);
                            pdf_update_stream(ctx, doc, ref, buffer, 1);
                            pdf_drop_obj(ctx, ref);
                            if (nw != w || nh != h)
                                resize_soft_mask(ctx, doc, object, images[i], nw, nh);
                            pdf_dict_puts_drop(ctx, object, "Width", pdf_new_int(ctx, nw));
                            pdf_dict_puts_drop(ctx, object, "Height", pdf_new_int(ctx, nh));
                            pdf_dict_puts_drop(ctx, object, "BitsPerComponent", pdf_new_int(ctx, 8));
                            pdf_dict_put(ctx, object, PDF_NAME(ColorSpace), PDF_NAME(DeviceRGB));
                            pdf_dict_put(ctx, object, PDF_NAME(Filter), PDF_NAME(DCTDecode));
                            pdf_dict_dels(ctx, object, "Decode");
                            pdf_dict_dels(ctx, object, "DecodeParms");
                            fz_drop_buffer(ctx, buffer);
                            buffer = NULL;
                        }
                        fz_drop_buffer(ctx, raw);
                        raw = NULL;
                        free(encoded);
                        encoded = NULL;
                    }
                    fz_drop_pixmap(ctx, rgb);
                    rgb = NULL;
                    fz_drop_pixmap(ctx, pix);
                    pix = NULL;
                    fz_drop_image(ctx, images[i]);
                    images[i] = NULL;
                    fz_empty_store(ctx);
                }
            }
            pdf_drop_obj(ctx, object);
            object = NULL;
            if (progress)
                progress(user, pages + (i * pages) / count, pages * 2);
        }
        {
            pdf_write_options options = {0};
            pdf_init_write_options(ctx, &options);
            options.do_garbage = 4;
            options.do_compress = 1;
            options.do_compress_images = 1;
            options.do_compress_fonts = 1;
            options.do_use_objstms = 1;
            options.compression_effort = 100;
            options.do_clean = 1;
            pdf_save_document(ctx, doc, output, &options);
            result = 0;
        }
    }
    fz_always(ctx)
    {
        if (images)
            for (i = 0; i < count; i++)
                fz_drop_image(ctx, images[i]);
        fz_free(ctx, images);
        fz_free(ctx, widths);
        fz_free(ctx, heights);
        free(encoded);
        pdf_drop_obj(ctx, object);
        pdf_drop_obj(ctx, reference);
        fz_drop_buffer(ctx, raw);
        fz_drop_buffer(ctx, buffer);
        fz_drop_pixmap(ctx, rgb);
        fz_drop_pixmap(ctx, pix);
        fz_drop_stext_page(ctx, text);
        fz_drop_page(ctx, page);
        pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx)
    {
        snprintf(error, capacity, "%s", fz_caught_message(ctx));
        result = -1;
    }
    fz_drop_context(ctx);
    return result;
}

int athanor_pdf_render(const char *path, int dpi, athanor_pdf_page_image emit_page, void *user, char *error,
                       size_t capacity)
{
    fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    fz_document *doc = NULL;
    fz_pixmap *pix = NULL;
    int count = -1, i = 0;
    if (!ctx)
    {
        snprintf(error, capacity, "Cannot allocate PDF renderer");
        return -1;
    }
    fz_var(doc);
    fz_var(pix);
    fz_var(count);
    fz_var(i);
    fz_try(ctx)
    {
        fz_register_document_handlers(ctx);
        doc = fz_open_document(ctx, path);
        if (fz_needs_password(ctx, doc))
            fz_throw(ctx, FZ_ERROR_ARGUMENT, "PDF requires a password");
        count = fz_count_pages(ctx, doc);
        if (count <= 0)
            fz_throw(ctx, FZ_ERROR_ARGUMENT, "PDF has no pages");
        for (i = 0; i < count; i++)
        {
            pix =
                fz_new_pixmap_from_page_number(ctx, doc, i, fz_scale(dpi / 72.0f, dpi / 72.0f), fz_device_rgb(ctx), 0);
            if (!emit_page(user, fz_pixmap_samples(ctx, pix), fz_pixmap_width(ctx, pix), fz_pixmap_height(ctx, pix),
                           fz_pixmap_stride(ctx, pix), i, count))
                fz_throw(ctx, FZ_ERROR_ARGUMENT, "Cannot encode PDF page image");
            fz_drop_pixmap(ctx, pix);
            pix = NULL;
        }
    }
    fz_always(ctx)
    {
        fz_drop_pixmap(ctx, pix);
        fz_drop_document(ctx, doc);
    }
    fz_catch(ctx)
    {
        snprintf(error, capacity, "%s", fz_caught_message(ctx));
        count = -1;
    }
    fz_drop_context(ctx);
    return count;
}
int athanor_pdf_from_images(const char *const *paths, int count, const char *output, athanor_pdf_progress progress,
                            void *user, char *error, size_t capacity)
{
    fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    pdf_document *doc = NULL;
    fz_image *image = NULL;
    fz_buffer *contents = NULL;
    pdf_obj *resources = NULL, *image_ref = NULL, *page = NULL;
    int ok = 0, i = 0;
    if (!ctx)
    {
        snprintf(error, capacity, "Cannot allocate PDF writer");
        return 0;
    }
    fz_var(doc);
    fz_var(image);
    fz_var(contents);
    fz_var(resources);
    fz_var(image_ref);
    fz_var(page);
    fz_var(ok);
    fz_var(i);
    fz_try(ctx)
    {
        doc = pdf_create_document(ctx);
        for (i = 0; i < count; i++)
        {
            char commands[256];
            float width, height;
            pdf_obj *xobjects;
            image = fz_new_image_from_file(ctx, paths[i]);
            {
                float page_scale = fminf(0.75f, 14400.0f / fmaxf((float)image->w, (float)image->h));
                width = image->w * page_scale;
                height = image->h * page_scale;
            }
            image_ref = pdf_add_image(ctx, doc, image);
            resources = pdf_new_dict(ctx, doc, 1);
            xobjects = pdf_dict_puts_dict(ctx, resources, "XObject", 1);
            pdf_dict_puts(ctx, xobjects, "Im0", image_ref);
            snprintf(commands, sizeof(commands), "q %g 0 0 %g 0 0 cm /Im0 Do Q\n", width, height);
            contents = fz_new_buffer_from_copied_data(ctx, (const unsigned char *)commands, strlen(commands));
            page = pdf_add_page(ctx, doc, fz_make_rect(0, 0, width, height), 0, resources, contents);
            pdf_insert_page(ctx, doc, -1, page);
            pdf_drop_obj(ctx, page);
            page = NULL;
            pdf_drop_obj(ctx, image_ref);
            image_ref = NULL;
            pdf_drop_obj(ctx, resources);
            resources = NULL;
            fz_drop_buffer(ctx, contents);
            contents = NULL;
            fz_drop_image(ctx, image);
            image = NULL;
            if (progress)
                progress(user, i + 1, count);
        }
        {
            pdf_write_options opts = {0};
            pdf_init_write_options(ctx, &opts);
            opts.do_compress = 1;
            opts.do_compress_images = 1;
            opts.do_garbage = 3;
            pdf_save_document(ctx, doc, output, &opts);
        }
        ok = 1;
    }
    fz_always(ctx)
    {
        pdf_drop_obj(ctx, page);
        pdf_drop_obj(ctx, image_ref);
        pdf_drop_obj(ctx, resources);
        fz_drop_buffer(ctx, contents);
        fz_drop_image(ctx, image);
        pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx)
    {
        snprintf(error, capacity, "%s", fz_caught_message(ctx));
    }
    fz_drop_context(ctx);
    return ok;
}
