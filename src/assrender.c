#include "assrender.h"
#include "render.h"
#include "sub.h"
#include "timecodes.h"

void AVSC_CC assrender_destroy(void* ud, AVS_ScriptEnvironment* env)
{
    ass_renderer_done(((udata*)ud)->ass_renderer);
    ass_library_done(((udata*)ud)->ass_library);
    ass_free_track(((udata*)ud)->ass);

    free(((udata*)ud)->sub_img[0]);
    free(((udata*)ud)->sub_img[1]);
    free(((udata*)ud)->sub_img[2]);
    free(((udata*)ud)->sub_img[3]);

    if (((udata*)ud)->isvfr)
        free(((udata*)ud)->timestamp);

    free(ud);
}

AVS_Value AVSC_CC assrender_create(AVS_ScriptEnvironment* env, AVS_Value args,
                                   void* ud)
{
    AVS_Value v;
    AVS_FilterInfo* fi;
    AVS_Clip* c = avs_new_c_filter(env, &fi, avs_array_elt(args, 0), 1);
    char e[250];

    const char* f = avs_as_string(avs_array_elt(args, 1));
    const char* vfr = avs_as_string(avs_array_elt(args, 2));
    int h = avs_is_int(avs_array_elt(args, 3)) ?
            avs_as_int(avs_array_elt(args, 3)) : 0;
    double scale = avs_is_float(avs_array_elt(args, 4)) ?
                   avs_as_float(avs_array_elt(args, 4)) : 1.0;
    double line_spacing = avs_is_float(avs_array_elt(args, 5)) ?
                          avs_as_float(avs_array_elt(args, 5)) : 0;
    double dar = avs_is_float(avs_array_elt(args, 6)) ?
                 avs_as_float(avs_array_elt(args, 6)) : 0;
    double sar = avs_is_float(avs_array_elt(args, 7)) ?
                 avs_as_float(avs_array_elt(args, 7)) : 0;
    int top = avs_is_int(avs_array_elt(args, 8)) ?
              avs_as_int(avs_array_elt(args, 8)) : 0;
    int bottom = avs_is_int(avs_array_elt(args, 9)) ?
                 avs_as_int(avs_array_elt(args, 9)) : 0;
    int left = avs_is_int(avs_array_elt(args, 10)) ?
               avs_as_int(avs_array_elt(args, 10)) : 0;
    int right = avs_is_int(avs_array_elt(args, 11)) ?
                avs_as_int(avs_array_elt(args, 11)) : 0;
    const char* cs = avs_as_string(avs_array_elt(args, 12)) ?
                     avs_as_string(avs_array_elt(args, 12)) : "UTF-8";
    int debuglevel = avs_is_int(avs_array_elt(args, 13)) ?
                     avs_as_int(avs_array_elt(args, 13)) : 0;
    const char* fontdir = avs_as_string(avs_array_elt(args, 14)) ?
                          avs_as_string(avs_array_elt(args, 14)) : "C:/Windows/Fonts";
    const char* srt_font = avs_as_string(avs_array_elt(args, 15)) ?
                           avs_as_string(avs_array_elt(args, 15)) : "sans-serif";
    const char* colorspace = avs_as_string(avs_array_elt(args, 16)) ?
                             avs_as_string(avs_array_elt(args, 16)) : "guess";

    char* tmpcsp = calloc(1, BUFSIZ);
    memcpy(tmpcsp, colorspace, BUFSIZ - 1);

    ASS_Hinting hinting;
    udata* data;
    ASS_Track* ass;

    if (!avs_is_rgb32(&fi->vi) && !avs_is_rgb24(&fi->vi) && !avs_is_yv12(&fi->vi) && !avs_is_yuv(&fi->vi)) {
        v = avs_new_value_error(
                "AssRender: supported colorspaces: RGB32, RGB24, "
                "YUY2, YV24, YV16, YV12, Y8");
        avs_release_clip(c);
        return v;
    }

    if (!f) {
        v = avs_new_value_error("AssRender: no input file specified");
        avs_release_clip(c);
        return v;
    }

    switch (h) {
    case 0:
        hinting = ASS_HINTING_NONE;
        break;
    case 1:
        hinting = ASS_HINTING_LIGHT;
        break;
    case 2:
        hinting = ASS_HINTING_NORMAL;
        break;
    case 3:
        hinting = ASS_HINTING_NATIVE;
        break;
    default:
        v = avs_new_value_error("AssRender: invalid hinting mode");
        avs_release_clip(c);
        return v;
    }

    data = malloc(sizeof(udata));

    if (!init_ass(fi->vi.width, fi->vi.height, scale, line_spacing, dar, sar,
                  top, bottom, left, right, hinting, debuglevel, fontdir,
                  data)) {
        v = avs_new_value_error("AssRender: failed to initialize");
        avs_release_clip(c);
        return v;
    }

    if (!strcasecmp(strrchr(f, '.'), ".srt"))
        ass = parse_srt(f, data, srt_font);
    else {
        ass = ass_read_file(data->ass_library, (char*)f, (char*)cs);
        ass_read_colorspace(f, tmpcsp);
    }

    if (!ass) {
        sprintf(e, "AssRender: unable to parse '%s'", f);
        v = avs_new_value_error(e);
        avs_release_clip(c);
        return v;
    }

    data->ass = ass;

    if (vfr) {
        int ver;
        FILE* fh = fopen(vfr, "r");

        if (!fh) {
            sprintf(e, "AssRender: could not read timecodes file '%s'", vfr);
            v = avs_new_value_error(e);
            avs_release_clip(c);
            return v;
        }

        data->isvfr = 1;

        if (fscanf(fh, "# timecode format v%d", &ver) != 1) {
            sprintf(e, "AssRender: invalid timecodes file '%s'", vfr);
            v = avs_new_value_error(e);
            avs_release_clip(c);
            return v;
        }

        switch (ver) {
        case 1:

            if (!parse_timecodesv1(fh, fi->vi.num_frames, data)) {
                v = avs_new_value_error(
                        "AssRender: error parsing timecodes file");
                avs_release_clip(c);
                return v;
            }

            break;
        case 2:

            if (!parse_timecodesv2(fh, fi->vi.num_frames, data)) {
                v = avs_new_value_error(
                        "AssRender: timecodes file had less frames than "
                        "expected");
                avs_release_clip(c);
                return v;
            }

            break;
        }

        fclose(fh);
    } else {
        data->isvfr = 0;
    }

    if (avs_is_rgb(&fi->vi)) {
        data->color_matrix = col2rgb;
    } else {
        if (!strcasecmp(tmpcsp, "bt.709") || !strcasecmp(tmpcsp, "rec709"))
            data->color_matrix = col2yuv709;
        else if (!strcasecmp(tmpcsp, "bt.601") || !strcasecmp(tmpcsp, "rec601"))
            data->color_matrix = col2yuv601;
        else if (!strcasecmp(tmpcsp, "bt.2020") || !strcasecmp(tmpcsp, "rec2020"))
            data->color_matrix = col2yuv2020;
        else {
            if (fi->vi.width > 1920 || fi->vi.height > 1080)
                data->color_matrix = col2yuv2020;
            else if (fi->vi.width > 1280 || fi->vi.height > 576)
                data->color_matrix = col2yuv709;
            else
                data->color_matrix = col2yuv601;
        }
    }

    switch (fi->vi.pixel_type)
    {
    case 0xA0000008:
        data->apply = apply_yv12;
        break;
    case 0xA0000308:
        data->apply = apply_yv16;
        break;
    case 0xA000030B:
        data->apply = apply_yv24;
        break;
    case 0xE0000000:
        data->apply = apply_y8;
        break;
    case 0x60000004:
        data->apply = apply_yuy2;
        break;
    case 0x50000001:
        data->apply = apply_rgb;
        break;
    case 0x50000002:
        data->apply = apply_rgba;
        break;
    default:
        v = avs_new_value_error("AssRender: unsupported pixel type");
        avs_release_clip(c);
        return v;
    }

    free(tmpcsp);

    data->sub_img[0] = malloc(fi->vi.width * fi->vi.height);
    data->sub_img[1] = malloc(fi->vi.width * fi->vi.height);
    data->sub_img[2] = malloc(fi->vi.width * fi->vi.height);
    data->sub_img[3] = malloc(fi->vi.width * fi->vi.height);

    fi->user_data = data;

    fi->get_frame = assrender_get_frame;

    v = avs_new_value_clip(c);
    avs_release_clip(c);

    avs_at_exit(env, assrender_destroy, data);

    return v;
}

const char* AVSC_CC avisynth_c_plugin_init(AVS_ScriptEnvironment* env)
{
    avs_add_function(env, "assrender",
                     "c[file]s[vfr]s[hinting]i[scale]f[line_spacing]f[dar]f"
                     "[sar]f[top]i[bottom]i[left]i[right]i[charset]s"
                     "[debuglevel]i[fontdir]s[srt_font]s[colorspace]s",
                     assrender_create, 0);
    return "AssRender 0.28: draws text subtitles better and faster than ever before";
}
