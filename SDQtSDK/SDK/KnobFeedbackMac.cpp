#include "KnobFeedback.h"
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>
#include <ImageIO/ImageIO.h>
#include <limits>


namespace {
template<typename T> class Ref {
public:
    explicit Ref(T value) : value(value) {}
    ~Ref() { if (value) CFRelease(value); }
    Ref(const Ref &) = delete;
    Ref &operator=(const Ref &) = delete;
    T get() const { return value; }
    explicit operator bool() const { return value != nullptr; }
private:
    T value;
};

CTLineRef makeLine(CFStringRef string, CTFontRef font, CGColorRef color) {
    const void *keys[] = { kCTFontAttributeName, kCTForegroundColorAttributeName };
    const void *values[] = { font, color };
    Ref<CFDictionaryRef> attributes(CFDictionaryCreate(kCFAllocatorDefault, keys, values, 2,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
    if (!attributes) return nullptr;
    Ref<CFAttributedStringRef> text(CFAttributedStringCreate(kCFAllocatorDefault, string, attributes.get()));
    return text ? CTLineCreateWithAttributedString(text.get()) : nullptr;
}

bool text(CGContextRef context, const FeedbackCommand &command, CGRect rect, CGColorRef color) {
    if (command.text.isEmpty()) return true;
    const QByteArray utf8 = command.text.toUtf8();
    Ref<CFStringRef> string(CFStringCreateWithBytes(kCFAllocatorDefault,
        reinterpret_cast<const UInt8 *>(utf8.constData()), utf8.size(), kCFStringEncodingUTF8, false));
    Ref<CTFontRef> regular(CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, command.fontSize, nullptr));
    if (!string || !regular) return false;
    Ref<CTFontRef> bold(command.fontWeight >= 600 ? CTFontCreateCopyWithSymbolicTraits(regular.get(),
        command.fontSize, nullptr, kCTFontBoldTrait, kCTFontBoldTrait) : nullptr);
    Ref<CTLineRef> line(makeLine(string.get(), bold ? bold.get() : regular.get(), color));
    if (!line) return false;
    double width = CTLineGetTypographicBounds(line.get(), nullptr, nullptr, nullptr);
    Ref<CTLineRef> ellipsis(makeLine(CFSTR("\u2026"), bold ? bold.get() : regular.get(), color));
    const bool truncate = command.overflow == "ellipsis" && width > rect.size.width;
    Ref<CTLineRef> truncated(truncate && ellipsis ?
        CTLineCreateTruncatedLine(line.get(), rect.size.width, kCTLineTruncationEnd, ellipsis.get()) : nullptr);
    CTLineRef draw = truncated ? truncated.get() : line.get();
    CGFloat ascent = 0, descent = 0;
    width = CTLineGetTypographicBounds(draw, &ascent, &descent, nullptr);
    const double scale = command.overflow == "fit" ? qMin(1.0, qMin(
        rect.size.width / qMax(1.0, width), rect.size.height / qMax(1.0, double(ascent + descent)))) : 1.0;
    width *= scale;
    ascent *= scale;
    descent *= scale;
    CGContextSaveGState(context);
    CGContextClipToRect(context, rect);
    CGContextSetTextMatrix(context, CGAffineTransformIdentity);
    const double x = command.alignment == "right" ? CGRectGetMaxX(rect) - width :
        command.alignment == "left" ? rect.origin.x : rect.origin.x + (rect.size.width - width) / 2;
    CGContextTranslateCTM(context, x, rect.origin.y + (rect.size.height - ascent - descent) / 2 + descent);
    CGContextScaleCTM(context, scale, scale);
    CGContextSetTextPosition(context, 0, 0);
    CTLineDraw(draw, context);
    CGContextRestoreGState(context);
    return true;
}
}

QByteArray RenderNativeFeedback(const FeedbackScene &scene)
{
    Ref<CGColorSpaceRef> colorSpace(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
    if (!colorSpace) return {};
    Ref<CGContextRef> context(CGBitmapContextCreate(nullptr, scene.outputSize.width(), scene.outputSize.height(),
        8, 0, colorSpace.get(), kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big));
    if (!context) return {};
    CGContextScaleCTM(context.get(), static_cast<double>(scene.outputSize.width()) / scene.logicalSize.width(),
                                   static_cast<double>(scene.outputSize.height()) / scene.logicalSize.height());
    CGContextSetRGBFillColor(context.get(), ((scene.background >> 16) & 255) / 255.0,
        ((scene.background >> 8) & 255) / 255.0, (scene.background & 255) / 255.0, (scene.background >> 24) / 255.0);
    CGContextFillRect(context.get(), CGRectMake(0, 0, scene.logicalSize.width(), scene.logicalSize.height()));
    for (const auto &command : scene.commands) {
        const auto &r = command.rect;
        const CGRect rect = CGRectMake(r.x(), scene.logicalSize.height() - r.bottom(), r.width(), r.height());
        Ref<CGColorRef> color(CGColorCreateGenericRGB(((command.color >> 16) & 255) / 255.0,
            ((command.color >> 8) & 255) / 255.0, (command.color & 255) / 255.0,
            ((command.color >> 24) & 255) / 255.0 * command.opacity));
        if (!color) return {};
        CGContextSetFillColorWithColor(context.get(), color.get());
        if (command.type == FeedbackCommand::Fill) {
            CGContextSetShouldAntialias(context.get(), false);
            CGContextFillRect(context.get(), rect);
            CGContextSetShouldAntialias(context.get(), true);
        } else if (command.type == FeedbackCommand::Triangle) {
            CGContextBeginPath(context.get());
            CGContextMoveToPoint(context.get(), CGRectGetMidX(rect), CGRectGetMaxY(rect));
            CGContextAddLineToPoint(context.get(), CGRectGetMinX(rect), CGRectGetMinY(rect));
            CGContextAddLineToPoint(context.get(), CGRectGetMaxX(rect), CGRectGetMinY(rect));
            CGContextClosePath(context.get());
            CGContextFillPath(context.get());
        } else if (command.type == FeedbackCommand::Text) {
            if (!text(context.get(), command, rect, color.get())) return {};
        } else if (!command.image.isEmpty()) {
            Ref<CFDataRef> data(CFDataCreate(kCFAllocatorDefault,
                reinterpret_cast<const UInt8 *>(command.image.constData()), command.image.size()));
            Ref<CGImageSourceRef> source(data ? CGImageSourceCreateWithData(data.get(), nullptr) : nullptr);
            Ref<CGImageRef> bitmap(source ? CGImageSourceCreateImageAtIndex(source.get(), 0, nullptr) : nullptr);
            if (!bitmap || !CGImageGetWidth(bitmap.get()) || !CGImageGetHeight(bitmap.get())) return {};
            const double scale = qMin(r.width() / CGImageGetWidth(bitmap.get()), r.height() / CGImageGetHeight(bitmap.get()));
            const double width = CGImageGetWidth(bitmap.get()) * scale, height = CGImageGetHeight(bitmap.get()) * scale;
            CGContextSaveGState(context.get());
            CGContextSetAlpha(context.get(), command.opacity);
            CGContextSetInterpolationQuality(context.get(), kCGInterpolationHigh);
            CGContextDrawImage(context.get(), CGRectMake(CGRectGetMidX(rect) - width / 2,
                CGRectGetMidY(rect) - height / 2, width, height), bitmap.get());
            CGContextRestoreGState(context.get());
        }
    }
    Ref<CGImageRef> image(CGBitmapContextCreateImage(context.get()));
    Ref<CFMutableDataRef> png(CFDataCreateMutable(kCFAllocatorDefault, 0));
    if (!image || !png) return {};
    Ref<CGImageDestinationRef> destination(CGImageDestinationCreateWithData(png.get(), CFSTR("public.png"), 1, nullptr));
    if (!destination) return {};
    CGImageDestinationAddImage(destination.get(), image.get(), nullptr);
    if (!CGImageDestinationFinalize(destination.get()) || CFDataGetLength(png.get()) > std::numeric_limits<int>::max()) return {};
    return QByteArray(reinterpret_cast<const char *>(CFDataGetBytePtr(png.get())), static_cast<int>(CFDataGetLength(png.get())));
}
