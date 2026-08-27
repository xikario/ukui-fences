/*
 * StackBlur — O(n) Blur Algorithm for QImage
 *
 * Based on Mario Klingemann's StackBlur algorithm.
 * Optimized for Qt 5 QImage (Format_ARGB32 / Format_RGB32).
 *
 * Performance: O(width × height), independent of blur radius.
 * Memory: O(radius) stack buffers only.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <QImage>
#include <algorithm>
#include <cstring>
#include <thread>
#include <vector>

namespace GlassEffect {

namespace detail {

static const unsigned short stackblur_mul[255] = {
    512,512,456,512,328,456,335,512,405,328,271,456,388,335,292,512,
    454,405,364,328,298,271,496,456,420,388,360,335,312,292,273,512,
    482,454,428,405,383,364,345,328,312,298,284,271,259,496,475,456,
    437,420,404,388,374,360,347,335,323,312,302,292,282,273,265,512,
    497,482,468,454,441,428,417,405,394,383,373,364,354,345,337,328,
    320,312,305,298,291,284,278,271,265,259,507,496,485,475,465,456,
    446,437,428,420,412,404,396,388,381,374,367,360,354,347,341,335,
    329,323,318,312,307,302,297,292,287,282,278,273,269,265,261,512,
    505,497,489,482,475,468,461,454,447,441,435,428,422,417,411,405,
    399,394,389,383,378,373,368,364,359,354,350,345,341,337,332,328,
    324,320,316,312,309,305,301,298,294,291,287,284,281,278,274,271,
    268,265,262,259,257,507,501,496,491,485,480,475,470,465,460,456,
    451,446,442,437,433,428,424,420,416,412,408,404,400,396,392,388,
    385,381,377,374,370,367,363,360,357,354,350,347,344,341,338,335,
    332,329,326,323,320,318,315,312,310,307,304,302,299,297,294,292,
    289,287,285,282,280,278,275,273,271,269,267,265,263,261,259
};

static const unsigned char stackblur_shr[255] = {
     9, 11, 12, 13, 13, 14, 14, 15, 15, 15, 15, 16, 16, 16, 16, 17,
    17, 17, 17, 17, 17, 17, 18, 18, 18, 18, 18, 18, 18, 18, 18, 19,
    19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 20, 20, 20,
    20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 21,
    21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
    21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 22, 22, 22, 22, 22, 22,
    22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22,
    22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 23,
    23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23,
    23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23,
    23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23,
    23, 23, 23, 23, 23, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
    24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
    24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
    24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
    24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24
};

template <typename Work>
inline void parallelFor(int count, bool enabled, Work work)
{
    const unsigned int available = std::thread::hardware_concurrency();
    const int workers = enabled
        ? std::min(count, static_cast<int>(std::min(available, 8U))) : 1;
    if (workers < 2) {
        work(0, count);
        return;
    }

    std::vector<std::thread> threads;
    threads.reserve(workers);
    for (int worker = 0; worker < workers; ++worker) {
        const int begin = count * worker / workers;
        const int end = count * (worker + 1) / workers;
        threads.emplace_back([=, &work] { work(begin, end); });
    }
    for (std::thread &thread : threads)
        thread.join();
}

} // namespace detail

/**
 * In-place StackBlur on a QImage.
 * @param img    Must be Format_ARGB32 or Format_RGB32
 * @param radius Blur radius (1-254)
 */
inline void stackBlur(QImage &img, int radius)
{
    if (radius < 1 || radius > 254 || img.isNull())
        return;

    if (img.format() != QImage::Format_ARGB32 &&
        img.format() != QImage::Format_RGB32)
        img = img.convertToFormat(QImage::Format_ARGB32);

    const int w = img.width();
    const int h = img.height();
    if (w < 2 || h < 2) return;

    unsigned char *pix = img.bits();
    const int stride = img.bytesPerLine();
    const int div = 2 * radius + 1;
    const unsigned short mul_sum = detail::stackblur_mul[radius];
    const unsigned char  shr_sum = detail::stackblur_shr[radius];

    // ═══ Horizontal pass ═══════════════════════════════════
    const auto horizontalPass = [&](int yBegin, int yEnd) {
      std::vector<unsigned char> stack(div * 4);
      for (int y = yBegin; y < yEnd; ++y) {
        unsigned long sr=0, sg=0, sb=0, sa=0;
        unsigned long sir=0, sig=0, sib=0, sia=0;
        unsigned long sor=0, sog=0, sob=0, soa=0;
        unsigned char *row = pix + y * stride;

        for (int i = -radius; i <= radius; ++i) {
            const int cx = std::min(std::max(i, 0), w - 1);
            unsigned char *s = row + cx * 4;
            const int si4 = (i + radius) * 4;
            stack[si4]=s[0]; stack[si4+1]=s[1]; stack[si4+2]=s[2]; stack[si4+3]=s[3];
            const int rbs = radius + 1 - std::abs(i);
            sb += s[0]*rbs; sg += s[1]*rbs; sr += s[2]*rbs; sa += s[3]*rbs;
            if (i > 0) { sib+=s[0]; sig+=s[1]; sir+=s[2]; sia+=s[3]; }
            else       { sob+=s[0]; sog+=s[1]; sor+=s[2]; soa+=s[3]; }
        }

        int sp = radius;
        for (int x = 0; x < w; ++x) {
            row[x*4]=(sb*mul_sum)>>shr_sum; row[x*4+1]=(sg*mul_sum)>>shr_sum;
            row[x*4+2]=(sr*mul_sum)>>shr_sum; row[x*4+3]=(sa*mul_sum)>>shr_sum;

            sb-=sob; sg-=sog; sr-=sor; sa-=soa;
            int ss = (sp+div-radius)%div;
            sob-=stack[ss*4]; sog-=stack[ss*4+1]; sor-=stack[ss*4+2]; soa-=stack[ss*4+3];

            const int nx = std::min(x+radius+1, w-1);
            unsigned char *s = row+nx*4;
            stack[ss*4]=s[0]; stack[ss*4+1]=s[1]; stack[ss*4+2]=s[2]; stack[ss*4+3]=s[3];
            sib+=s[0]; sig+=s[1]; sir+=s[2]; sia+=s[3];
            sb+=sib; sg+=sig; sr+=sir; sa+=sia;

            sp = (sp+1)%div;
            unsigned char *o = &stack[sp*4];
            sob+=o[0]; sog+=o[1]; sor+=o[2]; soa+=o[3];
            sib-=o[0]; sig-=o[1]; sir-=o[2]; sia-=o[3];
        }
      }
    };
    const bool useWorkers = static_cast<qint64>(w) * h >= 512 * 512;
    detail::parallelFor(h, useWorkers, horizontalPass);

    // ═══ Vertical pass ═════════════════════════════════════
    const auto verticalPass = [&](int xBegin, int xEnd) {
      std::vector<unsigned char> stack(div * 4);
      for (int x = xBegin; x < xEnd; ++x) {
        unsigned long sr=0, sg=0, sb=0, sa=0;
        unsigned long sir=0, sig=0, sib=0, sia=0;
        unsigned long sor=0, sog=0, sob=0, soa=0;

        for (int i = -radius; i <= radius; ++i) {
            const int cy = std::min(std::max(i, 0), h - 1);
            unsigned char *s = pix + cy*stride + x*4;
            const int si4 = (i + radius) * 4;
            stack[si4]=s[0]; stack[si4+1]=s[1]; stack[si4+2]=s[2]; stack[si4+3]=s[3];
            const int rbs = radius + 1 - std::abs(i);
            sb += s[0]*rbs; sg += s[1]*rbs; sr += s[2]*rbs; sa += s[3]*rbs;
            if (i > 0) { sib+=s[0]; sig+=s[1]; sir+=s[2]; sia+=s[3]; }
            else       { sob+=s[0]; sog+=s[1]; sor+=s[2]; soa+=s[3]; }
        }

        int sp = radius;
        for (int y = 0; y < h; ++y) {
            unsigned char *d = pix + y*stride + x*4;
            d[0]=(sb*mul_sum)>>shr_sum; d[1]=(sg*mul_sum)>>shr_sum;
            d[2]=(sr*mul_sum)>>shr_sum; d[3]=(sa*mul_sum)>>shr_sum;

            sb-=sob; sg-=sog; sr-=sor; sa-=soa;
            int ss = (sp+div-radius)%div;
            sob-=stack[ss*4]; sog-=stack[ss*4+1]; sor-=stack[ss*4+2]; soa-=stack[ss*4+3];

            const int ny = std::min(y+radius+1, h-1);
            unsigned char *s = pix + ny*stride + x*4;
            stack[ss*4]=s[0]; stack[ss*4+1]=s[1]; stack[ss*4+2]=s[2]; stack[ss*4+3]=s[3];
            sib+=s[0]; sig+=s[1]; sir+=s[2]; sia+=s[3];
            sb+=sib; sg+=sig; sr+=sir; sa+=sia;

            sp = (sp+1)%div;
            unsigned char *o = &stack[sp*4];
            sob+=o[0]; sog+=o[1]; sor+=o[2]; soa+=o[3];
            sib-=o[0]; sig-=o[1]; sir-=o[2]; sia-=o[3];
        }
      }
    };
    detail::parallelFor(w, useWorkers, verticalPass);
}

/** Create a blurred copy without modifying the original. */
inline QImage blurredCopy(const QImage &source, int radius)
{
    QImage result = source.copy();
    stackBlur(result, radius);
    return result;
}

} // namespace GlassEffect
